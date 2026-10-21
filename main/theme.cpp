#include "theme.hpp"

#include "esp_random.h"

// RGB565 (same values as LovyanGFX TFT_*).
uint16_t theme_col_bg = 0x0000;   // black
uint16_t theme_col_fg = 0xFFFF;   // white
uint16_t theme_col_dim = 0x8410;  // gray
uint16_t theme_col_sel = 0x07E0;  // green
uint16_t theme_col_err = 0xF800;  // red

namespace {

struct Palette {
    uint16_t bg;
    uint16_t fg;
    uint16_t dim;
    uint16_t sel;
    uint16_t err;
};

// Curated high-contrast sets for the small TFT.
constexpr Palette kPalettes[] = {
    {0x0000, 0xFFFF, 0x8410, 0x07E0, 0xF800}, // classic
    {0x0000, 0xFFE0, 0xC618, 0xFDA0, 0xF800}, // amber
    {0x0000, 0x07E0, 0x03E0, 0xAFE5, 0xF800}, // matrix
    {0x018C, 0xEFFB, 0x7BCF, 0x07FF, 0xFBAE}, // ocean
    {0x1082, 0xEF5D, 0x8410, 0xF81F, 0xF800}, // violet
    {0x2004, 0xFEDC, 0xA514, 0xFD20, 0xF800}, // sunset
    {0x2124, 0xEFDF, 0x9CD3, 0x5D1C, 0xFBAE}, // ice
    {0xEF5D, 0x18C3, 0x8410, 0x001F, 0xF800}, // paper
    {0x0000, 0x07FF, 0x0410, 0xFFE0, 0xF81F}, // cyber
    {0x0821, 0xF7BE, 0x6B4D, 0x07E0, 0xF800}, // forest
    {0x0000, 0xF81F, 0x780F, 0xFFE0, 0x07FF}, // neon
    {0x10A2, 0xFFFF, 0x7BCF, 0x07E0, 0xF800}, // slate
    {0x4000, 0xFFE0, 0xC300, 0xFDA0, 0xFFFF}, // blood
    {0xFFFF, 0x0000, 0x8410, 0x001F, 0xF800}, // invert
    {0x000A, 0x07FF, 0x03EF, 0xFFE0, 0xF81F}, // deep sea
    {0x2104, 0xFCA8, 0x9A85, 0x07E0, 0xF800}, // cocoa
    {0x0000, 0xFD20, 0xC300, 0xFFE0, 0xF81F}, // ember
    {0x39C7, 0xFFFF, 0xBDF7, 0x07FF, 0xF800}, // mist
    {0x780F, 0xF7BE, 0xC618, 0x07E0, 0xFDA0}, // grape
    {0x0320, 0xEFE0, 0x84C0, 0xFFE0, 0xF800}, // moss
};

constexpr size_t kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);
size_t palette_index = 0;

void apply_palette(size_t index)
{
    palette_index = index % kPaletteCount;
    const Palette &p = kPalettes[palette_index];
    theme_col_bg = p.bg;
    theme_col_fg = p.fg;
    theme_col_dim = p.dim;
    theme_col_sel = p.sel;
    theme_col_err = p.err;
}

} // namespace

void theme_pick_palette()
{
    apply_palette(esp_random() % kPaletteCount);
}

void theme_cycle_palette(int delta)
{
    const int n = static_cast<int>(kPaletteCount);
    int next = static_cast<int>(palette_index) + delta;
    next %= n;
    if (next < 0) {
        next += n;
    }
    apply_palette(static_cast<size_t>(next));
}

const char *theme_title = "Mini Launcher";
const char *theme_empty = "No apps installed.";
const theme_shortcut theme_shortcuts[] = {
    {"\x18 \x19", "Browse"},
    {"- +", "Brightness"},
    {"[ ]", "Colors"},
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
