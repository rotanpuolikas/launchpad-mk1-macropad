/*
 * launc-macro-gui — raylib + raygui front-end for launc-macro
 *
 * Window layout (fixed 820×728):
 *
 *  ┌──────────────────────────────────────────────────────────────────┐
 *  │ header: title / connect / start / [Opts] / status indicator      │ 50px
 *  ├──────────────────────────────────────────────────────────────────┤
 *  │ LAYERS: [1] [2] ... [+]   (layer bar)                            │ 28px
 *  ├─────────────────────────────────┬────────────────────────────────┤
 *  │                                 │  Button Editor                  │
 *  │  Launchpad 9×9 visual grid      │    color dropdowns + swatches  │
 *  │  (click to select button)       │    action type + value         │
 *  │                                 │  Config  path [...]            │
 *  ├─────────────────────────────────┴────────────────────────────────┤
 *  │ GIF SETTINGS   File: [path] [...]  Load | Clear  FPS  ColorMode  │
 *  │ EQUALIZER  [toggle]  [EQ Settings]                               │
 *  ├──────────────────────────────────────────────────────────────────┤
 *  │ status bar                                                        │ 25px
 *  └──────────────────────────────────────────────────────────────────┘
 *
 *  Floating overlays (drawn last, over everything):
 *    • gif_fb_visible  — file browser popup (GIF)
 *    • cfg_fb_visible  — file browser popup (config)
 *    • options_open    — theme/font options panel
 *    • eq_open         — equalizer settings panel
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

#define WIN_W        820
#define WIN_H        728
#define HDR_H         50
#define LAYER_BAR_H   28
#define LAYER_BAR_Y   HDR_H
#define SBAR_H        25
#define PAD           12

/* Launchpad grid */
#define BTN_SZ    38
#define BTN_GAP    4
#define BTN_STEP  (BTN_SZ + BTN_GAP)
#define GRID_X    PAD
#define GRID_Y    (HDR_H + LAYER_BAR_H + PAD + 16)   /* +16 for column-index labels */

/* Right info panel */
#define RPANEL_X  (GRID_X + 9 * BTN_STEP + PAD * 2)
#define RPANEL_W  (WIN_W - RPANEL_X - PAD)
#define RPANEL_Y  GRID_Y

/* GIF panel (below the launchpad grid) */
#define GIF_PANEL_Y  (GRID_Y + 9 * BTN_STEP + PAD)
#define GIF_PANEL_H  (WIN_H - SBAR_H - GIF_PANEL_Y - PAD)

/* Config panel (right column, below the button editor) */
#define CFG_PANEL_Y  546

/* Options overlay panel */
#define OPTS_W    340
#define OPTS_H    420
#define OPTS_X    ((WIN_W - OPTS_W) / 2)
#define OPTS_Y    (HDR_H + LAYER_BAR_H + 6)

/* Equalizer settings overlay */
#define EQ_PANEL_W   310
#define EQ_PANEL_H   220
#define EQ_PANEL_X   ((WIN_W - EQ_PANEL_W) / 2)
#define EQ_PANEL_Y   (HDR_H + LAYER_BAR_H + 40)

/* Browse-button width */
#define BROWSE_BTN_W  26

/* Dropdown identifiers */
#define DROPDOWN_NONE          0
#define DROPDOWN_COLOR         1
#define DROPDOWN_COLOR_PRESSED 2
#define DROPDOWN_ACTION_TYPE   3
#define DROPDOWN_GIF_MODE      4
#define DROPDOWN_EQ_COLOR      5

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
    const char *filter;
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

    if (!locked && CheckCollisionPointRec(mouse,
            (Rectangle){(float)x,(float)list_y,(float)w,(float)list_h})) {
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

    if (fb->count > vis_rows) {
        float sb_h = (float)list_h * vis_rows / fb->count;
        float sb_y = (float)list_y + (float)list_h * fb->scroll / fb->count;
        DrawRectangle(x + w - 4, (int)sb_y, 3, (int)sb_h, THEME_BTN_BORDER_HOV);
    }
    return result;
}

/* ── GUI options ────────────────────────────────────────────────────────────── */

typedef struct {
    char font_path[512];
    int  font_size;
    char col_bg[8];
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

        if      (!strcmp(k, "font_path"))   strncpy(o->font_path,  v, sizeof(o->font_path)  - 1);
        else if (!strcmp(k, "font_size"))   o->font_size = atoi(v);
        else if (!strcmp(k, "col_bg"))      strncpy(o->col_bg,     v, sizeof(o->col_bg)     - 1);
        else if (!strcmp(k, "col_header"))  strncpy(o->col_header, v, sizeof(o->col_header) - 1);
        else if (!strcmp(k, "col_accent"))  strncpy(o->col_accent, v, sizeof(o->col_accent) - 1);
        else if (!strcmp(k, "col_btn_off")) strncpy(o->col_btn_off,v, sizeof(o->col_btn_off)- 1);
    }
    fclose(f);
}

/* ── Palette helpers ────────────────────────────────────────────────────────── */

static const char *COLOUR_DROPDOWN =
    "black;red_low;red_med;red_max;"
    "green_low;green_med;green_max;"
    "yellow_low;yellow_med;yellow_max";

/* none=0  key=1  string=2  media=3  app=4  terminal=5  layer=6 */
static const char *ACTION_TYPE_DROPDOWN = "none;key;string;media;app;terminal;layer";

static const char *EQ_COLOR_DROPDOWN = "green;red;yellow";

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
            if (row == 0 && col == 8) continue;

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

    for (int c = 0; c < 8; c++) {
        DrawText(TextFormat("%d", c + 1),
                 GRID_X + c * BTN_STEP + BTN_SZ / 2 - 3,
                 GRID_Y - THEME_FONT_SMALL - 4,
                 THEME_FONT_SMALL, THEME_BTN_BORDER);
    }
    for (int row = 1; row <= 8; row++) {
        char letter[2] = { (char)('A' + row - 1), '\0' };
        DrawText(letter,
                 GRID_X + 9 * BTN_STEP + 4,
                 GRID_Y + row * BTN_STEP + BTN_SZ / 2 - THEME_FONT_SMALL / 2,
                 THEME_FONT_SMALL, THEME_BTN_BORDER);
    }
}

/* ── Layer bar ──────────────────────────────────────────────────────────────── */

static void draw_layer_bar(AppState *app, bool locked) {
    DrawRectangle(0, LAYER_BAR_Y, WIN_W, LAYER_BAR_H, (Color){20,20,26,255});
    DrawLine(0, LAYER_BAR_Y + LAYER_BAR_H - 1, WIN_W,
             LAYER_BAR_Y + LAYER_BAR_H - 1, THEME_DIVIDER);

    int x = PAD;
    int y = LAYER_BAR_Y + 3;
    int bh = LAYER_BAR_H - 6;

    DrawText("Layers:", x, y + (bh - THEME_FONT_SMALL) / 2,
             THEME_FONT_SMALL, THEME_TEXT_LABEL2);
    x += 56;

    int n = app->config.num_layers;
    for (int i = 0; i < n; i++) {
        int bw = 28;
        Rectangle r = { (float)x, (float)y, (float)bw, (float)bh };
        bool is_active = (i == app->active_layer);

        Color bg    = is_active ? THEME_TEXT_ACCENT : (Color){38,38,50,255};
        Color fg    = is_active ? (Color){255,255,255,255} : THEME_TEXT_LABEL2;
        Color brd   = is_active ? WHITE : THEME_BTN_BORDER;

        DrawRectangleRec(r, bg);
        DrawRectangleLinesEx(r, 1, brd);

        char lbl[4];
        snprintf(lbl, sizeof(lbl), "%d", i + 1);
        int tw = MeasureText(lbl, THEME_FONT_SMALL);
        DrawText(lbl, x + (bw - tw) / 2, y + (bh - THEME_FONT_SMALL) / 2,
                 THEME_FONT_SMALL, fg);

        /* Click to switch layer */
        if (!locked && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 m = GetMousePosition();
            if (CheckCollisionPointRec(m, r))
                app_switch_layer(app, i);
        }
        x += bw + 3;
    }

    /* "+" button — add a new layer */
    if (n < MAX_LAYERS) {
        int bw = 22;
        Rectangle r = { (float)x, (float)y, (float)bw, (float)bh };
        if (!locked) {
            if (GuiButton(r, "+")) {
                int new_idx = app_add_layer(app);
                if (new_idx >= 0) app_switch_layer(app, new_idx);
            }
        } else {
            GuiSetState(STATE_DISABLED);
            GuiButton(r, "+");
            GuiSetState(STATE_NORMAL);
        }
    }
}

/* ── Header ─────────────────────────────────────────────────────────────────── */

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

    /* GIF settings (per-layer, refreshed when active_layer changes) */
    char gif_path[512];
    int  gif_path_edit;
    char fps_buf[16];
    int  fps_edit;
    int  gif_mode_active;
    int  last_gif_layer;  /* tracks which layer gif fields were loaded from */

    /* Equalizer settings overlay */
    int  eq_open;
    int  eq_enabled_val;
    char eq_x_buf[8];
    char eq_y_buf[8];
    char eq_w_buf[8];
    char eq_h_buf[8];
    int  eq_color_active;
    char eq_device_buf[128];
    int  eq_x_edit, eq_y_edit, eq_w_edit, eq_h_edit, eq_dev_edit;
    Rectangle eq_color_rect;

    /* Dropdowns */
    int  open_dropdown;
    int  last_sel;
    int  last_edit_layer;  /* tracks active_layer for button editor refresh */
    Rectangle color_rect;
    Rectangle color_pressed_rect;
    Rectangle action_type_rect;
    Rectangle gif_mode_rect;

    /* File browsers */
    FileBrowser gif_fb;
    FileBrowser cfg_fb;
    int         gif_fb_visible;
    int         cfg_fb_visible;

    /* Repeat-on-hold */
    int  repeat_on_hold_val;
    char hold_delay_buf[8];
    char repeat_interval_buf[8];
    int  hold_delay_edit;
    int  repeat_interval_edit;

    /* Button clipboard */
    bool    btn_clip_valid;
    uint8_t btn_clip_color;
    uint8_t btn_clip_color_pressed;
    char    btn_clip_action[MAX_ACTION_LEN];
    int     btn_clip_gif_overlay;
    int     btn_clip_repeat_on_hold;
    int     btn_clip_hold_delay_ms;
    int     btn_clip_repeat_interval_ms;

    /* Options panel */
    int        options_open;
    GuiOptions opts;
    FileBrowser font_fb;
    int         font_fb_visible;
    int  opts_fp_edit;
    int  opts_fs_edit;
    int  opts_bg_edit;
    int  opts_hdr_edit;
    int  opts_acc_edit;
    int  opts_btn_edit;
    char opts_fs_buf[8];
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
    else if (!strncmp(action, "layer:",     6)) { *type_out = 6; strncpy(val_out, action + 6, val_size-1); }
    else { *type_out = 4; snprintf(val_out, val_size, "%s", action); }
}

static void assemble_action(char *out, int size, int type_active, const char *value) {
    static const char *prefixes[] = { "", "key:", "string:", "media:", "app:", "terminal:", "layer:" };
    if (type_active == 0 || value[0] == '\0') out[0] = '\0';
    else snprintf(out, size, "%s%s", prefixes[type_active], value);
}

static bool any_textbox_active(const PanelState *ps) {
    return ps->action_value_edit || ps->config_path_edit ||
           ps->gif_path_edit    || ps->fps_edit          ||
           ps->opts_fp_edit     || ps->opts_fs_edit      ||
           ps->opts_bg_edit     || ps->opts_hdr_edit     ||
           ps->opts_acc_edit    || ps->opts_btn_edit     ||
           ps->hold_delay_edit  || ps->repeat_interval_edit ||
           ps->eq_x_edit || ps->eq_y_edit || ps->eq_w_edit ||
           ps->eq_h_edit || ps->eq_dev_edit;
}

static void textbox_clipboard(char *buf, int buf_size, bool in_edit) {
    if (!in_edit) return;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (!ctrl) return;
    if (IsKeyPressed(KEY_C)) {
        SetClipboardText(buf);
    } else if (IsKeyPressed(KEY_X)) {
        SetClipboardText(buf);
        buf[0] = '\0';
    } else if (IsKeyPressed(KEY_V)) {
        const char *cb = GetClipboardText();
        if (cb && cb[0]) {
            strncpy(buf, cb, buf_size - 1);
            buf[buf_size - 1] = '\0';
        }
    }
}

/* Refresh GIF/EQ PanelState fields from the current active layer */
static void sync_ps_from_layer(AppState *app, PanelState *ps) {
    int li = app->active_layer;
    if (li >= app->config.num_layers) li = 0;
    const LayerCfg *layer = &app->config.layers[li];

    strncpy(ps->gif_path, layer->gif_path, sizeof(ps->gif_path) - 1);
    ps->gif_path[sizeof(ps->gif_path) - 1] = '\0';
    snprintf(ps->fps_buf, sizeof(ps->fps_buf), "%.2g", layer->fps > 0 ? (double)layer->fps : 0.0);
    ps->gif_mode_active = layer->gif_mode;

    ps->eq_enabled_val  = layer->eq_enabled;
    snprintf(ps->eq_x_buf, sizeof(ps->eq_x_buf), "%d", layer->eq_x);
    snprintf(ps->eq_y_buf, sizeof(ps->eq_y_buf), "%d", layer->eq_y);
    snprintf(ps->eq_w_buf, sizeof(ps->eq_w_buf), "%d", layer->eq_width  > 0 ? layer->eq_width  : 8);
    snprintf(ps->eq_h_buf, sizeof(ps->eq_h_buf), "%d", layer->eq_height > 0 ? layer->eq_height : 7);
    ps->eq_color_active = layer->eq_color;
    strncpy(ps->eq_device_buf, layer->eq_device, sizeof(ps->eq_device_buf) - 1);
    ps->eq_device_buf[sizeof(ps->eq_device_buf) - 1] = '\0';

    ps->last_gif_layer = li;
}

/* ── Right panel ─────────────────────────────────────────────────────────────── */

static void draw_right_panel(AppState *app, int sel, PanelState *ps) {
    int x = RPANEL_X;
    int y = RPANEL_Y;
    int w = RPANEL_W;

    bool any_locked = (ps->open_dropdown != DROPDOWN_NONE)
                   || ps->gif_fb_visible || ps->cfg_fb_visible
                   || ps->options_open   || ps->eq_open;

    /* Refresh when selection OR active layer changes */
    if (sel != ps->last_sel || app->active_layer != ps->last_edit_layer) {
        ps->last_sel        = sel;
        ps->last_edit_layer = app->active_layer;
        if (sel >= 0) {
            const char *id  = button_index_to_id(sel);
            const ButtonCfg *btn = config_find_button_in_layer(
                &app->config, app->active_layer, id);
            if (btn) {
                ps->color_active         = vel_to_palette_idx(btn->color);
                ps->color_pressed_active = vel_to_palette_idx(btn->color_pressed);
                parse_action(btn->action, &ps->action_type_active,
                             ps->action_value, sizeof(ps->action_value));
                ps->gif_overlay_val    = btn->gif_overlay;
                ps->repeat_on_hold_val = btn->repeat_on_hold;
                snprintf(ps->hold_delay_buf,      sizeof(ps->hold_delay_buf),      "%d",
                         btn->hold_delay_ms      > 0 ? btn->hold_delay_ms      : 500);
                snprintf(ps->repeat_interval_buf, sizeof(ps->repeat_interval_buf), "%d",
                         btn->repeat_interval_ms > 0 ? btn->repeat_interval_ms : 100);
            } else {
                ps->color_active         = vel_to_palette_idx(app->config.default_color);
                ps->color_pressed_active = vel_to_palette_idx(app->config.default_color_pressed);
                ps->action_type_active   = 0;
                ps->action_value[0]      = '\0';
                ps->gif_overlay_val      = 0;
                ps->repeat_on_hold_val   = 0;
                snprintf(ps->hold_delay_buf,      sizeof(ps->hold_delay_buf),      "500");
                snprintf(ps->repeat_interval_buf, sizeof(ps->repeat_interval_buf), "100");
            }
        }
    }

    DrawText(TextFormat("BUTTON EDITOR  (layer %d)", app->active_layer + 1),
             x, y, THEME_FONT_LABEL, THEME_TEXT_SECTION);
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
        textbox_clipboard(ps->action_value, sizeof(ps->action_value), ps->action_value_edit);
        y += 30;

        if (any_locked) GuiSetState(STATE_DISABLED);
        if (GuiButton((Rectangle){(float)x,(float)y,(float)w,26}, "Apply Action")) {
            char assembled[MAX_ACTION_LEN];
            assemble_action(assembled, sizeof(assembled),
                            ps->action_type_active, ps->action_value);
            app_set_button_action(app, sel, assembled);
            snprintf(app->status_msg, sizeof(app->status_msg),
                     "Action set for %s (layer %d).", id, app->active_layer + 1);
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

        if (any_locked) GuiSetState(STATE_DISABLED);
        if (GuiButton((Rectangle){(float)x,(float)y,(float)w,24},
                      ps->repeat_on_hold_val ? "Repeat on hold: ON" : "Repeat on hold: OFF")) {
            ps->repeat_on_hold_val = !ps->repeat_on_hold_val;
            app_set_button_repeat(app, sel, ps->repeat_on_hold_val,
                                  atoi(ps->hold_delay_buf), atoi(ps->repeat_interval_buf));
        }
        if (any_locked) GuiSetState(STATE_NORMAL);
        y += 30;

        DrawText("Hold delay (ms):", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL);
        y += 18;
        if (any_locked) GuiSetState(STATE_DISABLED);
        if (GuiTextBox((Rectangle){(float)x,(float)y,(float)w,24},
                       ps->hold_delay_buf, (int)sizeof(ps->hold_delay_buf),
                       ps->hold_delay_edit))
            ps->hold_delay_edit = !ps->hold_delay_edit;
        if (any_locked) GuiSetState(STATE_NORMAL);
        textbox_clipboard(ps->hold_delay_buf, sizeof(ps->hold_delay_buf), ps->hold_delay_edit);
        y += 30;

        DrawText("Repeat interval (ms):", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL);
        y += 18;
        if (any_locked) GuiSetState(STATE_DISABLED);
        if (GuiTextBox((Rectangle){(float)x,(float)y,(float)w,24},
                       ps->repeat_interval_buf, (int)sizeof(ps->repeat_interval_buf),
                       ps->repeat_interval_edit))
            ps->repeat_interval_edit = !ps->repeat_interval_edit;
        if (any_locked) GuiSetState(STATE_NORMAL);
        textbox_clipboard(ps->repeat_interval_buf, sizeof(ps->repeat_interval_buf), ps->repeat_interval_edit);

    } else {
        DrawText("Click a button to edit it", x, y, THEME_FONT_MID, THEME_TEXT_DIM);
        y += 26;
        ps->color_rect         = (Rectangle){(float)x,(float)y,          (float)w,24};
        ps->color_pressed_rect = (Rectangle){(float)x,(float)(y+54),     (float)w,24};
        ps->action_type_rect   = (Rectangle){(float)x,(float)(y+108),    (float)w,24};
    }
}

/* ── GIF panel ───────────────────────────────────────────────────────────────── */

static void draw_gif_panel(AppState *app, PanelState *ps) {
    bool any_locked = (ps->open_dropdown != DROPDOWN_NONE)
                   || ps->cfg_fb_visible || ps->options_open || ps->eq_open;
    bool busy = app->running;

    /* Refresh gif fields when active layer changes */
    if (app->active_layer != ps->last_gif_layer)
        sync_ps_from_layer(app, ps);

    int y = GIF_PANEL_Y;
    int x = GRID_X;
    int w = RPANEL_X - PAD - GRID_X;

    DrawText(TextFormat("GIF SETTINGS  (layer %d)", app->active_layer + 1),
             x, y, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    y += 18;

    DrawText("File:", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    y += 16;

    float tb_w = (float)(w - BROWSE_BTN_W - 4);
    if (busy || any_locked) GuiSetState(STATE_DISABLED);
    if (GuiTextBox((Rectangle){(float)x,(float)y,tb_w,22},
                   ps->gif_path, (int)sizeof(ps->gif_path), ps->gif_path_edit))
        ps->gif_path_edit = !ps->gif_path_edit;
    if (busy || any_locked) GuiSetState(STATE_NORMAL);
    textbox_clipboard(ps->gif_path, sizeof(ps->gif_path), ps->gif_path_edit);
    if (busy || any_locked) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){(float)(x+w-BROWSE_BTN_W),(float)y,BROWSE_BTN_W,22}, "..."))
        ps->gif_fb_visible = !ps->gif_fb_visible;
    if (busy || any_locked) GuiSetState(STATE_NORMAL);
    y += 28;

    float lw = (float)(w / 2 - 3);
    if (busy || any_locked) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){(float)x,(float)y,lw,24}, "Load GIF")) {
        int li = app->active_layer;
        strncpy(app->config.layers[li].gif_path, ps->gif_path,
                sizeof(app->config.layers[li].gif_path) - 1);
        app_load_layer_gif(app, li, ps->gif_path);
    }
    if (GuiButton((Rectangle){(float)(x + w/2 + 3),(float)y,lw,24}, "Clear")) {
        app_clear_layer_gif(app, app->active_layer);
        ps->gif_path[0] = '\0';
        app->config.layers[app->active_layer].gif_path[0] = '\0';
    }
    if (busy || any_locked) GuiSetState(STATE_NORMAL);
    y += 30;

    DrawText("Color mode:", x, y, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    y += 16;
    ps->gif_mode_rect = (Rectangle){(float)x,(float)y,(float)w,22};
    y += 30;

    DrawText("FPS:", x, y + 4, THEME_FONT_SMALL, THEME_TEXT_LABEL2);
    int fps_x = x + MeasureText("FPS:", THEME_FONT_SMALL) + 6;
    if (busy || any_locked) GuiSetState(STATE_DISABLED);
    if (GuiTextBox((Rectangle){(float)fps_x,(float)y,52,22},
                   ps->fps_buf, (int)sizeof(ps->fps_buf), ps->fps_edit)) {
        ps->fps_edit = !ps->fps_edit;
        if (!ps->fps_edit)
            app->config.layers[app->active_layer].fps = (float)atof(ps->fps_buf);
    }
    if (busy || any_locked) GuiSetState(STATE_NORMAL);
    textbox_clipboard(ps->fps_buf, sizeof(ps->fps_buf), ps->fps_edit);
    y += 30;

    /* Equalizer toggle + settings button */
    DrawLine(x, y, x + w, y, THEME_DIVIDER);
    y += 6;
    DrawText(TextFormat("EQUALIZER  (layer %d)", app->active_layer + 1),
             x, y, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    y += 16;

    if (any_locked) GuiSetState(STATE_DISABLED);
    int eq_en = app->config.layers[app->active_layer].eq_enabled;
    if (GuiButton((Rectangle){(float)x,(float)y,(float)(w/2 - 3),22},
                  eq_en ? "EQ: ON" : "EQ: OFF")) {
        app->config.layers[app->active_layer].eq_enabled = !eq_en;
        ps->eq_enabled_val = !eq_en;
    }
    if (GuiButton((Rectangle){(float)(x + w/2 + 3),(float)y,(float)(w/2 - 3),22},
                  "EQ Settings"))
        ps->eq_open = !ps->eq_open;
    if (any_locked) GuiSetState(STATE_NORMAL);

#ifndef HAVE_PULSEAUDIO
    y += 24;
    DrawText("(EQ requires PulseAudio — recompile with libpulse-simple)",
             x, y, THEME_FONT_SMALL, THEME_TEXT_DIM);
#endif
}

/* ── EQ settings overlay ────────────────────────────────────────────────────── */

static void draw_eq_panel(AppState *app, PanelState *ps) {
    int x = EQ_PANEL_X, y = EQ_PANEL_Y, w = EQ_PANEL_W;

    /* Dim background */
    DrawRectangle(0, 0, WIN_W, WIN_H, (Color){0,0,0,110});

    DrawRectangle(x, y, w, EQ_PANEL_H, (Color){28,28,36,255});
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,EQ_PANEL_H},
                         1, THEME_BTN_BORDER_HOV);

    DrawText(TextFormat("EQUALIZER SETTINGS  (layer %d)", app->active_layer + 1),
             x + PAD, y + 8, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    if (GuiButton((Rectangle){(float)(x+w-30),(float)(y+4),24,24}, "X"))
        ps->eq_open = 0;
    y += 34;

    int li = app->active_layer;
    LayerCfg *layer = &app->config.layers[li];

    int ix = x + PAD, iw = w - PAD * 2;

    /* Enabled toggle */
    if (GuiButton((Rectangle){(float)ix,(float)y,(float)(iw/2 - 2),22},
                  ps->eq_enabled_val ? "Enabled: YES" : "Enabled: NO")) {
        ps->eq_enabled_val   = !ps->eq_enabled_val;
        layer->eq_enabled    = ps->eq_enabled_val;
    }
    y += 28;

    /* Position row: X, Y */
    DrawText("Position X:", ix, y + 3, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    int lbl_w = MeasureText("Position X:", THEME_FONT_LABEL) + 6;
    if (GuiTextBox((Rectangle){(float)(ix + lbl_w),(float)y,40,22},
                   ps->eq_x_buf, (int)sizeof(ps->eq_x_buf), ps->eq_x_edit))
        ps->eq_x_edit = !ps->eq_x_edit;
    textbox_clipboard(ps->eq_x_buf, sizeof(ps->eq_x_buf), ps->eq_x_edit);

    int x2 = ix + lbl_w + 48;
    DrawText("Y:", x2, y + 3, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    int lw2 = MeasureText("Y:", THEME_FONT_LABEL) + 4;
    if (GuiTextBox((Rectangle){(float)(x2 + lw2),(float)y,40,22},
                   ps->eq_y_buf, (int)sizeof(ps->eq_y_buf), ps->eq_y_edit))
        ps->eq_y_edit = !ps->eq_y_edit;
    textbox_clipboard(ps->eq_y_buf, sizeof(ps->eq_y_buf), ps->eq_y_edit);
    y += 28;

    /* Size row: W, H */
    DrawText("Width:", ix, y + 3, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    int wlbl_w = MeasureText("Width:", THEME_FONT_LABEL) + 6;
    if (GuiTextBox((Rectangle){(float)(ix + wlbl_w),(float)y,40,22},
                   ps->eq_w_buf, (int)sizeof(ps->eq_w_buf), ps->eq_w_edit))
        ps->eq_w_edit = !ps->eq_w_edit;
    textbox_clipboard(ps->eq_w_buf, sizeof(ps->eq_w_buf), ps->eq_w_edit);

    int x3 = ix + wlbl_w + 48;
    DrawText("Height:", x3, y + 3, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    int hlbl_w = MeasureText("Height:", THEME_FONT_LABEL) + 4;
    if (GuiTextBox((Rectangle){(float)(x3 + hlbl_w),(float)y,40,22},
                   ps->eq_h_buf, (int)sizeof(ps->eq_h_buf), ps->eq_h_edit))
        ps->eq_h_edit = !ps->eq_h_edit;
    textbox_clipboard(ps->eq_h_buf, sizeof(ps->eq_h_buf), ps->eq_h_edit);
    y += 28;

    /* Color dropdown placeholder (drawn after other controls) */
    DrawText("Color:", ix, y + 3, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    int clbl_w = MeasureText("Color:", THEME_FONT_LABEL) + 6;
    ps->eq_color_rect = (Rectangle){(float)(ix + clbl_w),(float)y,(float)(iw - clbl_w),22};
    y += 28;

    /* Audio device */
    DrawText("Audio device:", ix, y + 3, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
    y += 18;
    if (GuiTextBox((Rectangle){(float)ix,(float)y,(float)iw,22},
                   ps->eq_device_buf, (int)sizeof(ps->eq_device_buf), ps->eq_dev_edit))
        ps->eq_dev_edit = !ps->eq_dev_edit;
    textbox_clipboard(ps->eq_device_buf, sizeof(ps->eq_device_buf), ps->eq_dev_edit);
    DrawText("(empty = default source, or e.g. alsa_output.*.monitor)",
             ix, y + 24, THEME_FONT_SMALL, THEME_TEXT_DIM);
    y += 44;

    /* Apply button */
    if (GuiButton((Rectangle){(float)(ix + iw - 80),(float)y,80,24}, "Apply")) {
        layer->eq_enabled = ps->eq_enabled_val;
        layer->eq_x       = atoi(ps->eq_x_buf);
        layer->eq_y       = atoi(ps->eq_y_buf);
        layer->eq_width   = atoi(ps->eq_w_buf);
        layer->eq_height  = atoi(ps->eq_h_buf);
        layer->eq_color   = ps->eq_color_active;
        strncpy(layer->eq_device, ps->eq_device_buf, sizeof(layer->eq_device) - 1);
        snprintf(app->status_msg, sizeof(app->status_msg),
                 "EQ settings applied for layer %d. Restart to take effect.",
                 li + 1);
    }
}

/* ── Config panel ────────────────────────────────────────────────────────────── */

static void draw_cfg_panel(AppState *app, PanelState *ps) {
    bool any_locked = (ps->open_dropdown != DROPDOWN_NONE)
                   || ps->gif_fb_visible || ps->options_open || ps->eq_open;

    int y = CFG_PANEL_Y;
    int x = RPANEL_X;
    int w = RPANEL_W;

    DrawText("CONFIG", x, y, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    y += 18;

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
    textbox_clipboard(ps->config_path_buf, sizeof(ps->config_path_buf), ps->config_path_edit);
    if (any_locked) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){(float)(x + w - BROWSE_BTN_W),(float)y,
                               BROWSE_BTN_W, 22}, "..."))
        ps->cfg_fb_visible = !ps->cfg_fb_visible;
    if (any_locked) GuiSetState(STATE_NORMAL);
    y += 28;

    float hw = (float)(w / 2 - 3);
    if (any_locked) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){(float)x,(float)y,hw,24}, "Load Config")) {
        snprintf(app->config_path, sizeof(app->config_path), "%s", ps->config_path_buf);
        app_reload_config(app);
        ps->last_sel        = -2;
        ps->last_edit_layer = -1;
        ps->last_gif_layer  = -1;
    }
    if (GuiButton((Rectangle){(float)(x + w/2 + 3),(float)y,hw,24}, "Save Config")) {
        if (ps->last_sel >= 0) {
            char assembled[MAX_ACTION_LEN];
            assemble_action(assembled, sizeof(assembled),
                            ps->action_type_active, ps->action_value);
            app_set_button_action(app, ps->last_sel, assembled);
            app_set_button_repeat(app, ps->last_sel, ps->repeat_on_hold_val,
                                  atoi(ps->hold_delay_buf), atoi(ps->repeat_interval_buf));
        }
        app_save_config(app);
    }
    if (any_locked) GuiSetState(STATE_NORMAL);
    y += 30;

    /* Delete current layer (greyed out if only 1 layer) */
    if (app->config.num_layers <= 1 || any_locked) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){(float)x,(float)y,(float)w,22},
                  TextFormat("Del Layer %d", app->active_layer + 1))) {
        app_delete_layer(app, app->active_layer);
        ps->last_sel        = -2;
        ps->last_edit_layer = -1;
        ps->last_gif_layer  = -1;
    }
    if (app->config.num_layers <= 1 || any_locked) GuiSetState(STATE_NORMAL);
}

/* ── Options panel overlay ───────────────────────────────────────────────────── */

static void draw_options_panel(AppState *app, PanelState *ps) {
    int x = OPTS_X, y = OPTS_Y, w = OPTS_W;

    DrawRectangle(0, 0, WIN_W, WIN_H, (Color){0,0,0,120});
    DrawRectangle(x, y, w, OPTS_H, (Color){28,28,36,255});
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,OPTS_H},
                         1, THEME_BTN_BORDER_HOV);

    DrawText("OPTIONS", x + PAD, y + 10, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    if (GuiButton((Rectangle){(float)(x+w-30),(float)(y+6),24,24}, "X"))
        ps->options_open = 0;
    y += 36;

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
    textbox_clipboard(ps->opts.font_path, sizeof(ps->opts.font_path), ps->opts_fp_edit);
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
    textbox_clipboard(ps->opts_fs_buf, sizeof(ps->opts_fs_buf), ps->opts_fs_edit);
    y += 28;

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

    int lbl_w = MeasureText("Background:", THEME_FONT_LABEL) + 4;
    for (int i = 0; i < 4; i++) {
        DrawText(col_labels[i], ix, y + 3, THEME_FONT_LABEL, THEME_TEXT_LABEL2);
        int hx = ix + lbl_w;
        Color preview = parse_hex_color(col_bufs[i], col_current[i]);
        DrawRectangle(hx, y, 20, 22, preview);
        DrawRectangleLinesEx((Rectangle){(float)hx,(float)y,20,22}, 1, THEME_BTN_BORDER);
        GuiTextBox((Rectangle){(float)(hx+24),(float)y,(float)(iw-lbl_w-24-4),22},
                   col_bufs[i], 7, *col_edits[i]);
        textbox_clipboard(col_bufs[i], 7, *col_edits[i]);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
            CheckCollisionPointRec(GetMousePosition(),
                (Rectangle){(float)(hx+24),(float)y,(float)(iw-lbl_w-24-4),22}))
            *col_edits[i] = !(*col_edits[i]);
        y += 26;
    }

    if (GuiButton((Rectangle){(float)(ix+iw-90),(float)y,90,22}, "Apply Colors"))
        opts_apply(&ps->opts);
    y += 30;

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
    int x = GRID_X;
    int y = GIF_PANEL_Y;
    int w = RPANEL_X - PAD - GRID_X;
    int h = GIF_PANEL_H;

    DrawRectangle(x, y, w, h, (Color){20,20,26,240});
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,(float)h},
                         1, THEME_BTN_BORDER_HOV);

    DrawText("Select GIF file", x + PAD, y + 6, THEME_FONT_LABEL, THEME_TEXT_SECTION);
    if (GuiButton((Rectangle){(float)(x+w-30),(float)(y+4),24,22}, "X"))
        ps->gif_fb_visible = 0;

    int fb_y = y + 28;
    if (fb_draw(&ps->gif_fb, x, fb_y, w, h - 28, false,
                ps->gif_path, sizeof(ps->gif_path)))
        ps->gif_fb_visible = 0;
}

static void draw_cfg_popup(AppState *app, PanelState *ps) {
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
    if (fb_draw(&ps->cfg_fb, x, fb_y, w, h - 28, false,
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

    Color dot = app->running          ? THEME_DOT_RUNNING
              : app->device_connected ? THEME_DOT_IDLE
                                      : THEME_DOT_OFFLINE;
    const char *dot_lbl = app->running          ? "running"
                        : app->device_connected ? "idle"
                                                : "offline";
    DrawCircle(WIN_W - 188, HDR_H / 2, 6, dot);
    DrawText(dot_lbl, WIN_W - 177, HDR_H / 2 - 7, THEME_FONT_MID, dot);

    if (GuiButton((Rectangle){ WIN_W - PAD - 80, 10, 80, 30 }, "Options"))
        ps->options_open = !ps->options_open;

    if (!app->device_connected) {
        if (GuiButton((Rectangle){ 200, 10, 110, 30 }, "Connect"))
            app_connect(app);
    } else {
        if (GuiButton((Rectangle){ 200, 10, 110, 30 }, "Disconnect"))
            app_disconnect(app);
    }

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

    {
        char icon_path[512];
        snprintf(icon_path, sizeof(icon_path), "%sicon.png", GetApplicationDirectory());
        Image app_icon = LoadImage(icon_path);
        if (app_icon.data) { SetWindowIcon(app_icon); UnloadImage(app_icon); }
    }

    SetTargetFPS(60);
    apply_theme();

    AppState  *app = app_create();
    app_connect(app);

    int        sel = -1;
    PanelState ps  = { 0 };
    ps.last_sel        = -2;
    ps.last_edit_layer = -1;
    ps.last_gif_layer  = -1;
    ps.open_dropdown   = DROPDOWN_NONE;
    snprintf(ps.config_path_buf, sizeof(ps.config_path_buf), "%s", app->config_path);

    /* Initialise GIF / EQ panel state from layer 0 */
    sync_ps_from_layer(app, &ps);

    char home[512];
    get_home_dir(home, sizeof(home));
    fb_init(&ps.gif_fb,  home, ".gif");
    fb_init(&ps.cfg_fb,  home, NULL);
    fb_init(&ps.font_fb, home, ".ttf");

    opts_defaults(&ps.opts);
    char opts_path[512];
    gui_opts_path(app->config_path, opts_path, sizeof(opts_path));
    opts_load(&ps.opts, opts_path);
    opts_apply(&ps.opts);
    snprintf(ps.opts_fs_buf, sizeof(ps.opts_fs_buf), "%d", ps.opts.font_size);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(THEME_BG);

        bool any_open   = (ps.open_dropdown != DROPDOWN_NONE);
        bool any_locked = any_open
                       || ps.gif_fb_visible || ps.cfg_fb_visible
                       || ps.options_open   || ps.eq_open;

        if (any_locked) GuiLock();

        draw_header(app, &ps);
        draw_layer_bar(app, any_locked);
        draw_launchpad(app, &sel, any_locked);
        draw_right_panel(app, sel, &ps);
        draw_gif_panel(app, &ps);
        draw_cfg_panel(app, &ps);

        DrawRectangle(0, WIN_H - SBAR_H, WIN_W, SBAR_H, THEME_SBAR_BG);
        DrawText(app->status_msg, 10, WIN_H - SBAR_H + 6,
                 THEME_FONT_LABEL, THEME_TEXT_SBAR);

        if (any_locked) GuiUnlock();

        /* ── Draw all dropdowns ── */
        int  frame_start_dropdown = any_open ? ps.open_dropdown : DROPDOWN_NONE;
        bool dis_btn = (sel < 0);
        bool dis_gif = app->running;

        /* Closed dropdowns first */
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
        /* EQ color dropdown — only shown when EQ panel is open */
        if (ps.eq_open && ps.open_dropdown != DROPDOWN_EQ_COLOR) {
            if (any_open) GuiSetState(STATE_DISABLED);
            if (GuiDropdownBox(ps.eq_color_rect, EQ_COLOR_DROPDOWN,
                               &ps.eq_color_active, 0) && !any_open)
                ps.open_dropdown = DROPDOWN_EQ_COLOR;
            GuiSetState(STATE_NORMAL);
        }

        /* Open dropdown (drawn on top) */
        switch (frame_start_dropdown) {
        case DROPDOWN_COLOR:
            if (GuiDropdownBox(ps.color_rect, COLOUR_DROPDOWN, &ps.color_active, 1)) {
                ps.open_dropdown = DROPDOWN_NONE;
                if (sel >= 0)
                    app_set_button_color(app, sel, PALETTE[ps.color_active].vel);
            }
            break;
        case DROPDOWN_COLOR_PRESSED:
            if (GuiDropdownBox(ps.color_pressed_rect, COLOUR_DROPDOWN,
                               &ps.color_pressed_active, 1)) {
                ps.open_dropdown = DROPDOWN_NONE;
                if (sel >= 0)
                    app_set_button_color_pressed(app, sel,
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
                app->config.layers[app->active_layer].gif_mode = ps.gif_mode_active;
            }
            break;
        case DROPDOWN_EQ_COLOR:
            if (GuiDropdownBox(ps.eq_color_rect, EQ_COLOR_DROPDOWN,
                               &ps.eq_color_active, 1)) {
                ps.open_dropdown = DROPDOWN_NONE;
                app->config.layers[app->active_layer].eq_color = ps.eq_color_active;
            }
            break;
        default: break;
        }

        /* ── Popup overlays ── */
        if (ps.gif_fb_visible)  draw_gif_popup(&ps);
        if (ps.cfg_fb_visible)  draw_cfg_popup(app, &ps);
        if (ps.eq_open)         draw_eq_panel(app, &ps);
        if (ps.options_open)    draw_options_panel(app, &ps);

        /* ── Button copy / paste / cut / delete ── */
        if (sel >= 0 && !any_locked && !any_textbox_active(&ps)) {
            bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            const char *id  = button_index_to_id(sel);
            const ButtonCfg *btn = config_find_button_in_layer(
                &app->config, app->active_layer, id);

            uint8_t    cur_col  = btn ? btn->color         : app->config.default_color;
            uint8_t    cur_colp = btn ? btn->color_pressed : app->config.default_color_pressed;
            const char *cur_act = btn ? btn->action        : "";
            int cur_gif = btn ? btn->gif_overlay : 0;
            int cur_rep = btn ? btn->repeat_on_hold    : 0;
            int cur_hd  = btn ? btn->hold_delay_ms     : 500;
            int cur_ri  = btn ? btn->repeat_interval_ms: 100;

            if (ctrl && IsKeyPressed(KEY_C)) {
                ps.btn_clip_color              = cur_col;
                ps.btn_clip_color_pressed      = cur_colp;
                strncpy(ps.btn_clip_action, cur_act, MAX_ACTION_LEN - 1);
                ps.btn_clip_action[MAX_ACTION_LEN - 1] = '\0';
                ps.btn_clip_gif_overlay        = cur_gif;
                ps.btn_clip_repeat_on_hold     = cur_rep;
                ps.btn_clip_hold_delay_ms      = cur_hd;
                ps.btn_clip_repeat_interval_ms = cur_ri;
                ps.btn_clip_valid              = true;
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "Copied %s (layer %d).", id, app->active_layer + 1);

            } else if (ctrl && IsKeyPressed(KEY_X)) {
                ps.btn_clip_color              = cur_col;
                ps.btn_clip_color_pressed      = cur_colp;
                strncpy(ps.btn_clip_action, cur_act, MAX_ACTION_LEN - 1);
                ps.btn_clip_action[MAX_ACTION_LEN - 1] = '\0';
                ps.btn_clip_gif_overlay        = cur_gif;
                ps.btn_clip_repeat_on_hold     = cur_rep;
                ps.btn_clip_hold_delay_ms      = cur_hd;
                ps.btn_clip_repeat_interval_ms = cur_ri;
                ps.btn_clip_valid              = true;
                app_set_button_color(app, sel, app->config.default_color);
                app_set_button_color_pressed(app, sel, app->config.default_color_pressed);
                app_set_button_action(app, sel, "");
                app_set_button_gif_overlay(app, sel, 0);
                app_set_button_repeat(app, sel, 0, 500, 100);
                ps.last_sel = -2;
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "Cut %s (layer %d).", id, app->active_layer + 1);

            } else if (ctrl && IsKeyPressed(KEY_V) && ps.btn_clip_valid) {
                app_set_button_color(app, sel, ps.btn_clip_color);
                app_set_button_color_pressed(app, sel, ps.btn_clip_color_pressed);
                app_set_button_action(app, sel, ps.btn_clip_action);
                app_set_button_gif_overlay(app, sel, ps.btn_clip_gif_overlay);
                app_set_button_repeat(app, sel, ps.btn_clip_repeat_on_hold,
                                      ps.btn_clip_hold_delay_ms,
                                      ps.btn_clip_repeat_interval_ms);
                ps.last_sel = -2;
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "Pasted to %s (layer %d).", id, app->active_layer + 1);

            } else if (IsKeyPressed(KEY_DELETE)) {
                app_set_button_color(app, sel, app->config.default_color);
                app_set_button_color_pressed(app, sel, app->config.default_color_pressed);
                app_set_button_action(app, sel, "");
                app_set_button_gif_overlay(app, sel, 0);
                app_set_button_repeat(app, sel, 0, 500, 100);
                ps.last_sel = -2;
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "Cleared %s (layer %d).", id, app->active_layer + 1);
            }
        }

        EndDrawing();
    }

    app_destroy(app);
    CloseWindow();
    return 0;
}
