#include "theme.hpp"

// RGB565 (same values as LovyanGFX TFT_*).
uint16_t theme_col_bg = 0x0000;   // black
uint16_t theme_col_fg = 0xFFFF;   // white
uint16_t theme_col_dim = 0x8410;  // gray
uint16_t theme_col_sel = 0x07E0;  // green
uint16_t theme_col_err = 0xF800;  // red

const char *theme_title = "Mini Launcher";
const char *theme_empty = "No apps installed.";
const theme_shortcut theme_shortcuts[] = {
    {"< >", "Browse"},
    {"- +", "Brightness"},
    {"OK", "Open"},
    {"ESC", "Close"},
    {"DEL", "Delete"},
};
const size_t theme_shortcuts_count =
    sizeof(theme_shortcuts) / sizeof(theme_shortcuts[0]);
const char *theme_kbd_error = "No TCA8418";
const char *theme_booting = "Starting...";
const char *theme_delete_hint = "DEL Cancel  OK Confirm";
const char *theme_deleting = "Deleting...";
const char *theme_delete_fail = "Delete failed";
