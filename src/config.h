#pragma once
#include <stdint.h>
#include <stddef.h>

#define MAX_BUTTONS    80   /* 8 top + 8 side + 64 grid */
#define MAX_ACTION_LEN 256
#define MAX_LAYERS     8

typedef struct {
    char    id[32];
    uint8_t color;
    uint8_t color_pressed;
    char    action[MAX_ACTION_LEN];
    int     gif_overlay;
    int     repeat_on_hold;
    int     hold_delay_ms;
    int     repeat_interval_ms;
} ButtonCfg;

/* Per-layer settings — one layer = one set of macros + optional animation */
typedef struct {
    /* GIF / animation */
    char    gif_path[512];
    int     gif_mode;    /* 0=full 1=red 2=green 3=yellow */
    float   fps;         /* 0 = use GIF metadata */

    /* Equalizer visualization */
    int     eq_enabled;
    int     eq_x;        /* starting column (0-8) */
    int     eq_y;        /* starting row (0-8) */
    int     eq_width;    /* number of frequency bands (columns) */
    int     eq_height;   /* number of amplitude levels (rows) */
    int     eq_color;    /* 0=green  1=red  2=yellow */
    char    eq_device[128]; /* PulseAudio source name (empty = default) */

    /* Per-layer color defaults (0 = inherit from Config globals) */
    uint8_t default_color;
    uint8_t default_color_pressed;

    /* Button configs for this layer */
    ButtonCfg buttons[MAX_BUTTONS];
    int       num_buttons;
} LayerCfg;

typedef struct {
    LayerCfg layers[MAX_LAYERS];
    int      num_layers;    /* always >= 1 */
    uint8_t  default_color;
    uint8_t  default_color_pressed;
} Config;

/* build the default config path (~/.config/launchpad-macro/launchpad.conf) into buf */
void config_default_path(char *buf, size_t size);

/*
 * Load an INI config file.  Missing file is not fatal — sensible defaults are used.
 * Format:
 *   [settings]                      global defaults
 *   [layer<N>]                      per-layer settings (N = 1-based)
 *   [layer<N>.<button_id>]          button in layer N
 *   [<button_id>]                   legacy: treated as layer 1 button
 */
int config_load(Config *cfg, const char *path);

/* Write the current config back to an INI file. Returns 0 on success, -1 on error. */
int config_save(const Config *cfg, const char *path);

/* Find a button config by id in a specific layer.  Returns NULL if not configured. */
const ButtonCfg *config_find_button_in_layer(const Config *cfg, int layer_idx, const char *id);

/* Backwards-compat wrapper: searches layer 0 */
const ButtonCfg *config_find_button(const Config *cfg, const char *id);
