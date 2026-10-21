#include "pch_display.hpp"

#include <cstdio>
#include <cstring>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "display.hpp"
#include "theme.hpp"

namespace {

M5GFX display;

constexpr int kBrightMin = 0;
constexpr int kBrightMax = 100;
constexpr int kBrightStep = 10;
int brightness_percent = 80;

void apply_brightness()
{
    display.setBrightness(static_cast<uint8_t>((brightness_percent * 255) / kBrightMax));
}

constexpr int kBodyFontPx = 8;

void set_body_font()
{
    display.setTextSize(static_cast<float>(kBodyFontPx) / fonts::Font0.height);
}

// Cardputer ADV: BAT+ → 100k → GPIO10/ADC1_CH9 → 100k → GND (docs + schematic).
// TP4057 CHRG/STDBY are not wired to the ESP32, so charge state is inferred.
constexpr adc_unit_t kBatAdcUnit = ADC_UNIT_1;
constexpr adc_channel_t kBatAdcChan = ADC_CHANNEL_9;
constexpr adc_atten_t kBatAdcAtten = ADC_ATTEN_DB_12;

adc_oneshot_unit_handle_t bat_adc = nullptr;
adc_cali_handle_t bat_cali = nullptr;
bool bat_ready = false;

// Soft state + NVS. USB plug-in usually resets the S3, so RAM alone cannot
// hold SoC across the unplug→plug transition the user actually notices.
int bat_mv_filt = 0;
int bat_pct_disp = -1;
bool bat_charging = false;
bool bat_have_rest = false;
bool bat_nvs_tried = false;

constexpr char kBatNvsNs[] = "bat";
constexpr char kBatNvsKey[] = "rest_pct";

void bat_nvs_ensure()
{
    if (bat_nvs_tried) {
        return;
    }
    bat_nvs_tried = true;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return;
    }
    nvs_handle_t h = 0;
    if (nvs_open(kBatNvsNs, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v = 0;
    if (nvs_get_u8(h, kBatNvsKey, &v) == ESP_OK && v <= 100) {
        bat_pct_disp = v;
        bat_have_rest = true;
    }
    nvs_close(h);
}

void bat_nvs_save(int pct)
{
    if (pct < 0 || pct > 100) {
        return;
    }
    bat_nvs_ensure();
    nvs_handle_t h = 0;
    if (nvs_open(kBatNvsNs, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    uint8_t cur = 255;
    if (nvs_get_u8(h, kBatNvsKey, &cur) == ESP_OK && cur == static_cast<uint8_t>(pct)) {
        nvs_close(h);
        return;
    }
    if (nvs_set_u8(h, kBatNvsKey, static_cast<uint8_t>(pct)) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

bool ensure_battery_adc()
{
    if (bat_ready) {
        return true;
    }
    adc_oneshot_unit_init_cfg_t init_cfg = {};
    init_cfg.unit_id = kBatAdcUnit;
    if (adc_oneshot_new_unit(&init_cfg, &bat_adc) != ESP_OK) {
        return false;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = kBatAdcAtten;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_oneshot_config_channel(bat_adc, kBatAdcChan, &chan_cfg) != ESP_OK) {
        return false;
    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = kBatAdcUnit;
    cali_cfg.chan = kBatAdcChan;
    cali_cfg.atten = kBatAdcAtten;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &bat_cali) != ESP_OK) {
        bat_cali = nullptr;
    }
#endif
    bat_ready = true;
    return true;
}

int read_battery_mv()
{
    if (!ensure_battery_adc()) {
        return -1;
    }
    int raw_sum = 0;
    constexpr int kSamples = 8;
    for (int i = 0; i < kSamples; ++i) {
        int raw = 0;
        if (adc_oneshot_read(bat_adc, kBatAdcChan, &raw) != ESP_OK) {
            return -1;
        }
        raw_sum += raw;
    }
    const int raw = raw_sum / kSamples;
    int half_mv = 0;
    if (bat_cali) {
        if (adc_cali_raw_to_voltage(bat_cali, raw, &half_mv) != ESP_OK) {
            return -1;
        }
    } else {
        // ~0–3100 mV at 12 dB atten when eFuse calibration is missing.
        half_mv = (raw * 3100) / 4095;
    }
    return half_mv * 2;
}

// Piecewise LiPo under light load (Cardputer ~120 mA). Better than linear
// 3.3–4.15 V: most capacity sits in a narrow mid band.
int voltage_to_percent(int mv)
{
    static constexpr struct {
        int mv;
        int pct;
    } kCurve[] = {
        {3300, 0},  {3450, 5},  {3550, 10}, {3650, 20}, {3720, 30},
        {3780, 40}, {3830, 50}, {3880, 60}, {3930, 70}, {3980, 80},
        {4050, 90}, {4120, 97}, {4180, 100},
    };
    if (mv <= kCurve[0].mv) {
        return 0;
    }
    constexpr size_t n = sizeof(kCurve) / sizeof(kCurve[0]);
    if (mv >= kCurve[n - 1].mv) {
        return 100;
    }
    for (size_t i = 1; i < n; ++i) {
        if (mv <= kCurve[i].mv) {
            const int mv0 = kCurve[i - 1].mv;
            const int mv1 = kCurve[i].mv;
            const int p0 = kCurve[i - 1].pct;
            const int p1 = kCurve[i].pct;
            return p0 + ((mv - mv0) * (p1 - p0)) / (mv1 - mv0);
        }
    }
    return 100;
}

struct battery_reading {
    int pct; // 0–100 when known; ignore when !known
    bool charging;
    bool known;
};

// Infer charging: charger lifts BAT+; CHRG pin is unavailable on this board.
battery_reading read_battery()
{
    bat_nvs_ensure();

    const int mv_raw = read_battery_mv();
    if (mv_raw < 0) {
        return {-1, false, false};
    }

    const int prev = bat_mv_filt;
    if (bat_mv_filt <= 0) {
        bat_mv_filt = mv_raw;
    } else {
        // EMA ~1/4 toward new sample — calm ADC noise without lagging forever.
        bat_mv_filt += (mv_raw - bat_mv_filt) / 4;
    }
    const int mv = bat_mv_filt;
    const int pct_now = voltage_to_percent(mv);

    // Absolute: under load a cell rarely sits ≥4.05 V unless a charger is on.
    // Delta: plug-in jumps the rail faster than chemistry can.
    const bool rise = prev > 0 && mv_raw >= prev + 100;
    const bool high = mv >= 4050 || mv_raw >= 4080;
    if (high || (rise && mv_raw >= 3780)) {
        bat_charging = true;
    } else if (mv <= 3980 && mv_raw <= 4000 && !rise) {
        bat_charging = false;
    }

    if (bat_charging) {
        // Never trust inflated charge voltage as SoC. Use last rest (NVS/RAM).
        if (bat_have_rest && bat_pct_disp >= 0) {
            return {bat_pct_disp, true, true};
        }
        return {0, true, false};
    }

    bat_have_rest = true;
    bat_pct_disp = pct_now;
    bat_nvs_save(pct_now);
    return {bat_pct_disp, false, true};
}

uint16_t lerp_rgb565(uint16_t a, uint16_t b, int t)
{
    if (t <= 0) {
        return a;
    }
    if (t >= 100) {
        return b;
    }
    const int ar = (a >> 11) & 0x1F;
    const int ag = (a >> 5) & 0x3F;
    const int ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F;
    const int bg = (b >> 5) & 0x3F;
    const int bb = b & 0x1F;
    const int r = ar + ((br - ar) * t) / 100;
    const int g = ag + ((bg - ag) * t) / 100;
    const int bl = ab + ((bb - ab) * t) / 100;
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

uint16_t battery_status_color(int pct)
{
    // Ok / critical follow the palette; warn is a warm mid tinted by both.
    const uint16_t ok = theme_col_sel;
    const uint16_t critical = theme_col_err;
    constexpr uint16_t kYellow = 0xFFE0;
    const uint16_t warn =
        lerp_rgb565(kYellow, lerp_rgb565(ok, critical, 50), 45);

    if (pct > 80) {
        return ok;
    }
    if (pct > 60) {
        return lerp_rgb565(ok, warn, 35);
    }
    if (pct > 40) {
        return lerp_rgb565(ok, warn, 70);
    }
    if (pct > 25) {
        return warn;
    }
    if (pct > 10) {
        return lerp_rgb565(warn, critical, 55);
    }
    return critical;
}

void draw_charge_bolt(int x, int y, uint16_t color)
{
    // Compact 5×7 bolt inside the cell.
    display.drawLine(x + 2, y, x, y + 3, color);
    display.drawLine(x, y + 3, x + 3, y + 3, color);
    display.drawLine(x + 3, y + 3, x + 1, y + 6, color);
}

void draw_battery_indicator()
{
    const battery_reading bat = read_battery();
    if (!bat.known && !bat.charging) {
        return;
    }
    set_body_font();
    char label[16];
    if (bat.known) {
        snprintf(label, sizeof(label), "%d%%", bat.pct);
    } else {
        // Charging, but USB reset wiped RAM and we have no saved rest yet.
        snprintf(label, sizeof(label), "--");
    }

    constexpr int kIconW = 18;
    constexpr int kIconH = 8;
    constexpr int kTipW = 2;
    constexpr int kGap = 4;
    constexpr int kPad = 8;
    const int text_w = static_cast<int>(strlen(label)) * display.fontWidth();
    const int total_w = text_w + kGap + kIconW + kTipW;
    const int x0 = display.width() - kPad - total_w;
    const int y0 = display.height() - kPad - kIconH;
    const uint16_t fill =
        bat.known ? battery_status_color(bat.pct) : theme_col_sel;

    display.setTextColor(fill, theme_col_bg);
    display.setCursor(x0, y0);
    display.print(label);

    const int ix = x0 + text_w + kGap;
    display.drawRect(ix, y0, kIconW, kIconH, fill);
    display.fillRect(ix + kIconW, y0 + 2, kTipW, kIconH - 4, fill);
    if (bat.known) {
        const int inner = kIconW - 2;
        const int fill_w = (inner * bat.pct) / 100;
        if (fill_w > 0) {
            display.fillRect(ix + 1, y0 + 1, fill_w, kIconH - 2, fill);
        }
    }
    if (bat.charging) {
        // Clear a notch so the bolt stays readable over the fill.
        display.fillRect(ix + 6, y0 + 1, 6, kIconH - 2, theme_col_bg);
        draw_charge_bolt(ix + 7, y0 + 1, fill);
    }
}

void draw_multiline(const char *text, int x, int y, int stride, uint16_t color)
{
    display.setTextColor(color, theme_col_bg);
    set_body_font();
    int cy = y;
    const char *p = text;
    char line[48];
    while (*p) {
        size_t n = 0;
        while (*p && *p != '\n' && n + 1 < sizeof(line)) {
            line[n++] = *p++;
        }
        line[n] = '\0';
        display.setCursor(x, cy);
        display.print(line);
        cy += stride;
        if (*p == '\n') {
            ++p;
        }
    }
}

} // namespace

void display_init()
{
    display.init();
    display.setRotation(1);
    apply_brightness();
}

int display_adjust_brightness(int delta_steps)
{
    brightness_percent += delta_steps * kBrightStep;
    if (brightness_percent < kBrightMin) {
        brightness_percent = kBrightMin;
    } else if (brightness_percent > kBrightMax) {
        brightness_percent = kBrightMax;
    }
    apply_brightness();
    return brightness_percent;
}

void display_show_splash()
{
    display.fillScreen(theme_col_bg);
    constexpr int kFontPx = 16;
    display.setTextSize(static_cast<float>(kFontPx) / fonts::Font0.height);
    display.setTextColor(theme_col_fg, theme_col_bg);
    display.setCursor(8, 8);
    display.print(theme_title);
}

void display_show_shortcuts()
{
    display.fillScreen(theme_col_bg);
    set_body_font();
    const int stride = display.fontHeight() + 4;
    constexpr int kKeysX = 8;
    constexpr int kLabelX = 40;
    for (size_t i = 0; i < theme_shortcuts_count; ++i) {
        const int y = 8 + static_cast<int>(i) * stride;
        display.setTextColor(theme_col_fg, theme_col_bg);
        // drawChar, not print: print drops glyphs below 0x20 (the font's arrows).
        int x = kKeysX;
        for (const char *k = theme_shortcuts[i].keys; *k; ++k) {
            x += display.drawChar(static_cast<uint8_t>(*k), x, y);
        }
        display.setCursor(kLabelX, y);
        display.print(theme_shortcuts[i].label);
    }
    draw_battery_indicator();
}

void display_show_kbd_error()
{
    display.fillScreen(theme_col_bg);
    display.setTextColor(theme_col_err, theme_col_bg);
    set_body_font();
    display.setCursor(8, 60);
    display.print(theme_kbd_error);
}

void display_show_empty()
{
    display.fillScreen(theme_col_bg);
    set_body_font();
    const int stride = display.fontHeight() + 4;
    draw_multiline(theme_empty, 8, 8, stride, theme_col_fg);
}

void display_show_list(const launcher_app_view *apps, size_t count, size_t selected)
{
    display.fillScreen(theme_col_bg);

    if (count == 0) {
        display_show_empty();
        return;
    }

    constexpr int kListTop = 8;
    constexpr int kListFontPx = 16;
    constexpr int kListStride = (kListFontPx * 150) / 100;
    display.setTextSize(static_cast<float>(kListFontPx) / fonts::Font0.height);

    const size_t kVisible =
        static_cast<size_t>((display.height() - kListTop) / kListStride);
    size_t start = 0;
    if (kVisible > 0 && count > kVisible && selected >= kVisible / 2) {
        start = selected - kVisible / 2;
        if (start + kVisible > count) {
            start = count - kVisible;
        }
    }

    for (size_t i = 0; start + i < count; ++i) {
        const int y = kListTop + static_cast<int>(i) * kListStride;
        if (y >= display.height()) {
            break;
        }
        const size_t idx = start + i;
        const bool on = idx == selected;
        display.setTextColor(on ? theme_col_sel : theme_col_fg, theme_col_bg);
        display.setCursor(8, y);
        display.print(apps[idx].name);
        if (on) {
            display.drawChar(0x1B, display.getCursorX() + display.fontWidth(), y);
        }
    }
}

void display_show_delete_confirm(const char *name)
{
    display.fillScreen(theme_col_bg);
    set_body_font();
    display.setTextColor(theme_col_err, theme_col_bg);
    display.setCursor(8, 8);
    display.printf("Delete %s?", name ? name : "");
    display.setTextColor(theme_col_dim, theme_col_bg);
    display.setCursor(8, 120);
    display.print(theme_delete_hint);
}

void display_show_deleting()
{
    display.fillScreen(theme_col_bg);
    set_body_font();
    display.setTextColor(theme_col_fg, theme_col_bg);
    display.setCursor(8, 8);
    display.print(theme_deleting);
}

void display_show_delete_fail()
{
    display.fillScreen(theme_col_bg);
    set_body_font();
    display.setTextColor(theme_col_err, theme_col_bg);
    display.setCursor(8, 60);
    display.print(theme_delete_fail);
}

void display_show_booting(const char *name)
{
    display.fillScreen(theme_col_bg);
    set_body_font();
    display.setTextColor(theme_col_fg, theme_col_bg);
    display.setCursor(8, 40);
    display.print(theme_booting);
    display.setCursor(8, 60);
    display.print(name ? name : "");
}
