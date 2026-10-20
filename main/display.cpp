#include "pch_display.hpp"

#include <cstdio>

#include "display.hpp"
#include "theme.hpp"

namespace {

M5GFX display;

void draw_header()
{
    display.setTextColor(theme_col_fg, theme_col_bg);
    display.setTextSize(2);
    display.setCursor(8, 6);
    display.print(theme_title);
}

void draw_hint()
{
    display.setTextColor(theme_col_dim, theme_col_bg);
    display.setTextSize(1);
    display.setCursor(8, 120);
    display.print(theme_hint);
}

} // namespace

void display_init()
{
    display.init();
    display.setRotation(1);
}

void display_show_kbd_error()
{
    display.fillScreen(theme_col_bg);
    draw_header();
    display.setTextColor(theme_col_err, theme_col_bg);
    display.setTextSize(1);
    display.setCursor(8, 60);
    display.print(theme_kbd_error);
}

void display_show_empty()
{
    display.fillScreen(theme_col_bg);
    draw_header();
    display.setTextColor(theme_col_dim, theme_col_bg);
    display.setTextSize(1);
    display.setCursor(8, 48);
    display.print(theme_empty);
    draw_hint();
}

void display_show_list(const launcher_app_view *apps, size_t count, size_t selected)
{
    display.fillScreen(theme_col_bg);
    draw_header();

    if (count == 0) {
        display_show_empty();
        return;
    }

    // Show up to 4 rows centered on selection.
    constexpr size_t kVisible = 4;
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
        const int y = 36 + static_cast<int>(i) * 18;
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
    draw_hint();
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
