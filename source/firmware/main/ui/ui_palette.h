#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_THEME_CLASSIC = 0,
    UI_THEME_TRAVEL = 1,
    UI_THEME_COUNT,
} ui_theme_t;

typedef enum {
    UI_COLOR_BG = 0,
    UI_COLOR_SURFACE,
    UI_COLOR_SURFACE_ALT,
    UI_COLOR_BORDER,
    UI_COLOR_TEXT,
    UI_COLOR_TEXT_DIM,
    UI_COLOR_ACCENT,
    UI_COLOR_DANGER,
    UI_COLOR_ON_ACCENT,
    UI_COLOR_SUCCESS,
    UI_COLOR_COUNT,
} ui_color_role_t;

void ui_theme_set(ui_theme_t theme);
ui_theme_t ui_theme_get(void);
bool ui_theme_is_classic(void);
uint32_t ui_theme_color(ui_color_role_t role);
const char *ui_theme_name(ui_theme_t theme);

#ifdef __cplusplus
}
#endif

#define UI_BG          ui_theme_color(UI_COLOR_BG)
#define UI_SURFACE     ui_theme_color(UI_COLOR_SURFACE)
#define UI_SURFACE_ALT ui_theme_color(UI_COLOR_SURFACE_ALT)
#define UI_BORDER      ui_theme_color(UI_COLOR_BORDER)
#define UI_TEXT        ui_theme_color(UI_COLOR_TEXT)
#define UI_TEXT_DIM    ui_theme_color(UI_COLOR_TEXT_DIM)
#define UI_ACCENT      ui_theme_color(UI_COLOR_ACCENT)
#define UI_DANGER      ui_theme_color(UI_COLOR_DANGER)
#define UI_ON_ACCENT   ui_theme_color(UI_COLOR_ON_ACCENT)
#define UI_SUCCESS     ui_theme_color(UI_COLOR_SUCCESS)

#define UI_SKY        UI_BG
#define UI_SKY_DARK   UI_ACCENT
#define UI_INK        UI_ON_ACCENT
#define UI_PAPER      UI_SURFACE
#define UI_GRASS      UI_SUCCESS
#define UI_GRASS_DARK UI_SUCCESS
#define UI_AMBER      UI_ACCENT
#define UI_YELLOW     UI_ACCENT
#define UI_ORANGE     UI_ACCENT
#define UI_RED        UI_DANGER
#define UI_MUTED      UI_TEXT_DIM
#define UI_SUB        UI_TEXT_DIM
