#include "theme.hpp"

// RGB565 (same values as LovyanGFX TFT_*).
uint16_t theme_col_bg = 0x0000;   // black
uint16_t theme_col_fg = 0xFFFF;   // white
uint16_t theme_col_dim = 0x8410;  // gray
uint16_t theme_col_sel = 0x07E0;  // green
uint16_t theme_col_err = 0xF800;  // red

const char *theme_title = "Mini Launcher";
const char *theme_empty = "No apps - add via\ninstallers site";
const char *theme_shortcuts =
    "< > browse\n"
    "Enter open\n"
    "Esc close";
const char *theme_kbd_error = "No TCA8418";
const char *theme_booting = "Starting...";
