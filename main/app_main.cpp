// Mini Launcher — list packed ota_* apps; Enter launches via deep-sleep retain.
// Power-off clears RTC → bootloader falls back to factory (this UI).

#include <cstdio>
#include <cstring>

#include "bootloader_common.h"
#include "driver/i2c_master.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_flash_partitions.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_md5.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.hpp"

namespace {

constexpr char TAG[] = "launcher";

constexpr size_t kMaxApps = 16;
constexpr size_t kNameLen = 32;

// Cardputer ADV keyboard (TCA8418)
constexpr gpio_num_t KBD_SDA = GPIO_NUM_8;
constexpr gpio_num_t KBD_SCL = GPIO_NUM_9;
constexpr uint8_t KBD_ADDR = 0x34;
constexpr uint32_t KBD_HZ = 400000;

constexpr uint8_t TCA_REG_CFG = 0x01;
constexpr uint8_t TCA_REG_INT_STAT = 0x02;
constexpr uint8_t TCA_REG_KEY_EVENT_A = 0x04;
constexpr uint8_t TCA_REG_KP_GPIO1 = 0x1D;
constexpr uint8_t TCA_REG_KP_GPIO2 = 0x1E;
constexpr uint8_t TCA_REG_KP_GPIO3 = 0x1F;

// Physical keycodes (ADV TCA8418 scan, raw events)
constexpr uint8_t KEY_ESC = 1;
constexpr uint8_t KEY_MINUS = 55;
constexpr uint8_t KEY_PLUS = 61;
constexpr uint8_t KEY_DEL = 65;
constexpr uint8_t KEY_LEFT = 54;
constexpr uint8_t KEY_RIGHT = 64;
constexpr uint8_t KEY_ENTER = 67;
constexpr uint8_t KEY_UP = 57;
constexpr uint8_t KEY_DOWN = 58;

constexpr uint32_t kTableOff = 0x8000;
constexpr uint32_t kTableSize = 0xc00;
constexpr size_t kEntrySize = 32;
constexpr uint16_t kPartMagic = 0x50aa;
constexpr uint16_t kMd5Magic = 0xebeb;
constexpr size_t kMaxParts = 32;
constexpr uint32_t kEraseAlign = 0x1000;

struct AppEntry {
    const esp_partition_t *part;
    char name[kNameLen];
};

struct FlashPart {
    uint8_t type;
    uint8_t subtype;
    uint32_t address;
    uint32_t size;
    char label[16];
    uint32_t flags;
};

i2c_master_bus_handle_t i2c_bus = nullptr;
i2c_master_dev_handle_t tca = nullptr;

esp_err_t tca_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(tca, buf, sizeof(buf), 100);
}

esp_err_t tca_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(tca, &reg, 1, val, 1, 100);
}

bool kbd_init()
{
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = KBD_SDA;
    bus_cfg.scl_io_num = KBD_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus_cfg, &i2c_bus) != ESP_OK) {
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = KBD_ADDR;
    dev_cfg.scl_speed_hz = KBD_HZ;
    if (i2c_master_bus_add_device(i2c_bus, &dev_cfg, &tca) != ESP_OK) {
        return false;
    }

    if (tca_write(TCA_REG_KP_GPIO1, 0x7F) != ESP_OK) {
        return false;
    }
    tca_write(TCA_REG_KP_GPIO2, 0xFF);
    tca_write(TCA_REG_KP_GPIO3, 0x00);
    tca_write(TCA_REG_CFG, 0x01);
    tca_write(TCA_REG_INT_STAT, 0x0F);
    return true;
}

bool kbd_poll_press(uint8_t *keycode)
{
    uint8_t ev = 0;
    if (tca_read(TCA_REG_KEY_EVENT_A, &ev) != ESP_OK || ev == 0) {
        return false;
    }
    tca_write(TCA_REG_INT_STAT, 0x0F);
    if ((ev & 0x80) == 0) {
        return false;
    }
    *keycode = ev & 0x7F;
    return true;
}

bool is_ota_subtype(uint8_t subtype)
{
    return subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
           subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MIN + 15;
}

bool is_generic_name(const char *s)
{
    if (s == nullptr || s[0] == '\0') {
        return true;
    }
    if (std::strcmp(s, "app") == 0 || std::strcmp(s, "firmware") == 0 ||
        std::strcmp(s, "arduino-lib-builder") == 0 || std::strcmp(s, "project") == 0) {
        return true;
    }
    // Bare ota_N partition labels
    if (std::strncmp(s, "ota_", 4) == 0) {
        const char *p = s + 4;
        if (*p == '\0') {
            return true;
        }
        while (*p) {
            if (*p < '0' || *p > '9') {
                return false;
            }
            ++p;
        }
        return true;
    }
    return false;
}

void fill_app_name(AppEntry *entry, const esp_partition_t *part, const esp_app_desc_t &desc)
{
    // 1) Partition label from packer / installers (catalog name, truncated)
    if (!is_generic_name(part->label)) {
        std::snprintf(entry->name, sizeof(entry->name), "%s", part->label);
        return;
    }
    // 2) CMake / IDF project_name
    if (!is_generic_name(desc.project_name)) {
        std::snprintf(entry->name, sizeof(entry->name), "%s", desc.project_name);
        return;
    }
    // 3) Version string when it looks like a real name (not a short git sha)
    if (!is_generic_name(desc.version) && std::strlen(desc.version) > 2) {
        bool mostly_hex = true;
        for (const char *p = desc.version; *p; ++p) {
            const char c = *p;
            const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!hex) {
                mostly_hex = false;
                break;
            }
        }
        if (!mostly_hex || std::strlen(desc.version) > 10) {
            std::snprintf(entry->name, sizeof(entry->name), "%s", desc.version);
            return;
        }
    }
    // 4) Fallback
    std::snprintf(entry->name, sizeof(entry->name), "ota_%u",
                  static_cast<unsigned>(part->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN));
}

size_t discover_apps(AppEntry *out, size_t cap)
{
    size_t n = 0;
    esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (it != nullptr && n < cap) {
        const esp_partition_t *part = esp_partition_get(it);
        it = esp_partition_next(it);
        if (part == nullptr || part->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            continue;
        }
        if (!is_ota_subtype(part->subtype)) {
            continue;
        }

        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(part, &desc) != ESP_OK) {
            continue;
        }

        out[n].part = part;
        fill_app_name(&out[n], part, desc);
        ++n;
    }
    if (it) {
        esp_partition_iterator_release(it);
    }
    return n;
}

/** Keep otadata on factory so cold boot always lands here. */
void ensure_factory_boot_target()
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (factory == nullptr) {
        return;
    }
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (boot != nullptr && boot->address == factory->address) {
        return;
    }
    // Erases otadata → next cold boot uses factory.
    esp_err_t err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ensure factory boot: %s", esp_err_to_name(err));
    }
}

void boot_app(const AppEntry &app)
{
    ESP_LOGI(TAG, "Deep-sleep launch %s @ 0x%08lx", app.name,
             static_cast<unsigned long>(app.part->address));
    display_show_booting(app.name);

    // Stay on factory in otadata. Point RTC retain at the app; deep-sleep wake
    // makes the bootloader load it without changing the permanent boot target.
    esp_partition_pos_t pos = {
        .offset = app.part->address,
        .size = app.part->size,
    };
    bootloader_common_update_rtc_retain_mem(&pos, true);

    esp_sleep_enable_timer_wakeup(50 * 1000); // 50 ms
    esp_deep_sleep_start();
}

uint32_t align_up(uint32_t n, uint32_t a)
{
    return (n + (a - 1)) & ~(a - 1);
}

size_t read_part_table(FlashPart *out, size_t cap)
{
    static uint8_t table[kTableSize];
    if (esp_flash_read(nullptr, table, kTableOff, kTableSize) != ESP_OK) {
        return 0;
    }
    size_t n = 0;
    for (size_t o = 0; o + kEntrySize <= kTableSize && n < cap; o += kEntrySize) {
        const uint16_t magic = static_cast<uint16_t>(table[o] | (table[o + 1] << 8));
        if (magic == kMd5Magic) {
            break;
        }
        if (magic != kPartMagic) {
            break;
        }
        FlashPart &p = out[n];
        p.type = table[o + 2];
        p.subtype = table[o + 3];
        p.address = table[o + 4] | (table[o + 5] << 8) | (table[o + 6] << 16) |
                    (static_cast<uint32_t>(table[o + 7]) << 24);
        p.size = table[o + 8] | (table[o + 9] << 8) | (table[o + 10] << 16) |
                 (static_cast<uint32_t>(table[o + 11]) << 24);
        std::memcpy(p.label, &table[o + 12], 15);
        p.label[15] = '\0';
        p.flags = table[o + 28] | (table[o + 29] << 8) | (table[o + 30] << 16) |
                  (static_cast<uint32_t>(table[o + 31]) << 24);
        ++n;
    }
    return n;
}

void encode_part_entry(uint8_t *raw, const FlashPart &p)
{
    std::memset(raw, 0, kEntrySize);
    raw[0] = kPartMagic & 0xff;
    raw[1] = (kPartMagic >> 8) & 0xff;
    raw[2] = p.type;
    raw[3] = p.subtype;
    raw[4] = p.address & 0xff;
    raw[5] = (p.address >> 8) & 0xff;
    raw[6] = (p.address >> 16) & 0xff;
    raw[7] = (p.address >> 24) & 0xff;
    raw[8] = p.size & 0xff;
    raw[9] = (p.size >> 8) & 0xff;
    raw[10] = (p.size >> 16) & 0xff;
    raw[11] = (p.size >> 24) & 0xff;
    std::memcpy(&raw[12], p.label, 15);
    raw[28] = p.flags & 0xff;
    raw[29] = (p.flags >> 8) & 0xff;
    raw[30] = (p.flags >> 16) & 0xff;
    raw[31] = (p.flags >> 24) & 0xff;
}

esp_err_t write_part_table(const FlashPart *parts, size_t count)
{
    static uint8_t table[kTableSize];
    std::memset(table, 0xff, sizeof(table));
    size_t o = 0;
    for (size_t i = 0; i < count; ++i) {
        encode_part_entry(&table[o], parts[i]);
        o += kEntrySize;
    }
    md5_context_t ctx;
    esp_rom_md5_init(&ctx);
    esp_rom_md5_update(&ctx, table, static_cast<uint32_t>(o));
    uint8_t digest[ESP_ROM_MD5_DIGEST_LEN];
    esp_rom_md5_final(digest, &ctx);
    table[o] = kMd5Magic & 0xff;
    table[o + 1] = (kMd5Magic >> 8) & 0xff;
    std::memset(&table[o + 2], 0xff, 14);
    std::memcpy(&table[o + 16], digest, sizeof(digest));

    esp_err_t err = esp_flash_erase_region(nullptr, kTableOff, align_up(kTableSize, kEraseAlign));
    if (err != ESP_OK) {
        return err;
    }
    return esp_flash_write(nullptr, table, kTableOff, kTableSize);
}

/** Drop app from the partition table only (no flash compact — much faster). */
esp_err_t delete_app_at(uint32_t remove_addr)
{
    static FlashPart parts[kMaxParts];
    static FlashPart final_parts[kMaxParts];
    const size_t n = read_part_table(parts, kMaxParts);
    if (n == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    FlashPart otas[kMaxApps];
    size_t ota_n = 0;
    size_t final_n = 0;
    bool found = false;

    for (size_t i = 0; i < n; ++i) {
        if (parts[i].type == ESP_PARTITION_TYPE_APP && is_ota_subtype(parts[i].subtype)) {
            if (parts[i].address == remove_addr) {
                found = true;
                continue;
            }
            otas[ota_n++] = parts[i];
        } else {
            final_parts[final_n++] = parts[i];
        }
    }
    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 1; i < ota_n; ++i) {
        FlashPart key = otas[i];
        size_t j = i;
        while (j > 0 && otas[j - 1].address > key.address) {
            otas[j] = otas[j - 1];
            --j;
        }
        otas[j] = key;
    }
    for (size_t i = 0; i < ota_n; ++i) {
        otas[i].subtype = static_cast<uint8_t>(ESP_PARTITION_SUBTYPE_APP_OTA_MIN + i);
        final_parts[final_n++] = otas[i];
    }

    esp_err_t err = write_part_table(final_parts, final_n);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write table failed: %s", esp_err_to_name(err));
    }
    return err;
}

} // namespace

extern "C" void app_main(void)
{
    // Keep app tables out of the main task stack — M5GFX init needs the room.
    static AppEntry apps[kMaxApps];
    static launcher_app_view views[kMaxApps];

    display_init();
    display_show_splash();

    ensure_factory_boot_target();

    const size_t count = discover_apps(apps, kMaxApps);
    for (size_t i = 0; i < count; ++i) {
        views[i].name = apps[i].name;
    }

    if (!kbd_init()) {
        ESP_LOGE(TAG, "TCA8418 init failed — is this a Cardputer ADV?");
        display_show_kbd_error();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    size_t selected = 0;
    bool show_shortcuts = false;
    bool confirm_delete = false;

    auto redraw = [&]() {
        if (confirm_delete && count > 0) {
            display_show_delete_confirm(apps[selected].name);
        } else if (show_shortcuts) {
            display_show_shortcuts();
        } else if (count == 0) {
            display_show_empty();
        } else {
            display_show_list(views, count, selected);
        }
    };
    redraw();
    ESP_LOGI(TAG, "%u app(s)", static_cast<unsigned>(count));

    for (;;) {
        uint8_t key = 0;
        if (!kbd_poll_press(&key)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (key == KEY_MINUS || key == KEY_PLUS) {
            display_adjust_brightness(key == KEY_PLUS ? 1 : -1);
            continue;
        }

        if (confirm_delete) {
            if (key == KEY_DEL || key == KEY_ESC) {
                confirm_delete = false;
                redraw();
            } else if (key == KEY_ENTER && count > 0) {
                display_show_deleting(apps[selected].name);
                const esp_err_t err = delete_app_at(apps[selected].part->address);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "delete failed: %s", esp_err_to_name(err));
                    display_show_delete_fail();
                    vTaskDelay(pdMS_TO_TICKS(1200));
                    confirm_delete = false;
                    redraw();
                } else {
                    ESP_LOGI(TAG, "deleted %s — restarting", apps[selected].name);
                    esp_restart();
                }
            }
            continue;
        }

        if (key == KEY_ESC) {
            show_shortcuts = !show_shortcuts;
            redraw();
            continue;
        }

        if (show_shortcuts || count == 0) {
            continue;
        }

        bool need_redraw = false;
        switch (key) {
        case KEY_LEFT:
        case KEY_UP:
            selected = (selected == 0) ? count - 1 : selected - 1;
            need_redraw = true;
            break;
        case KEY_RIGHT:
        case KEY_DOWN:
            selected = (selected + 1) % count;
            need_redraw = true;
            break;
        case KEY_DEL:
            confirm_delete = true;
            need_redraw = true;
            break;
        case KEY_ENTER:
            boot_app(apps[selected]);
            need_redraw = true;
            break;
        default:
            break;
        }

        if (need_redraw) {
            redraw();
        }
    }
}
