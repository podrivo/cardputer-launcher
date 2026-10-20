// Mini Launcher — list packed ota_* apps; Enter one-shot boots (rollback on power-off).

#include <cstdio>
#include <cstring>

#include "driver/i2c_master.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
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

// Physical keycodes (ADV TCA8418 scan)
constexpr uint8_t KEY_LEFT = 54;
constexpr uint8_t KEY_RIGHT = 64;
constexpr uint8_t KEY_ENTER = 67;
constexpr uint8_t KEY_UP = 57;
constexpr uint8_t KEY_DOWN = 58;

struct AppEntry {
    const esp_partition_t *part;
    char name[kNameLen];
};

i2c_master_bus_handle_t i2c_bus;
i2c_master_dev_handle_t tca;

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
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = KBD_SDA,
        .scl_io_num = KBD_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = true},
    };
    if (i2c_new_master_bus(&bus_cfg, &i2c_bus) != ESP_OK) {
        return false;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = KBD_ADDR,
        .scl_speed_hz = KBD_HZ,
    };
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
    // ESP-IDF: ota_0 … ota_15 → subtype 0x10 … 0x1F
    return subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
           subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MIN + 15;
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
        std::snprintf(out[n].name, sizeof(out[n].name), "%s", desc.project_name);
        if (out[n].name[0] == '\0') {
            std::snprintf(out[n].name, sizeof(out[n].name), "ota_%u",
                          static_cast<unsigned>(part->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN));
        }
        ++n;
    }
    if (it) {
        esp_partition_iterator_release(it);
    }
    return n;
}

void boot_app(const AppEntry &app)
{
    ESP_LOGI(TAG, "One-shot boot %s @ 0x%08lx", app.name, static_cast<unsigned long>(app.part->address));
    display_show_booting(app.name);
    // Never mark valid — power cycle rolls back to factory (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
    esp_err_t err = esp_ota_set_boot_partition(app.part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

} // namespace

extern "C" void app_main(void)
{
    // Keep app tables out of the main task stack — M5GFX init needs the room.
    static AppEntry apps[kMaxApps];
    static launcher_app_view views[kMaxApps];

    display_init();

    const size_t count = discover_apps(apps, kMaxApps);
    for (size_t i = 0; i < count; ++i) {
        views[i].name = apps[i].name;
    }

    if (!kbd_init()) {
        ESP_LOGE(TAG, "TCA8418 init failed — is this a Cardputer ADV?");
        display_show_kbd_error();
        return;
    }

    size_t selected = 0;
    if (count == 0) {
        display_show_empty();
    } else {
        display_show_list(views, count, selected);
    }
    ESP_LOGI(TAG, "%u app(s)", static_cast<unsigned>(count));

    for (;;) {
        uint8_t key = 0;
        if (!kbd_poll_press(&key)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (count == 0) {
            continue;
        }

        bool redraw = false;
        switch (key) {
        case KEY_LEFT:
        case KEY_UP:
            selected = (selected == 0) ? count - 1 : selected - 1;
            redraw = true;
            break;
        case KEY_RIGHT:
        case KEY_DOWN:
            selected = (selected + 1) % count;
            redraw = true;
            break;
        case KEY_ENTER:
            boot_app(apps[selected]);
            // If boot failed, redraw list.
            redraw = true;
            break;
        default:
            break;
        }

        if (redraw) {
            display_show_list(views, count, selected);
        }
    }
}
