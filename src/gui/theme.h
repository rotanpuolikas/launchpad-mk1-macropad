#pragma once
/*
 * theme.h — UI appearance constants for launc-macro-gui
 *
 * Edit theme.c to change any font size, color, or widget palette entry.
 * All values are applied once at startup via apply_theme().
 */
#include "raylib.h"

/* ── Font sizes (pixels) ─────────────────────────────────────────────────── */
extern int THEME_FONT_TITLE;    /* window title ("launc-macro") */
extern int THEME_FONT_UI;       /* raygui widget text size      */
extern int THEME_FONT_VALUE;    /* selected button ID label     */
extern int THEME_FONT_MID;      /* status-dot label, hint text  */
extern int THEME_FONT_LABEL;    /* section headers, field labels, status bar */
extern int THEME_FONT_SMALL;    /* grid column-index numbers    */

/* ── Window / background colors ──────────────────────────────────────────── */
extern Color THEME_BG;           /* main window background  */
extern Color THEME_HEADER_BG;    /* header strip background */
extern Color THEME_SBAR_BG;      /* status bar background   */

/* ── Text colors ─────────────────────────────────────────────────────────── */
extern Color THEME_TEXT_TITLE;   /* "launc-macro" word              */
extern Color THEME_TEXT_ACCENT;  /* "GUI" accent word               */
extern Color THEME_TEXT_SECTION; /* CONFIG / GIF SETTINGS headings  */
extern Color THEME_TEXT_LABEL;   /* primary field labels            */
extern Color THEME_TEXT_LABEL2;  /* secondary field labels          */
extern Color THEME_TEXT_DIM;     /* placeholder / hint text         */
extern Color THEME_TEXT_SBAR;    /* status bar message              */

/* ── Status-indicator dot colors ─────────────────────────────────────────── */
extern Color THEME_DOT_RUNNING;
extern Color THEME_DOT_IDLE;
extern Color THEME_DOT_OFFLINE;

/* ── Launchpad button grid ───────────────────────────────────────────────── */
extern Color THEME_BTN_OFF;        /* unlit button fill        */
extern Color THEME_BTN_OFF_SEL;    /* unlit selected fill      */
extern Color THEME_BTN_BORDER;     /* normal button border     */
extern Color THEME_BTN_BORDER_HOV; /* hovered button border    */
/* selected border = WHITE (raylib constant) */

/* ── Miscellaneous elements ──────────────────────────────────────────────── */
extern Color THEME_DIVIDER;       /* horizontal divider lines    */
extern Color THEME_SWATCH_BORDER; /* colour-swatch outline       */

/* ── raygui widget palette ───────────────────────────────────────────────── */
extern Color THEME_GUI_TEXT;
extern Color THEME_GUI_BASE;
extern Color THEME_GUI_BASE_HOV;
extern Color THEME_GUI_BASE_PRESS;
extern Color THEME_GUI_BORDER;
extern Color THEME_GUI_BG;
extern Color THEME_GUI_BTN_TEXT;
extern Color THEME_GUI_BTN_BASE;
extern Color THEME_GUI_BTN_BASE_HOV;
extern Color THEME_GUI_BTN_BASE_PRESS;

/* ── Custom font ─────────────────────────────────────────────────────────── */
/* Set THEME_CUSTOM_FONT_PATH in theme.c to a .ttf/.otf file path to load a
 * custom font for all raygui widgets.  Leave it as "" for the built-in font.
 * After apply_theme() returns, THEME_CUSTOM_FONT_LOADED is 1 if the font was
 * loaded successfully, and THEME_CUSTOM_FONT holds the Font handle. */
extern char        THEME_CUSTOM_FONT_PATH[512];
extern Font        THEME_CUSTOM_FONT;
extern int         THEME_CUSTOM_FONT_LOADED;

/* Apply all raygui style entries — call once after InitWindow() */
void apply_theme(void);
