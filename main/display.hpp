#pragma once

#include <cstddef>

// Thin display API — keeps M5GFX out of other translation units.

struct launcher_app_view {
    const char *name;
};

void display_init();
int display_adjust_brightness(int delta_steps);
void display_show_splash();
void display_show_shortcuts();
void display_show_kbd_error();
void display_show_empty();
void display_show_list(const launcher_app_view *apps, size_t count, size_t selected);
void display_show_delete_confirm(const char *name);
void display_show_deleting();
void display_show_delete_fail();
void display_show_booting(const char *name);
