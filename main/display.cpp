#include "pch_display.hpp"

#include <cstdio>

#include "display.hpp"
#include "theme.hpp"

namespace {

M5GFX display;

int count_lines(const char *text)
{
    int n = 1;
    for (const char *p = text; *p; ++p) {
        if (*p == '\n') {
            ++n;
        }
    }
    return n;
}

void draw_multiline(const char *text, int x, int y, int stride, uint16_t color)
{
    display.setTextColor(color, theme_col_bg);
    display.setTextSize(1);
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

void draw_multiline_vcenter(const char *text, int x, uint16_t color)
{
    display.setTextSize(1);
    const int fh = display.fontHeight();
    const int stride = fh + 4;
    const int lines = count_lines(text);
    // Height is first glyph + gaps between lines — not lines * stride.
    const int block_h = fh + (lines - 1) * stride;
    const int y = (display.height() - block_h) / 2;
    draw_multiline(text, x, y, stride, color);
}

} // namespace

void display_init()
{
    display.init();
    display.setRotation(1);
}

void display_show_splash()
{
    display.fillScreen(theme_col_bg);
    display.setTextColor(theme_col_fg, theme_col_bg);
    display.setTextSize(2);
    const int tw = display.textWidth(theme_title);
    const int th = display.fontHeight();
    display.setCursor((display.width() - tw) / 2, (display.height() - th) / 2);
    display.print(theme_title);
}

void display_show_shortcuts()
{
    display.fillScreen(theme_col_bg);
    draw_multiline_vcenter(theme_shortcuts, 8, theme_col_dim);
}

void display_show_kbd_error()
{
    display.fillScreen(theme_col_bg);
    display.setTextColor(theme_col_err, theme_col_bg);
    display.setTextSize(1);
    display.setCursor(8, 60);
    display.print(theme_kbd_error);
}

void display_show_empty()
{
    display.fillScreen(theme_col_bg);
    draw_multiline_vcenter(theme_empty, 8, theme_col_dim);
}

void display_show_list(const launcher_app_view *apps, size_t count, size_t selected)
{
    display.fillScreen(theme_col_bg);

    if (count == 0) {
        display_show_empty();
        return;
    }

    // Show up to 6 rows centered on selection (full screen, no title).
    constexpr size_t kVisible = 6;
    size_t start = 0;
    if (count > kVisible && selected >= kVisible / 2) {
        start = selected - kVisible / 2;
        if (start + kVisible > count) {
            start = count - kVisible;
        }
    }

    display.setTextSize(1);
    for (size_t i = 0; i < kVisible && start + i < count; ++i) {
        const size_t idx = start + i;
        const int y = 8 + static_cast<int>(i) * 18;
        const bool on = idx == selected;
        display.setTextColor(on ? theme_col_sel : theme_col_fg, theme_col_bg);
        display.setCursor(8, y);
        display.printf("%s %s", on ? ">" : " ", apps[idx].name);
    }

    char footer[24];
    std::snprintf(footer, sizeof(footer), "%u / %u",
                  static_cast<unsigned>(selected + 1),
                  static_cast<unsigned>(count));
    display.setTextColor(theme_col_dim, theme_col_bg);
    display.setCursor(180, 120);
    display.print(footer);
}

void display_show_booting(const char *name)
{
    display.fillScreen(theme_col_bg);
    display.setTextColor(theme_col_fg, theme_col_bg);
    display.setTextSize(2);
    display.setCursor(8, 40);
    display.print(theme_booting);
    display.setTextSize(1);
    display.setCursor(8, 80);
    display.print(name ? name : "");
}
