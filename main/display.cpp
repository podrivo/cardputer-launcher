#include "pch_display.hpp"

#include <cstdio>
#include <cstring>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

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
constexpr adc_unit_t kBatAdcUnit = ADC_UNIT_1;
constexpr adc_channel_t kBatAdcChan = ADC_CHANNEL_9;
constexpr adc_atten_t kBatAdcAtten = ADC_ATTEN_DB_12;
constexpr int kBatMvEmpty = 3300;
constexpr int kBatMvFull = 4150;

adc_oneshot_unit_handle_t bat_adc = nullptr;
adc_cali_handle_t bat_cali = nullptr;
bool bat_ready = false;

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

int read_battery_percent()
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
    const int mv = half_mv * 2;
    int pct = ((mv - kBatMvEmpty) * 100) / (kBatMvFull - kBatMvEmpty);
    if (pct < 0) {
        pct = 0;
    } else if (pct > 100) {
        pct = 100;
    }
    return pct;
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

void draw_battery_indicator()
{
    const int pct = read_battery_percent();
    if (pct < 0) {
        return;
    }
    set_body_font();
    char label[16];
    snprintf(label, sizeof(label), "%d%%", pct);

    constexpr int kIconW = 18;
    constexpr int kIconH = 8;
    constexpr int kTipW = 2;
    constexpr int kGap = 4;
    constexpr int kPad = 8;
    const int text_w = static_cast<int>(strlen(label)) * display.fontWidth();
    const int total_w = text_w + kGap + kIconW + kTipW;
    const int x0 = display.width() - kPad - total_w;
    const int y0 = display.height() - kPad - kIconH;
    const uint16_t fill = battery_status_color(pct);

    display.setTextColor(fill, theme_col_bg);
    display.setCursor(x0, y0);
    display.print(label);

    const int ix = x0 + text_w + kGap;
    display.drawRect(ix, y0, kIconW, kIconH, fill);
    display.fillRect(ix + kIconW, y0 + 2, kTipW, kIconH - 4, fill);
    const int inner = kIconW - 2;
    const int fill_w = (inner * pct) / 100;
    if (fill_w > 0) {
        display.fillRect(ix + 1, y0 + 1, fill_w, kIconH - 2, fill);
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
