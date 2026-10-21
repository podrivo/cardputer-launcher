#include "theme.hpp"

// RGB565 (same values as LovyanGFX TFT_*).
uint16_t theme_col_bg = 0x0000;   // black
uint16_t theme_col_fg = 0xFFFF;   // white
uint16_t theme_col_dim = 0x8410;  // gray
uint16_t theme_col_sel = 0x07E0;  // green
uint16_t theme_col_err = 0xF800;  // red

const char *theme_title = "Mini Launcher";
const char *theme_empty = "No apps installed.";
const char *theme_shortcuts =
    "< > Browse\n"
    "OK Open\n"
    "DEL Delete\n"
    "- + Brightness\n"
    "ESC Close";
const char *theme_kbd_error = "No TCA8418";
const char *theme_booting = "Starting...";
const char *theme_delete_hint = "DEL Cancel  OK Confirm";
const char *theme_deleting = "Deleting...";
const char *theme_delete_fail = "Delete failed";
