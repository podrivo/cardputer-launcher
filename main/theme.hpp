#pragma once

#include <cstddef>
#include <cstdint>

extern uint16_t theme_col_bg;
extern uint16_t theme_col_fg;
extern uint16_t theme_col_dim;
extern uint16_t theme_col_sel;
extern uint16_t theme_col_err;

struct theme_shortcut {
    const char *keys;
    const char *label;
};

extern const char *theme_title;
extern const char *theme_empty;
extern const theme_shortcut theme_shortcuts[];
extern const size_t theme_shortcuts_count;
extern const char *theme_kbd_error;
extern const char *theme_booting;
extern const char *theme_delete_hint;
extern const char *theme_deleting;
extern const char *theme_delete_fail;
