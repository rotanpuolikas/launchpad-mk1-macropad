/*
 * theme.c — UI appearance constants for launc-macro-gui
 *
 * Change any value here and recompile to restyle the whole app.
 * No other source file needs to be touched.
 */
#include "theme.h"

/* raygui.h provides GuiSetStyle / ColorToInt declarations.
 * RAYGUI_IMPLEMENTATION is defined in main_gui.c, which owns the impl. */
#include "raygui.h"

/* ── Font sizes (pixels) ─────────────────────────────────────────────────── */
int THEME_FONT_TITLE  = 20;   /* window title                       */
int THEME_FONT_UI     = 18;   /* raygui widget text, button-ID label */
int THEME_FONT_VALUE  = 18;   /* selected button ID                 */
int THEME_FONT_MID    = 14;   /* status-dot label, hint text        */
int THEME_FONT_LABEL  = 13;   /* section headers, field labels      */
int THEME_FONT_SMALL  = 12;   /* grid column index numbers          */

/* ── Window / background colors ──────────────────────────────────────────── */
Color THEME_BG         = {  20,  20,  26, 255 };
Color THEME_HEADER_BG  = {  28,  28,  34, 255 };
Color THEME_SBAR_BG    = {  16,  16,  20, 255 };

/* ── Text colors ─────────────────────────────────────────────────────────── */
Color THEME_TEXT_TITLE   = { 255, 255, 255, 255 };
Color THEME_TEXT_ACCENT  = { 100, 180, 255, 255 };
Color THEME_TEXT_SECTION = { 110, 110, 120, 255 };
Color THEME_TEXT_LABEL   = { 150, 150, 160, 255 };
Color THEME_TEXT_LABEL2  = { 130, 130, 140, 255 };
Color THEME_TEXT_DIM     = {  75,  75,  80, 255 };
Color THEME_TEXT_SBAR    = { 150, 150, 160, 255 };

/* ── Status-indicator dot colors ─────────────────────────────────────────── */
Color THEME_DOT_RUNNING  = {  80, 220,  80, 255 };
Color THEME_DOT_IDLE     = { 220, 220,  80, 255 };
Color THEME_DOT_OFFLINE  = { 200,  60,  60, 255 };

/* ── Launchpad button grid ───────────────────────────────────────────────── */
Color THEME_BTN_OFF        = {  45,  45,  50, 255 };
Color THEME_BTN_OFF_SEL    = {  60,  60,  65, 255 };
Color THEME_BTN_BORDER     = {  70,  70,  75, 255 };
Color THEME_BTN_BORDER_HOV = { 160, 160, 160, 255 };

/* ── Miscellaneous elements ──────────────────────────────────────────────── */
Color THEME_DIVIDER       = {  55,  55,  60, 255 };
Color THEME_SWATCH_BORDER = {  90,  90,  95, 255 };

/* ── raygui widget palette ───────────────────────────────────────────────── */
Color THEME_GUI_TEXT        = { 210, 210, 215, 255 };
Color THEME_GUI_BASE        = {  48,  48,  54, 255 };
Color THEME_GUI_BASE_HOV    = {  62,  62,  70, 255 };
Color THEME_GUI_BASE_PRESS  = {  38,  38,  44, 255 };
Color THEME_GUI_BORDER      = {  75,  75,  82, 255 };
Color THEME_GUI_BG          = {  28,  28,  34, 255 };
Color THEME_GUI_BTN_TEXT    = { 255, 255, 255, 255 };
Color THEME_GUI_BTN_BASE    = {  52,  52,  60, 255 };
Color THEME_GUI_BTN_BASE_HOV   = {  68,  68,  78, 255 };
Color THEME_GUI_BTN_BASE_PRESS = {  38,  78, 130, 255 };

/* ── Custom font ─────────────────────────────────────────────────────────── */
/* Set THEME_CUSTOM_FONT_PATH to a .ttf/.otf path to load a custom font for
 * all raygui widgets.  Leave as "" to use the built-in raylib font. */
char        THEME_CUSTOM_FONT_PATH[512] = "";  /* set to a .ttf/.otf path for a custom font */
Font        THEME_CUSTOM_FONT;
int         THEME_CUSTOM_FONT_LOADED = 0;

/* ── apply_theme() ───────────────────────────────────────────────────────── */
/* Call once after InitWindow(). Sets all raygui style entries from the
 * constants above so the rest of the app never hard-codes colour values. */
void apply_theme(void) {
    GuiSetStyle(DEFAULT, TEXT_SIZE,           THEME_FONT_UI);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL,   ColorToInt(THEME_GUI_TEXT));
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL,   ColorToInt(THEME_GUI_BASE));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED,  ColorToInt(THEME_GUI_BASE_HOV));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED,  ColorToInt(THEME_GUI_BASE_PRESS));
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, ColorToInt(THEME_GUI_BORDER));
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR,    ColorToInt(THEME_GUI_BG));

    GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL,   ColorToInt(THEME_GUI_BTN_TEXT));
    GuiSetStyle(BUTTON, BASE_COLOR_NORMAL,   ColorToInt(THEME_GUI_BTN_BASE));
    GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED,  ColorToInt(THEME_GUI_BTN_BASE_HOV));
    GuiSetStyle(BUTTON, BASE_COLOR_PRESSED,  ColorToInt(THEME_GUI_BTN_BASE_PRESS));

    /* Load custom font if a path was specified */
    if (THEME_CUSTOM_FONT_PATH[0] != '\0') {
        THEME_CUSTOM_FONT = LoadFont(THEME_CUSTOM_FONT_PATH);
        if (THEME_CUSTOM_FONT.texture.id > 0) {
            THEME_CUSTOM_FONT_LOADED = 1;
            GuiSetFont(THEME_CUSTOM_FONT);
            GuiSetStyle(DEFAULT, TEXT_SIZE, THEME_CUSTOM_FONT.baseSize);
        }
    }
}
