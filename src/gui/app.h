#pragma once
#include <pthread.h>
#include <stdint.h>

#include "../midi.h"
#include "../keys.h"
#include "../config.h"
#include "../gif.h"
#include "../audio.h"

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

    /* Active layer (0-based).  Both hardware and GUI follow this. */
    int         active_layer;
    volatile int layer_switch_flag;  /* set by execute_action, cleared by gif_thread */

#ifdef HAVE_GIF
    /* Pre-loaded GIFs, one per layer (NULL = no GIF for that layer) */
    GifData    *layer_gif[MAX_LAYERS];
#endif

#ifdef HAVE_PULSEAUDIO
    AudioCapture *audio;
    pthread_t     eq_thread;
    int           eq_thread_running;
#endif

    pthread_t   gif_thread;
    pthread_t   event_thread;
    int         gif_thread_running;
    int         event_thread_running;

    pthread_mutex_t mutex;
    volatile int    stop_requested;
    volatile int    gif_stop;

    /* Protected buttons for GIF overlay (rebuilt on layer switch) */
    int prot_row[MAX_BUTTONS];
    int prot_col[MAX_BUTTONS];
    int nprot;

    /* Settings */
    char config_path[512];

    /* Display state — read by GUI, written by event/gif/eq threads, mutex-protected */
    uint8_t led_state[BUTTON_COUNT];

    /* Status */
    int  device_connected;
    int  running;
    char status_msg[256];
} AppState;

AppState   *app_create(void);
void        app_destroy(AppState *app);

int         app_connect(AppState *app);
void        app_disconnect(AppState *app);

int         app_start(AppState *app);
void        app_stop(AppState *app);

/* Load/clear GIF for the given layer (0-based).  Use active_layer if layer<0. */
int         app_load_layer_gif(AppState *app, int layer, const char *path);
void        app_clear_layer_gif(AppState *app, int layer);

void        app_reload_config(AppState *app);
void        app_save_config(AppState *app);

/* Switch the active layer (0-based).  Safe to call from any thread. */
void        app_switch_layer(AppState *app, int layer_idx);

/* Add a new empty layer.  Returns the new layer index, or -1 on failure. */
int         app_add_layer(AppState *app);

/* Delete a layer by index.  Active layer is clamped after deletion. */
void        app_delete_layer(AppState *app, int layer_idx);

/* In-memory config editing — always operates on the active layer */
void        app_set_button_color(AppState *app, int btn_idx, uint8_t color);
void        app_set_button_color_pressed(AppState *app, int btn_idx, uint8_t color_pressed);
void        app_set_button_action(AppState *app, int btn_idx, const char *action);
void        app_set_button_gif_overlay(AppState *app, int btn_idx, int overlay);
void        app_set_button_repeat(AppState *app, int btn_idx, int on_hold,
                                  int hold_delay_ms, int repeat_interval_ms);

/* Button index <-> id string helpers */
const char *button_index_to_id(int idx);
int         button_id_to_index(const char *id);

/* MIDI velocity → approximate RGB [0..255] */
void        vel_to_rgb_bytes(uint8_t vel, uint8_t *r, uint8_t *g, uint8_t *b);
