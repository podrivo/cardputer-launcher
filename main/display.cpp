#include "pch_display.hpp"

#include <cstdio>

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
