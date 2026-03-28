#pragma once
#include <pthread.h>
#include <stdint.h>

#include "../midi.h"
#include "../keys.h"
#include "../config.h"
#include "../gif.h"

/* Button index layout (BUTTON_COUNT = 81):
 *   0..8   = top_0  .. top_8   (top row, 9 buttons)
 *   9..72  = grid_0_0 .. grid_7_7  (8×8 grid, row-major)
 *   73..80 = side_0 .. side_7  (right column, 8 buttons)
 */
#define BUTTON_COUNT 81

typedef struct {
    Launchpad   lp;
    Keys       *keys;
    Config      config;

    GifData    *gif;
    int         gif_mode;   /* 0=full 1=red 2=green 3=yellow */

    pthread_t   gif_thread;
    pthread_t   event_thread;
    int         gif_thread_running;
    int         event_thread_running;

    pthread_mutex_t mutex;
    volatile int    stop_requested;
    volatile int    gif_stop;

    /* protected buttons for GIF overlay (populated at start time) */
    int prot_row[MAX_BUTTONS];
    int prot_col[MAX_BUTTONS];
    int nprot;

    /* settings */
    char  config_path[512];
    char  gif_path[512];
    float fps;              /* 0 = use GIF metadata */

    /* display state — read by GUI, written by event/gif threads, mutex-protected */
    uint8_t led_state[BUTTON_COUNT];

    /* status */
    int  device_connected;
    int  running;
    char status_msg[256];
} AppState;

AppState   *app_create(void);
void        app_destroy(AppState *app);

int         app_connect(AppState *app);     /* 0=ok, -1=fail */
void        app_disconnect(AppState *app);

int         app_start(AppState *app);       /* 0=ok, -1=fail */
void        app_stop(AppState *app);

int         app_load_gif(AppState *app, const char *path);  /* 0=ok, -1=fail */
void        app_clear_gif(AppState *app);
void        app_reload_config(AppState *app);
void        app_save_config(AppState *app);

/* in-memory config editing (creates button entry if not present) */
void        app_set_button_color(AppState *app, int btn_idx, uint8_t color);
void        app_set_button_color_pressed(AppState *app, int btn_idx, uint8_t color_pressed);
void        app_set_button_action(AppState *app, int btn_idx, const char *action);
void        app_set_button_gif_overlay(AppState *app, int btn_idx, int overlay);
void        app_set_button_repeat(AppState *app, int btn_idx, int on_hold, int hold_delay_ms, int repeat_interval_ms);

/* button index <-> id string helpers */
const char *button_index_to_id(int idx);        /* "top_0", "grid_3_4", "side_2" */
int         button_id_to_index(const char *id); /* -1 on error */

/* MIDI velocity → approximate RGB [0..255] */
void        vel_to_rgb_bytes(uint8_t vel, uint8_t *r, uint8_t *g, uint8_t *b);
