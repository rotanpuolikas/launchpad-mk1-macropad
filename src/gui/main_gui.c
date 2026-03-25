/*
 * launc-macro-gui — raylib + raygui front-end for launc-macro
 *
 * Window layout (fixed 820×700):
 *
 *  ┌──────────────────────────────────────────────────────────────────┐
 *  │ header: title / connect / start / [Opts] / status indicator      │ 50px
 *  ├─────────────────────────────────┬────────────────────────────────┤
 *  │                                 │  Button Editor                  │
 *  │  Launchpad 9×9 visual grid      │    color dropdowns + swatches  │
 *  │  (click to select button)       │    action type + value         │
 *  │                                 │  Config  path [...]            │
 *  ├─────────────────────────────────┴────────────────────────────────┤
 *  │ GIF SETTINGS   File: [path] [...]  Load | Clear  FPS  ColorMode  │
 *  ├──────────────────────────────────────────────────────────────────┤
 *  │ status bar                                                        │ 25px
 *  └──────────────────────────────────────────────────────────────────┘
 *
 *  Floating overlays (drawn last, over everything):
 *    • gif_fb_visible  — file browser popup anchored to GIF panel
 *    • cfg_fb_visible  — file browser popup anchored to CONFIG section
 *    • options_open    — options/theme editor panel (top-right)
 *
 * raygui dropdown note: GuiDropdownBox must be drawn last so it appears
 * on top of other controls. Only one dropdown may be open at a time.
 */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifndef _WIN32
#  include <dirent.h>
#  include <sys/stat.h>
#endif

#include "raylib.h"
#define RAYGUI_TEXTEDIT_CURSOR_BLINK_FRAMES 40
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "app.h"
#include "../colours.h"
#include "theme.h"

/* ── Layout constants ───────────────────────────────────────────────────────── */

#define WIN_W    820
#define WIN_H    700
#define HDR_H     50
#define SBAR_H    25
#define PAD       12

/* Launchpad grid */
#define BTN_SZ    38
#define BTN_GAP    4
#define BTN_STEP  (BTN_SZ + BTN_GAP)
#define GRID_X    PAD
#define GRID_Y    (HDR_H + PAD + 16)   /* +16 for column-index labels above */

/* Right info panel */
#define RPANEL_X  (GRID_X + 9 * BTN_STEP + PAD * 2)
#define RPANEL_W  (WIN_W - RPANEL_X - PAD)
#define RPANEL_Y  GRID_Y

/* GIF panel (below the launchpad grid) — compact (no inline browser) */
#define GIF_PANEL_Y  (GRID_Y + 9 * BTN_STEP + PAD)
#define GIF_PANEL_H  (WIN_H - SBAR_H - GIF_PANEL_Y - PAD)

/* Options overlay panel */
#define OPTS_W    340
#define OPTS_H    420
#define OPTS_X    ((WIN_W - OPTS_W) / 2)
#define OPTS_Y    (HDR_H + 6)

/* Browse-button width (the small [...] next to path inputs) */
#define BROWSE_BTN_W  26

/* Dropdown identifiers */
#define DROPDOWN_NONE          0
#define DROPDOWN_COLOR         1
#define DROPDOWN_COLOR_PRESSED 2
#define DROPDOWN_ACTION_TYPE   3
#define DROPDOWN_GIF_MODE      4

/* ── File browser ───────────────────────────────────────────────────────────── */

#define FB_MAX_ENTRIES  256
#define FB_NAME_LEN     256
#define FB_ROW_H         16

typedef struct {
    char        cwd[512];
    char        names[FB_MAX_ENTRIES][FB_NAME_LEN];
    int         is_dir[FB_MAX_ENTRIES];
    int         count;
    int         scroll;
    int         selected;
    const char *filter;   /* e.g. ".gif", NULL = all */
} FileBrowser;

static int fb_strcmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static void fb_refresh(FileBrowser *fb) {
    char dirs [FB_MAX_ENTRIES][FB_NAME_LEN];
    char files[FB_MAX_ENTRIES][FB_NAME_LEN];
    int  ndirs = 0, nfiles = 0;

#ifdef _WIN32
    char pattern[528];
    snprintf(pattern, sizeof(pattern), "%s\\*", fb->cwd);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            int isd = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (isd) {
                if (ndirs < FB_MAX_ENTRIES - 1)
                    strncpy(dirs[ndirs++], fd.cFileName, FB_NAME_LEN - 1);
            } else if (!fb->filter) {
                if (nfiles < FB_MAX_ENTRIES - 1)
                    strncpy(files[nfiles++], fd.cFileName, FB_NAME_LEN - 1);
            } else {
                size_t nl = strlen(fd.cFileName), fl = strlen(fb->filter);
                if (nl > fl && _stricmp(fd.cFileName + nl - fl, fb->filter) == 0
                    && nfiles < FB_MAX_ENTRIES - 1)
                    strncpy(files[nfiles++], fd.cFileName, FB_NAME_LEN - 1);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(fb->cwd);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", fb->cwd, e->d_name);
            struct stat st;
            int isd = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
            if (isd) {
                if (ndirs < FB_MAX_ENTRIES - 1)
                    strncpy(dirs[ndirs++], e->d_name, FB_NAME_LEN - 1);
            } else if (!fb->filter) {
                if (nfiles < FB_MAX_ENTRIES - 1)
                    strncpy(files[nfiles++], e->d_name, FB_NAME_LEN - 1);
            } else {
                size_t nl = strlen(e->d_name), fl = strlen(fb->filter);
                if (nl > fl && strcasecmp(e->d_name + nl - fl, fb->filter) == 0
                    && nfiles < FB_MAX_ENTRIES - 1)
                    strncpy(files[nfiles++], e->d_name, FB_NAME_LEN - 1);
            }
        }
        closedir(d);
    }
#endif
    qsort(dirs,  ndirs,  FB_NAME_LEN, fb_strcmp);
    qsort(files, nfiles, FB_NAME_LEN, fb_strcmp);

    fb->count = 0;
    strncpy(fb->names[fb->count], "..", FB_NAME_LEN - 1);
    fb->is_dir[fb->count++] = 1;
    for (int i = 0; i < ndirs  && fb->count < FB_MAX_ENTRIES; i++) {
        strncpy(fb->names[fb->count], dirs[i],  FB_NAME_LEN - 1);
        fb->is_dir[fb->count++] = 1;
    }
    for (int i = 0; i < nfiles && fb->count < FB_MAX_ENTRIES; i++) {
        strncpy(fb->names[fb->count], files[i], FB_NAME_LEN - 1);
        fb->is_dir[fb->count++] = 0;
    }
    fb->scroll   = 0;
    fb->selected = -1;
}

static void fb_navigate_up(FileBrowser *fb) {
#ifdef _WIN32
    char *sep = strrchr(fb->cwd, '\\');
    if (sep && sep != fb->cwd) { *sep = '\0'; fb_refresh(fb); }
#else
    char *sep = strrchr(fb->cwd, '/');
    if (!sep) return;
    if (sep == fb->cwd) { if (fb->cwd[1]) { fb->cwd[1] = '\0'; fb_refresh(fb); } }
    else               { *sep = '\0'; fb_refresh(fb); }
#endif
}

static void fb_init(FileBrowser *fb, const char *start_dir, const char *filter) {
    memset(fb, 0, sizeof(*fb));
    fb->selected = -1;
    fb->filter   = filter;
    strncpy(fb->cwd, start_dir, sizeof(fb->cwd) - 1);
    fb_refresh(fb);
}

static void get_home_dir(char *buf, size_t sz) {
#ifdef _WIN32
    const char *h = getenv("USERPROFILE");
    strncpy(buf, h ? h : "C:\\", sz - 1);
#else
    const char *h = getenv("HOME");
    strncpy(buf, h ? h : "/", sz - 1);
#endif
}

/* Draw a file browser.  Returns 1 when a file is selected (path written to out). */
static int fb_draw(FileBrowser *fb, int x, int y, int w, int h,
                   bool locked, char *out, int out_size) {
    int     result  = 0;
    Vector2 mouse   = GetMousePosition();
    bool    clicked = !locked && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    DrawRectangle(x, y, w, h, (Color){ 22, 22, 28, 255 });
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,(float)h},
                         1, THEME_BTN_BORDER);

    /* Current directory (tail-truncated) */
    int  cwd_len   = (int)strlen(fb->cwd);
    int  max_chars = (w - 8) / 7;
    char cwd_disp[FB_NAME_LEN];
    if (cwd_len > max_chars && max_chars > 3)
        snprintf(cwd_disp, sizeof(cwd_disp), "...%s", fb->cwd + cwd_len - (max_chars - 3));
    else
        strncpy(cwd_disp, fb->cwd, sizeof(cwd_disp) - 1);
    DrawText(cwd_disp, x + 4, y + 2, THEME_FONT_SMALL, THEME_TEXT_LABEL2);

    int list_y   = y + FB_ROW_H + 2;
    int list_h   = h - FB_ROW_H - 2;
    int vis_rows = list_h / FB_ROW_H;

    /* Mouse-wheel scroll */
    if (!locked && CheckCollisionPointRec(mouse, (Rectangle){(float)x,(float)list_y,(float)w,(float)list_h})) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            fb->scroll -= (int)wheel;
            if (fb->scroll < 0) fb->scroll = 0;
            int max_s = fb->count - vis_rows;
            if (max_s < 0) max_s = 0;
            if (fb->scroll > max_s) fb->scroll = max_s;
        }
    }

    BeginScissorMode(x, list_y, w, list_h);
    for (int i = 0; i < vis_rows; i++) {
        int idx = fb->scroll + i;
        if (idx >= fb->count) break;
        int  ry       = list_y + i * FB_ROW_H;
        bool is_hover = !locked && mouse.x >= x && mouse.x < x + w
                        && mouse.y >= ry && mouse.y < ry + FB_ROW_H;
        bool is_sel   = (fb->selected == idx);
        Color row_bg  = (i % 2 == 0) ? (Color){ 26,26,32,255 } : (Color){ 22,22,28,255 };
        if (is_sel)        row_bg = (Color){ 38, 78,130,220 };
        else if (is_hover) row_bg = (Color){ 48, 48, 60,255 };
        DrawRectangle(x + 1, ry, w - 2, FB_ROW_H, row_bg);
        Color tc = fb->is_dir[idx] ? (Color){ 100,180,255,255 } : THEME_TEXT_LABEL;
        char disp[36];
        if (fb->is_dir[idx]) snprintf(disp, sizeof(disp), "/ %.28s", fb->names[idx]);
        else                 snprintf(disp, sizeof(disp), "%.30s",   fb->names[idx]);
        DrawText(disp, x + 4, ry + 2, THEME_FONT_SMALL, tc);
        if (clicked && is_hover) {
            if (fb->is_dir[idx]) {
                if (!strcmp(fb->names[idx], "..")) {
                    fb_navigate_up(fb);
                } else {
                    char np[512];
#ifdef _WIN32
                    snprintf(np, sizeof(np), "%s\\%s", fb->cwd, fb->names[idx]);
#else
                    snprintf(np, sizeof(np), "%s/%s",  fb->cwd, fb->names[idx]);
#endif
                    strncpy(fb->cwd, np, sizeof(fb->cwd) - 1);
                    fb_refresh(fb);
                }
            } else {
                fb->selected = idx;
                if (out) {
#ifdef _WIN32
                    snprintf(out, out_size, "%s\\%s", fb->cwd, fb->names[idx]);
#else
                    snprintf(out, out_size, "%s/%s",  fb->cwd, fb->names[idx]);
#endif
                }
                result = 1;
            }
        }
    }
    EndScissorMode();

    /* Scrollbar */
    if (fb->count > vis_rows) {
        float sb_h = (float)list_h * vis_rows / fb->count;
        float sb_y = (float)list_y + (float)list_h * fb->scroll / fb->count;
        DrawRectangle(x + w - 4, (int)sb_y, 3, (int)sb_h, THEME_BTN_BORDER_HOV);
    }
    return result;
}

/* ── GUI options (font + colours, persisted to gui.conf) ───────────────────── */

typedef struct {
    char font_path[512];
    int  font_size;            /* 0 = use default */
    char col_bg[8];            /* 6-char hex RRGGBB, no '#' */
    char col_header[8];
    char col_accent[8];
    char col_btn_off[8];
} GuiOptions;

static Color parse_hex_color(const char *hex, Color fallback) {
    unsigned int r = 0, g = 0, b = 0;
    if (strlen(hex) == 6 && sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3)
        return (Color){ (uint8_t)r, (uint8_t)g, (uint8_t)b, 255 };
    return fallback;
}

static void color_to_hex(Color c, char out[8]) {
    snprintf(out, 8, "%02x%02x%02x", c.r, c.g, c.b);
}

static void opts_defaults(GuiOptions *o) {
    memset(o, 0, sizeof(*o));
    o->font_size = THEME_FONT_UI;
    color_to_hex(THEME_BG,          o->col_bg);
    color_to_hex(THEME_HEADER_BG,   o->col_header);
    color_to_hex(THEME_TEXT_ACCENT, o->col_accent);
    color_to_hex(THEME_BTN_OFF,     o->col_btn_off);
}

/* Apply GuiOptions to live theme variables */
static void opts_apply(GuiOptions *o) {
    THEME_BG          = parse_hex_color(o->col_bg,     THEME_BG);
    THEME_HEADER_BG   = parse_hex_color(o->col_header, THEME_HEADER_BG);
    THEME_TEXT_ACCENT = parse_hex_color(o->col_accent, THEME_TEXT_ACCENT);
    THEME_BTN_OFF     = parse_hex_color(o->col_btn_off,THEME_BTN_OFF);

    if (o->font_size > 0 && o->font_size != THEME_FONT_UI) {
        THEME_FONT_UI    = o->font_size;
        THEME_FONT_VALUE = o->font_size;
        GuiSetStyle(DEFAULT, TEXT_SIZE, THEME_FONT_UI);
    }

    if (o->font_path[0] != '\0') {
        Font f = LoadFont(o->font_path);
        if (f.texture.id > 0) {
            if (THEME_CUSTOM_FONT_LOADED) UnloadFont(THEME_CUSTOM_FONT);
            THEME_CUSTOM_FONT        = f;
            THEME_CUSTOM_FONT_LOADED = 1;
            strncpy(THEME_CUSTOM_FONT_PATH, o->font_path,
                    sizeof(THEME_CUSTOM_FONT_PATH) - 1);
            GuiSetFont(THEME_CUSTOM_FONT);
            GuiSetStyle(DEFAULT, TEXT_SIZE,
                        o->font_size > 0 ? o->font_size : f.baseSize);
        }
    }
}

/* Derive gui.conf path from the launchpad config path */
static void gui_opts_path(const char *cfg_path, char *buf, size_t sz) {
    strncpy(buf, cfg_path, sz - 1);
    char *sep = strrchr(buf, '/');
#ifdef _WIN32
    char *sep2 = strrchr(buf, '\\');
    if (sep2 > sep) sep = sep2;
#endif
    if (sep) { *(sep + 1) = '\0'; strncat(buf, "gui.conf", sz - strlen(buf) - 1); }
    else     { strncpy(buf, "gui.conf", sz - 1); }
}

static void opts_save(const GuiOptions *o, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "font_path    = %s\n", o->font_path);
    fprintf(f, "font_size    = %d\n", o->font_size);
    fprintf(f, "col_bg       = %s\n", o->col_bg);
    fprintf(f, "col_header   = %s\n", o->col_header);
    fprintf(f, "col_accent   = %s\n", o->col_accent);
    fprintf(f, "col_btn_off  = %s\n", o->col_btn_off);
    fclose(f);
}

static void opts_load(GuiOptions *o, const char *path) {
    opts_defaults(o);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[640];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = line, *v = eq + 1;
        while (isspace((unsigned char)*k)) k++;
        while (isspace((unsigned char)*v)) v++;
        char *e = k + strlen(k) - 1;
        while (e > k && isspace((unsigned char)*e)) *e-- = '\0';
        e = v + strlen(v) - 1;
        while (e > v && isspace((unsigned char)*e)) *e-- = '\0';

        if (!strcmp(k, "font_path"))   strncpy(o->font_path,  v, sizeof(o->font_path)  - 1);
        else if (!strcmp(k, "font_size"))   o->font_size = atoi(v);
        else if (!strcmp(k, "col_bg"))      strncpy(o->col_bg,     v, 7);
        else if (!strcmp(k, "col_header"))  strncpy(o->col_header, v, 7);
        else if (!strcmp(k, "col_accent"))  strncpy(o->col_accent, v, 7);
        else if (!strcmp(k, "col_btn_off")) strncpy(o->col_btn_off,v, 7);
    }
    fclose(f);
}

/* ── Palette helpers ────────────────────────────────────────────────────────── */

static const char *COLOUR_DROPDOWN =
    "black;red_low;red_med;red_max;"
    "green_low;green_med;green_max;"
    "yellow_low;yellow_med;yellow_max";

static const char *ACTION_TYPE_DROPDOWN = "none;key;string;media;app;terminal";

static int vel_to_palette_idx(uint8_t vel) {
    for (int i = 0; i < PALETTE_COUNT; i++)
        if (PALETTE[i].vel == vel) return i;
    return 0;
}

/* ── Color helpers ──────────────────────────────────────────────────────────── */

static Color vel_to_color(uint8_t vel) {
    uint8_t r, g, b;
    vel_to_rgb_bytes(vel, &r, &g, &b);
    if (r == 0 && g == 0) return THEME_BTN_OFF;
    return (Color){ r, g, b, 255 };
}

static Color vel_to_color_bright(uint8_t vel) {
    uint8_t r, g, b;
    vel_to_rgb_bytes(vel, &r, &g, &b);
    if (r == 0 && g == 0) return THEME_BTN_OFF_SEL;
    int rr = (int)r + 40; if (rr > 255) rr = 255;
    int gg = (int)g + 40; if (gg > 255) gg = 255;
    return (Color){ (uint8_t)rr, (uint8_t)gg, 0, 255 };
}

/* ── Launchpad visual grid ──────────────────────────────────────────────────── */

static void draw_launchpad(AppState *app, int *sel, bool locked) {
    uint8_t leds[BUTTON_COUNT];
    pthread_mutex_lock(&app->mutex);
    memcpy(leds, app->led_state, sizeof(leds));
    pthread_mutex_unlock(&app->mutex);

    Vector2 mouse   = GetMousePosition();
    bool    clicked = !locked && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    for (int row = 0; row < 9; row++) {
        for (int col = 0; col < 9; col++) {
            if (row == 0 && col == 8) continue; /* does not exist on Launchpad */

            int idx;
            if (row == 0)      idx = col;
            else if (col == 8) idx = 73 + (row - 1);
            else               idx = 9  + (row - 1) * 8 + col;

            Rectangle rect = {
                (float)(GRID_X + col * BTN_STEP),
                (float)(GRID_Y + row * BTN_STEP),
                BTN_SZ, BTN_SZ
            };
            bool  is_circle = (row == 0 || col == 8);
            float cx = rect.x + BTN_SZ * 0.5f;
            float cy = rect.y + BTN_SZ * 0.5f;
            float cr = BTN_SZ * 0.5f - 2.0f;

            bool is_sel   = (*sel == idx);
            bool is_hover = !locked &&
                (is_circle ? CheckCollisionPointCircle(mouse, (Vector2){cx,cy}, cr)
                           : CheckCollisionPointRec(mouse, rect));

            Color fill   = is_sel ? vel_to_color_bright(leds[idx])
                                  : vel_to_color(leds[idx]);
            Color border = is_sel   ? WHITE
                         : is_hover ? THEME_BTN_BORDER_HOV
                                    : THEME_BTN_BORDER;

            if (is_circle) {
                DrawCircleV((Vector2){cx, cy}, cr, fill);
                DrawCircleLinesV((Vector2){cx, cy}, cr, border);
                if (is_sel) DrawCircleLinesV((Vector2){cx, cy}, cr - 1.5f, border);
            } else {
                DrawRectangleRec(rect, fill);
                DrawRectangleLinesEx(rect, is_sel ? 2.0f : 1.0f, border);
            }
            if (clicked && is_hover) *sel = idx;
        }
    }

    /* Column labels 1..8 above the grid */
    for (int c = 0; c < 8; c++) {
        DrawText(TextFormat("%d", c + 1),
                 GRID_X + c * BTN_STEP + BTN_SZ / 2 - 3,
                 GRID_Y - THEME_FONT_SMALL - 4,
                 THEME_FONT_SMALL, THEME_BTN_BORDER);
    }
    /* Row labels A..H to the right of the side column */
    for (int row = 1; row <= 8; row++) {
        char letter[2] = { (char)('A' + row - 1), '\0' };
        DrawText(letter,
                 GRID_X + 9 * BTN_STEP + 4,
                 GRID_Y + row * BTN_STEP + BTN_SZ / 2 - THEME_FONT_SMALL / 2,
                 THEME_FONT_SMALL, THEME_BTN_BORDER);
    }
}

/* ── Header ─────────────────────────────────────────────────────────────────── */

/* forward-declare so draw_header can toggle ps->options_open */
typedef struct PanelState PanelState;

static void draw_header(AppState *app, PanelState *ps);

/* ── Panel state ─────────────────────────────────────────────────────────────── */

struct PanelState {
    /* Button editor */
    int  color_active;
    int  color_pressed_active;
    int  action_type_active;
    char action_value[MAX_ACTION_LEN];
    int  action_value_edit;
    int  gif_overlay_val;

    /* Config */
    char config_path_buf[512];
    int  config_path_edit;

    /* GIF settings */
    char gif_path[512];
    int  gif_path_edit;
    char fps_buf[16];
    int  fps_edit;
    int  gif_mode_active;

    /* Dropdowns */
    int  open_dropdown;
    int  last_sel;
    Rectangle color_rect;
    Rectangle color_pressed_rect;
    Rectangle action_type_rect;
    Rectangle gif_mode_rect;

    /* File browsers (popup overlays) */
    FileBrowser gif_fb;
    FileBrowser cfg_fb;
    int         gif_fb_visible;   /* GIF browse popup open */
    int         cfg_fb_visible;   /* Config browse popup open */

    /* Options panel */
    int        options_open;
    GuiOptions opts;
    FileBrowser font_fb;           /* font browse popup inside options */
    int         font_fb_visible;
    /* edit states for options inputs */
    int  opts_fp_edit;             /* font path textbox */
    int  opts_fs_edit;             /* font size textbox */
    int  opts_bg_edit;
    int  opts_hdr_edit;
    int  opts_acc_edit;
    int  opts_btn_edit;
    char opts_fs_buf[8];           /* font-size string */
};

/* ── Action string helpers ──────────────────────────────────────────────────── */

static void parse_action(const char *action, int *type_out,
                         char *val_out, int val_size) {
    val_out[val_size - 1] = '\0';
    if (!action || action[0] == '\0') {
        *type_out = 0; val_out[0] = '\0';
    } else if (!strncmp(action, "key:",      4)) { *type_out = 1; strncpy(val_out, action + 4, val_size-1); }
    else if (!strncmp(action, "string:",    7)) { *type_out = 2; strncpy(val_out, action + 7, val_size-1); }
    else if (!strncmp(action, "media:",     6)) { *type_out = 3; strncpy(val_out, action + 6, val_size-1); }
    else if (!strncmp(action, "app:",       4)) { *type_out = 4; strncpy(val_out, action + 4, val_size-1); }
    else if (!strncmp(action, "terminal:",  9)) { *type_out = 5; strncpy(val_out, action + 9, val_size-1); }
    else { *type_out = 4; snprintf(val_out, val_size, "%s", action); }
}

static void assemble_action(char *out, int size, int type_active, const char *value) {
    static const char *prefixes[] = { "", "key:", "string:", "media:", "app:", "terminal:" };
    if (type_active == 0 || value[0] == '\0') out[0] = '\0';
    else snprintf(out, size, "%s%s", prefixes[type_active], value);
}

/* ── Right panel ─────────────────────────────────────────────────────────────── */

static void draw_right_panel(AppState *app, int sel, PanelState *ps) {
    int x = RPANEL_X;
    int y = RPANEL_Y;
    int w = RPANEL_W;

    bool any_locked = (ps->open_dropdown != DROPDOWN_NONE)
                   || ps->gif_fb_visible || ps->cfg_fb_visible || ps->options_open;

    /* Refresh editor state on selection change */
    if (sel != ps->last_sel) {
        ps->last_sel = sel;
        if (sel >= 0) {
            const char *id = button_index_to_id(sel);
            const ButtonCfg *btn = config_find_button(&app->config, id);
            if (btn) {
                ps->color_active         = vel_to_palette_idx(btn->color);
                ps->color_pressed_active = vel_to_palette_idx(btn->color_pressed);
                parse_action(btn->action, &ps->action_type_active,
                             ps->action_value, sizeof(ps->action_value));
                ps->gif_overlay_val = btn->gif_overlay;
            } else {
                ps->color_active         = vel_to_palette_idx(app->config.default_color);
                ps->color_pressed_active = vel_to_palette_idx(app->config.default_color_pressed);
                ps->action_type_active   = 0;
                ps->action_value[0]      = '\0';
                ps->gif_overlay_val      = 0;
            }
        }
    }

    /* ── BUTTON EDITOR ── */
    DrawText("BUTTON EDITOR", x, y, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    y += 20;

    if (sel >= 0) {
        const char *id = button_index_to_id(sel);
        DrawText(id, x, y, THEME_FONT_UI, WHITE);
        y += 26;

        DrawText("Color (idle):", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL);
        y += 18;
        DrawRectangle(x, y, 20, 24, vel_to_color(PALETTE[ps->color_active].vel));
        DrawRectangleLinesEx((Rectangle){(float)x,(float)y,20,24}, 1, THEME_SWATCH_BORDER);
        ps->color_rect = (Rectangle){ (float)(x+24),(float)y,(float)(w-24),24 };
        y += 30;

        DrawText("Color (pressed):", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL);
        y += 18;
        DrawRectangle(x, y, 20, 24, vel_to_color(PALETTE[ps->color_pressed_active].vel));
        DrawRectangleLinesEx((Rectangle){(float)x,(float)y,20,24}, 1, THEME_SWATCH_BORDER);
        ps->color_pressed_rect = (Rectangle){ (float)(x+24),(float)y,(float)(w-24),24 };
        y += 30;

        DrawText("Action type:", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL);
        y += 18;
        ps->action_type_rect = (Rectangle){ (float)x,(float)y,(float)w,24 };
        y += 30;

        DrawText("Action value:", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL);
        y += 18;
        if (any_locked) GuiSetState(STATE_DISABLED);
        if (GuiTextBox((Rectangle){(float)x,(float)y,(float)w,24},
                       ps->action_value, (int)sizeof(ps->action_value),
                       ps->action_value_edit))
            ps->action_value_edit = !ps->action_value_edit;
        if (any_locked) GuiSetState(STATE_NORMAL);
        y += 30;

        if (any_locked) GuiSetState(STATE_DISABLED);
        if (GuiButton((Rectangle){(float)x,(float)y,(float)w,26}, "Apply Action")) {
            char assembled[MAX_ACTION_LEN];
            assemble_action(assembled, sizeof(assembled),
                            ps->action_type_active, ps->action_value);
            app_set_button_action(app, sel, assembled);
            snprintf(app->status_msg, sizeof(app->status_msg),
                     "Action set for %s.", id);
        }
        if (any_locked) GuiSetState(STATE_NORMAL);
        y += 32;

        if (any_locked) GuiSetState(STATE_DISABLED);
        if (GuiButton((Rectangle){(float)x,(float)y,(float)w,24},
                      ps->gif_overlay_val ? "GIF overlay: ON" : "GIF overlay: OFF")) {
            ps->gif_overlay_val = !ps->gif_overlay_val;
            app_set_button_gif_overlay(app, sel, ps->gif_overlay_val);
        }
        if (any_locked) GuiSetState(STATE_NORMAL);
        y += 30;

    } else {
        DrawText("Click a button to edit it", x, y, THEME_FONT_MID, THEME_TEXT_DIM);
        y += 26;
        ps->color_rect         = (Rectangle){(float)x,(float)y,          (float)w,24};
        ps->color_pressed_rect = (Rectangle){(float)x,(float)(y+54),     (float)w,24};
        ps->action_type_rect   = (Rectangle){(float)x,(float)(y+108),    (float)w,24};
        y += 268;
    }

}

/* ── GIF panel (left column, below the grid) ────────────────────────────────── */

static void draw_gif_panel(AppState *app, PanelState *ps) {
    bool any_locked = (ps->open_dropdown != DROPDOWN_NONE)
                   || ps->cfg_fb_visible || ps->options_open;
    bool busy = app->running;

    int y = GIF_PANEL_Y;
    int x = GRID_X;
    int w = RPANEL_X - PAD - GRID_X;   /* left column = ~390px */

    DrawText("GIF SETTINGS", x, y, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    y += 18;

    /* File row: textbox + browse button */
    DrawText("File:", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    y += 16;

    float tb_w = (float)(w - BROWSE_BTN_W - 4);
    if (busy || any_locked) GuiSetState(STATE_DISABLED);
    if (GuiTextBox((Rectangle){(float)x,(float)y,tb_w,22},
                   ps->gif_path, (int)sizeof(ps->gif_path), ps->gif_path_edit))
        ps->gif_path_edit = !ps->gif_path_edit;
    if (busy || any_locked) GuiSetState(STATE_NORMAL);
    if (busy || any_locked) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){(float)(x+w-BROWSE_BTN_W),(float)y,BROWSE_BTN_W,22}, "..."))
        ps->gif_fb_visible = !ps->gif_fb_visible;
    if (busy || any_locked) GuiSetState(STATE_NORMAL);
    y += 28;

    /* Load / Clear row */
    float lw = (float)(w / 2 - 3);
    if (busy || any_locked) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){(float)x,(float)y,lw,24}, "Load GIF"))
        app_load_gif(app, ps->gif_path);
    if (GuiButton((Rectangle){(float)(x + w/2 + 3),(float)y,lw,24}, "Clear")) {
        app_clear_gif(app);
        ps->gif_path[0] = '\0';
    }
    if (busy || any_locked) GuiSetState(STATE_NORMAL);
    y += 30;

    /* Color mode dropdown — full column width */
    DrawText("Color mode:", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    y += 16;
    ps->gif_mode_rect = (Rectangle){(float)x,(float)y,(float)w,22};
    y += 30;

    /* FPS row */
    DrawText("FPS:", x, y + 4, THEME_FONT_SMALL, THEME_TEXT_LABEL2);
    int fps_x = x + MeasureText("FPS:", THEME_FONT_SMALL) + 6;
    if (busy || any_locked) GuiSetState(STATE_DISABLED);
    if (GuiTextBox((Rectangle){(float)fps_x,(float)y,52,22},
                   ps->fps_buf, (int)sizeof(ps->fps_buf), ps->fps_edit)) {
        ps->fps_edit = !ps->fps_edit;
        if (!ps->fps_edit) app->fps = (float)atof(ps->fps_buf);
    }
    if (busy || any_locked) GuiSetState(STATE_NORMAL);
}

/* ── Config panel (right column, below the grid) ────────────────────────────── */

static void draw_cfg_panel(AppState *app, PanelState *ps) {
    bool any_locked = (ps->open_dropdown != DROPDOWN_NONE)
                   || ps->gif_fb_visible || ps->options_open;

    int y = GIF_PANEL_Y;
    int x = RPANEL_X;
    int w = RPANEL_W;

    DrawText("CONFIG", x, y, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    y += 18;

    /* Path row */
    DrawText("Path:", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    y += 16;
    float tb_w = (float)(w - BROWSE_BTN_W - 4);
    if (any_locked) GuiSetState(STATE_DISABLED);
    if (GuiTextBox((Rectangle){(float)x,(float)y,tb_w,22},
                   ps->config_path_buf, (int)sizeof(ps->config_path_buf),
                   ps->config_path_edit)) {
        ps->config_path_edit = !ps->config_path_edit;
        if (!ps->config_path_edit)
            snprintf(app->config_path, sizeof(app->config_path),
                     "%s", ps->config_path_buf);
    }
    if (any_locked) GuiSetState(STATE_NORMAL);
    if (any_locked) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){(float)(x + w - BROWSE_BTN_W),(float)y,
                               BROWSE_BTN_W, 22}, "..."))
        ps->cfg_fb_visible = !ps->cfg_fb_visible;
    if (any_locked) GuiSetState(STATE_NORMAL);
    y += 28;

    /* Load / Save row */
    float hw = (float)(w / 2 - 3);
    if (any_locked) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){(float)x,(float)y,hw,24}, "Load Config")) {
        snprintf(app->config_path, sizeof(app->config_path),
                 "%s", ps->config_path_buf);
        app_reload_config(app);
        ps->last_sel = -2;
    }
    if (GuiButton((Rectangle){(float)(x + w/2 + 3),(float)y,hw,24}, "Save Config"))
        app_save_config(app);
    if (any_locked) GuiSetState(STATE_NORMAL);
}

/* ── Options panel overlay ───────────────────────────────────────────────────── */

static void draw_options_panel(AppState *app, PanelState *ps) {
    int x = OPTS_X, y = OPTS_Y, w = OPTS_W;

    /* Shadow / dim */
    DrawRectangle(0, 0, WIN_W, WIN_H, (Color){0,0,0,120});

    /* Panel background */
    DrawRectangle(x, y, w, OPTS_H, (Color){28,28,36,255});
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,OPTS_H},
                         1, THEME_BTN_BORDER_HOV);

    /* Title row */
    DrawText("OPTIONS", x + PAD, y + 10, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    if (GuiButton((Rectangle){(float)(x+w-30),(float)(y+6),24,24}, "X"))
        ps->options_open = 0;
    y += 36;

    /* ── FONT ── */
    DrawLine(x + PAD, y, x + w - PAD, y, THEME_DIVIDER);
    y += 8;
    DrawText("FONT", x + PAD, y, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    y += 18;

    int ix = x + PAD, iw = w - PAD * 2;

    DrawText("Path:", ix, y, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    y += 16;
    float fp_w = (float)(iw - BROWSE_BTN_W - 4);
    GuiTextBox((Rectangle){(float)ix,(float)y,fp_w,22},
               ps->opts.font_path, sizeof(ps->opts.font_path), ps->opts_fp_edit);
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
        CheckCollisionPointRec(GetMousePosition(),
            (Rectangle){(float)ix,(float)y,fp_w,22}))
        ps->opts_fp_edit = !ps->opts_fp_edit;

    if (GuiButton((Rectangle){(float)(ix+iw-BROWSE_BTN_W),(float)y,BROWSE_BTN_W,22}, "..."))
        ps->font_fb_visible = !ps->font_fb_visible;
    y += 28;

    DrawText("Size (px):", ix, y, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    int fs_x = ix + MeasureText("Size (px):", THEME_FONT_LABEL) + 6;
    if (GuiTextBox((Rectangle){(float)fs_x,(float)y,52,22},
                   ps->opts_fs_buf, sizeof(ps->opts_fs_buf), ps->opts_fs_edit))
        ps->opts_fs_edit = !ps->opts_fs_edit;
    y += 28;

    /* Font file browser (shown when toggle is on) */
    if (ps->font_fb_visible) {
        int fb_h = 100;
        if (fb_draw(&ps->font_fb, ix, y, iw, fb_h, false,
                    ps->opts.font_path, sizeof(ps->opts.font_path))) {
            ps->font_fb_visible = 0;
        }
        y += fb_h + 4;
    }

    if (GuiButton((Rectangle){(float)(ix+iw-80),(float)y,80,22}, "Apply Font")) {
        ps->opts.font_size = atoi(ps->opts_fs_buf);
        opts_apply(&ps->opts);
    }
    y += 30;

    /* ── COLORS ── */
    DrawLine(x + PAD, y, x + w - PAD, y, THEME_DIVIDER);
    y += 8;
    DrawText("COLORS", ix, y, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    y += 18;

    static const char *col_labels[]   = { "Background:", "Header BG:", "Accent:", "Button off:" };
    char              *col_bufs[]     = { ps->opts.col_bg, ps->opts.col_header,
                                          ps->opts.col_accent, ps->opts.col_btn_off };
    int               *col_edits[]    = { &ps->opts_bg_edit, &ps->opts_hdr_edit,
                                          &ps->opts_acc_edit, &ps->opts_btn_edit };
    Color               col_current[] = { THEME_BG, THEME_HEADER_BG,
                                          THEME_TEXT_ACCENT, THEME_BTN_OFF };

    int lbl_w = MeasureText("Background:", THEME_FONT_LABEL) + 4;  /* widest label */
    for (int i = 0; i < 4; i++) {
        DrawText(col_labels[i], ix, y + 3, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
        int hx = ix + lbl_w;
        /* live swatch of current value in the textbox */
        Color preview = parse_hex_color(col_bufs[i], col_current[i]);
        DrawRectangle(hx, y, 20, 22, preview);
        DrawRectangleLinesEx((Rectangle){(float)hx,(float)y,20,22}, 1, THEME_BTN_BORDER);
        GuiTextBox((Rectangle){(float)(hx+24),(float)y,(float)(iw-lbl_w-24-4),22},
                   col_bufs[i], 7, *col_edits[i]);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
            CheckCollisionPointRec(GetMousePosition(),
                (Rectangle){(float)(hx+24),(float)y,(float)(iw-lbl_w-24-4),22}))
            *col_edits[i] = !(*col_edits[i]);
        y += 26;
    }

    if (GuiButton((Rectangle){(float)(ix+iw-90),(float)y,90,22}, "Apply Colors")) {
        opts_apply(&ps->opts);
    }
    y += 30;

    /* ── Save / Load ── */
    DrawLine(x + PAD, y, x + w - PAD, y, THEME_DIVIDER);
    y += 10;
    char opts_path[512];
    gui_opts_path(app->config_path, opts_path, sizeof(opts_path));
    float bw = (float)(iw / 2 - 4);
    if (GuiButton((Rectangle){(float)ix,(float)y,bw,24}, "Save Options"))
        opts_save(&ps->opts, opts_path);
    if (GuiButton((Rectangle){(float)(ix+iw/2+4),(float)y,bw,24}, "Load Options")) {
        opts_load(&ps->opts, opts_path);
        snprintf(ps->opts_fs_buf, sizeof(ps->opts_fs_buf), "%d", ps->opts.font_size);
    }
}

/* ── File-browser popup overlays ────────────────────────────────────────────── */

static void draw_gif_popup(PanelState *ps) {
    /* Covers the GIF panel area */
    int x = GRID_X;
    int y = GIF_PANEL_Y;
    int w = RPANEL_X - PAD - GRID_X;
    int h = GIF_PANEL_H;

    DrawRectangle(x, y, w, h, (Color){20,20,26,240});
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,(float)h},
                         1, THEME_BTN_BORDER_HOV);

    /* Title + close */
    DrawText("Select GIF file", x + PAD, y + 6, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    if (GuiButton((Rectangle){(float)(x+w-30),(float)(y+4),24,22}, "X"))
        ps->gif_fb_visible = 0;

    int fb_y = y + 28;
    int fb_h = h - 28;
    if (fb_draw(&ps->gif_fb, x, fb_y, w, fb_h, false,
                ps->gif_path, sizeof(ps->gif_path)))
        ps->gif_fb_visible = 0;  /* auto-close on selection */
}

static void draw_cfg_popup(AppState *app, PanelState *ps) {
    /* Float over the config panel area */
    int x = RPANEL_X;
    int y = GIF_PANEL_Y;
    int w = RPANEL_W;
    int h = GIF_PANEL_H;

    DrawRectangle(x, y, w, h, (Color){20,20,26,245});
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,(float)h},
                         1, THEME_BTN_BORDER_HOV);

    DrawText("Select config file", x + PAD, y + 6, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    if (GuiButton((Rectangle){(float)(x+w-30),(float)(y+4),24,22}, "X"))
        ps->cfg_fb_visible = 0;

    int fb_y = y + 28;
    int fb_h = h - 28;
    if (fb_draw(&ps->cfg_fb, x, fb_y, w, fb_h, false,
                ps->config_path_buf, sizeof(ps->config_path_buf))) {
        snprintf(app->config_path, sizeof(app->config_path),
                 "%s", ps->config_path_buf);
        ps->cfg_fb_visible = 0;
    }
}

/* ── Header ─────────────────────────────────────────────────────────────────── */

static void draw_header(AppState *app, PanelState *ps) {
    DrawRectangle(0, 0, WIN_W, HDR_H, THEME_HEADER_BG);

    DrawText("launc-macro", 14, 12, THEME_FONT_TITLE, THEME_TEXT_TITLE);
    DrawText("GUI",
             14 + MeasureText("launc-macro", THEME_FONT_TITLE) + 5, 12,
             THEME_FONT_TITLE, THEME_TEXT_ACCENT);

    /* Status indicator */
    Color dot = app->running          ? THEME_DOT_RUNNING
              : app->device_connected ? THEME_DOT_IDLE
                                      : THEME_DOT_OFFLINE;
    const char *dot_lbl = app->running          ? "running"
                        : app->device_connected ? "idle"
                                                : "offline";
    DrawCircle(WIN_W - 188, HDR_H / 2, 6, dot);
    DrawText(dot_lbl, WIN_W - 177, HDR_H / 2 - 7, THEME_FONT_MID, dot);

    /* Options toggle — right edge of header */
    if (GuiButton((Rectangle){ WIN_W - PAD - 80, 10, 80, 30 }, "Options"))
        ps->options_open = !ps->options_open;

    /* Connect / Disconnect */
    if (!app->device_connected) {
        if (GuiButton((Rectangle){ 200, 10, 110, 30 }, "Connect"))
            app_connect(app);
    } else {
        if (GuiButton((Rectangle){ 200, 10, 110, 30 }, "Disconnect"))
            app_disconnect(app);
    }

    /* Start / Stop */
    if (app->device_connected) {
        if (!app->running) {
            if (GuiButton((Rectangle){ 320, 10, 90, 30 }, "Start"))
                app_start(app);
        } else {
            if (GuiButton((Rectangle){ 320, 10, 90, 30 }, "Stop"))
                app_stop(app);
        }
    }
}

/* ── Main ───────────────────────────────────────────────────────────────────── */

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WIN_W, WIN_H, "launc-macro GUI");
    SetTargetFPS(60);

    apply_theme();

    AppState  *app = app_create();
    app_connect(app);

    int        sel = -1;
    PanelState ps  = { 0 };
    ps.last_sel      = -2;
    ps.open_dropdown = DROPDOWN_NONE;
    snprintf(ps.fps_buf,        sizeof(ps.fps_buf),        "0");
    snprintf(ps.config_path_buf,sizeof(ps.config_path_buf),"%s", app->config_path);

    char home[512];
    get_home_dir(home, sizeof(home));
    fb_init(&ps.gif_fb,  home, ".gif");
    fb_init(&ps.cfg_fb,  home, NULL);
    fb_init(&ps.font_fb, home, ".ttf");

    opts_defaults(&ps.opts);
    /* Try loading saved options */
    char opts_path[512];
    gui_opts_path(app->config_path, opts_path, sizeof(opts_path));
    opts_load(&ps.opts, opts_path);
    opts_apply(&ps.opts);
    snprintf(ps.opts_fs_buf, sizeof(ps.opts_fs_buf), "%d", ps.opts.font_size);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(THEME_BG);

        bool any_open   = (ps.open_dropdown != DROPDOWN_NONE);
        /* "globally locked" = dropdown open OR a popup is open */
        bool any_locked = any_open
                       || ps.gif_fb_visible || ps.cfg_fb_visible
                       || ps.options_open;

        if (any_locked) GuiLock();

        draw_header(app, &ps);
        draw_launchpad(app, &sel, any_locked);
        draw_right_panel(app, sel, &ps);
        draw_gif_panel(app, &ps);
        draw_cfg_panel(app, &ps);

        /* Status bar */
        DrawRectangle(0, WIN_H - SBAR_H, WIN_W, SBAR_H, THEME_SBAR_BG);
        DrawText(app->status_msg, 10, WIN_H - SBAR_H + 6,
                 THEME_FONT_LABEL, THEME_TEXT_SBAR);

        /* Unlock so overlays and dropdowns receive input */
        if (any_locked) GuiUnlock();

        /* ── Draw all dropdowns (closed ones first, then open one on top) ── */
        int frame_start_dropdown = any_open ? ps.open_dropdown : DROPDOWN_NONE;
        bool dis_btn = (sel < 0);
        bool dis_gif = app->running;

        if (ps.open_dropdown != DROPDOWN_COLOR) {
            if (dis_btn || any_open) GuiSetState(STATE_DISABLED);
            if (GuiDropdownBox(ps.color_rect, COLOUR_DROPDOWN,
                               &ps.color_active, 0) && !dis_btn && !any_open)
                ps.open_dropdown = DROPDOWN_COLOR;
            GuiSetState(STATE_NORMAL);
        }
        if (ps.open_dropdown != DROPDOWN_COLOR_PRESSED) {
            if (dis_btn || any_open) GuiSetState(STATE_DISABLED);
            if (GuiDropdownBox(ps.color_pressed_rect, COLOUR_DROPDOWN,
                               &ps.color_pressed_active, 0) && !dis_btn && !any_open)
                ps.open_dropdown = DROPDOWN_COLOR_PRESSED;
            GuiSetState(STATE_NORMAL);
        }
        if (ps.open_dropdown != DROPDOWN_ACTION_TYPE) {
            if (dis_btn || any_open) GuiSetState(STATE_DISABLED);
            if (GuiDropdownBox(ps.action_type_rect, ACTION_TYPE_DROPDOWN,
                               &ps.action_type_active, 0) && !dis_btn && !any_open)
                ps.open_dropdown = DROPDOWN_ACTION_TYPE;
            GuiSetState(STATE_NORMAL);
        }
        if (ps.open_dropdown != DROPDOWN_GIF_MODE) {
            if (dis_gif || any_open) GuiSetState(STATE_DISABLED);
            if (GuiDropdownBox(ps.gif_mode_rect, "Full;Red;Green;Yellow",
                               &ps.gif_mode_active, 0) && !dis_gif && !any_open)
                ps.open_dropdown = DROPDOWN_GIF_MODE;
            GuiSetState(STATE_NORMAL);
        }
        switch (frame_start_dropdown) {
        case DROPDOWN_COLOR:
            if (GuiDropdownBox(ps.color_rect, COLOUR_DROPDOWN, &ps.color_active, 1)) {
                ps.open_dropdown = DROPDOWN_NONE;
                if (sel >= 0) app_set_button_color(app, sel, PALETTE[ps.color_active].vel);
            }
            break;
        case DROPDOWN_COLOR_PRESSED:
            if (GuiDropdownBox(ps.color_pressed_rect, COLOUR_DROPDOWN,
                               &ps.color_pressed_active, 1)) {
                ps.open_dropdown = DROPDOWN_NONE;
                if (sel >= 0) app_set_button_color_pressed(app, sel,
                                    PALETTE[ps.color_pressed_active].vel);
            }
            break;
        case DROPDOWN_ACTION_TYPE:
            if (GuiDropdownBox(ps.action_type_rect, ACTION_TYPE_DROPDOWN,
                               &ps.action_type_active, 1))
                ps.open_dropdown = DROPDOWN_NONE;
            break;
        case DROPDOWN_GIF_MODE:
            if (GuiDropdownBox(ps.gif_mode_rect, "Full;Red;Green;Yellow",
                               &ps.gif_mode_active, 1)) {
                ps.open_dropdown = DROPDOWN_NONE;
                app->gif_mode = ps.gif_mode_active;
            }
            break;
        default: break;
        }

        /* ── Popup overlays (drawn on top of everything) ── */
        if (ps.gif_fb_visible)  draw_gif_popup(&ps);
        if (ps.cfg_fb_visible)  draw_cfg_popup(app, &ps);
        if (ps.options_open)    draw_options_panel(app, &ps);

        EndDrawing();
    }

    app_destroy(app);
    CloseWindow();
    return 0;
}
