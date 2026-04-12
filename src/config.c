#include "config.h"
#include "colours.h"
#include "ini.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef _WIN32
#  include <direct.h>
#  define mkdir_compat(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  define mkdir_compat(p) mkdir(p, 0755)
#endif

/* ── Section name parsing ───────────────────────────────────────────────────── */

#define SEC_SETTINGS   0
#define SEC_LAYER_HDR  1   /* [layer<N>]              */
#define SEC_LAYER_BTN  2   /* [layer<N>.<button_id>]  */
#define SEC_LEGACY_BTN 3   /* [<button_id>] — old format, maps to layer 0 */

/* Returns one of the SEC_* constants.
   layer_idx_out: 0-based layer index.
   btn_id_out:    button id string (empty for SEC_LAYER_HDR, SEC_SETTINGS). */
static int parse_section(const char *section, int *layer_idx_out, char *btn_id_out) {
    if (strcmp(section, "settings") == 0) {
        *layer_idx_out = 0;
        btn_id_out[0]  = '\0';
        return SEC_SETTINGS;
    }
    if (strncmp(section, "layer", 5) == 0 && isdigit((unsigned char)section[5])) {
        int n = atoi(section + 5);
        if (n < 1 || n > MAX_LAYERS) return -1;
        *layer_idx_out = n - 1;
        /* skip digits */
        const char *p = section + 5;
        while (isdigit((unsigned char)*p)) p++;
        if (*p == '\0') {
            btn_id_out[0] = '\0';
            return SEC_LAYER_HDR;
        }
        if (*p == '.') {
            strncpy(btn_id_out, p + 1, 31);
            btn_id_out[31] = '\0';
            return SEC_LAYER_BTN;
        }
    }
    /* Legacy: treat as layer 0 button */
    *layer_idx_out = 0;
    strncpy(btn_id_out, section, 31);
    btn_id_out[31] = '\0';
    return SEC_LEGACY_BTN;
}

/* Ensure cfg->layers[0..layer_idx] exist, initialising new ones with global defaults */
static LayerCfg *ensure_layer(Config *cfg, int layer_idx) {
    if (layer_idx < 0 || layer_idx >= MAX_LAYERS) return NULL;
    while (cfg->num_layers <= layer_idx) {
        int i = cfg->num_layers;
        memset(&cfg->layers[i], 0, sizeof(LayerCfg));
        cfg->layers[i].default_color         = cfg->default_color;
        cfg->layers[i].default_color_pressed = cfg->default_color_pressed;
        /* Sensible eq defaults */
        cfg->layers[i].eq_x      = 0;
        cfg->layers[i].eq_y      = 1;
        cfg->layers[i].eq_width  = 8;
        cfg->layers[i].eq_height = 7;
        cfg->num_layers++;
    }
    return &cfg->layers[layer_idx];
}

static ButtonCfg *find_or_create_in_layer(LayerCfg *layer,
                                           const char *id,
                                           uint8_t def_color,
                                           uint8_t def_color_pressed) {
    for (int i = 0; i < layer->num_buttons; i++)
        if (strcmp(layer->buttons[i].id, id) == 0)
            return &layer->buttons[i];
    if (layer->num_buttons >= MAX_BUTTONS) return NULL;
    ButtonCfg *btn = &layer->buttons[layer->num_buttons++];
    memset(btn, 0, sizeof(*btn));
    strncpy(btn->id, id, sizeof(btn->id) - 1);
    btn->color         = def_color;
    btn->color_pressed = def_color_pressed;
    return btn;
}

/* ── INI handler ────────────────────────────────────────────────────────────── */

static int cfg_ini_handler(void *user, const char *section,
                           const char *name, const char *value) {
    Config *cfg = (Config *)user;
    int  layer_idx = 0;
    char btn_id[32];
    int  sec_type = parse_section(section, &layer_idx, btn_id);

    /* ── global settings ── */
    if (sec_type == SEC_SETTINGS) {
        if (strcmp(name, "default_color") == 0) {
            uint8_t v = colour_velocity(value);
            if (v == COLOUR_BLACK && strcmp(value, "black") != 0)
                fprintf(stderr, "warning: unknown colour '%s' in [settings]\n", value);
            cfg->default_color = v;
        } else if (strcmp(name, "default_color_pressed") == 0) {
            uint8_t v = colour_velocity(value);
            if (v == COLOUR_BLACK && strcmp(value, "black") != 0)
                fprintf(stderr, "warning: unknown colour '%s' in [settings]\n", value);
            cfg->default_color_pressed = v;
        }
        return 1;
    }
    if (sec_type < 0) return 1; /* unknown section format, skip */

    LayerCfg *layer = ensure_layer(cfg, layer_idx);
    if (!layer) return 1;

    /* ── layer header [layer<N>] ── */
    if (sec_type == SEC_LAYER_HDR) {
        if (strcmp(name, "gif") == 0 || strcmp(name, "gif_path") == 0) {
            strncpy(layer->gif_path, value, sizeof(layer->gif_path) - 1);
        } else if (strcmp(name, "gif_mode") == 0) {
            if      (strcmp(value, "red")    == 0) layer->gif_mode = 1;
            else if (strcmp(value, "green")  == 0) layer->gif_mode = 2;
            else if (strcmp(value, "yellow") == 0) layer->gif_mode = 3;
            else                                   layer->gif_mode = 0;
        } else if (strcmp(name, "fps") == 0) {
            layer->fps = (float)atof(value);
        } else if (strcmp(name, "eq_enabled") == 0) {
            layer->eq_enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        } else if (strcmp(name, "eq_x") == 0) {
            layer->eq_x = atoi(value);
        } else if (strcmp(name, "eq_y") == 0) {
            layer->eq_y = atoi(value);
        } else if (strcmp(name, "eq_width") == 0) {
            layer->eq_width = atoi(value);
        } else if (strcmp(name, "eq_height") == 0) {
            layer->eq_height = atoi(value);
        } else if (strcmp(name, "eq_color") == 0) {
            if      (strcmp(value, "red")    == 0) layer->eq_color = 1;
            else if (strcmp(value, "yellow") == 0) layer->eq_color = 2;
            else                                   layer->eq_color = 0; /* green */
        } else if (strcmp(name, "eq_device") == 0) {
            strncpy(layer->eq_device, value, sizeof(layer->eq_device) - 1);
        } else if (strcmp(name, "default_color") == 0) {
            layer->default_color = colour_velocity(value);
        } else if (strcmp(name, "default_color_pressed") == 0) {
            layer->default_color_pressed = colour_velocity(value);
        }
        return 1;
    }

    /* ── button section [layer<N>.<btn>] or legacy [<btn>] ── */
    uint8_t dc  = layer->default_color         ? layer->default_color         : cfg->default_color;
    uint8_t dcp = layer->default_color_pressed ? layer->default_color_pressed : cfg->default_color_pressed;
    ButtonCfg *btn = find_or_create_in_layer(layer, btn_id, dc, dcp);
    if (!btn) return 1;

    if (strcmp(name, "color") == 0) {
        btn->color = colour_velocity(value);
    } else if (strcmp(name, "color_pressed") == 0) {
        btn->color_pressed = colour_velocity(value);
    } else if (strcmp(name, "action") == 0) {
        strncpy(btn->action, value, MAX_ACTION_LEN - 1);
        btn->action[MAX_ACTION_LEN - 1] = '\0';
    } else if (strcmp(name, "gif_overlay") == 0) {
        btn->gif_overlay = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(name, "repeat_on_hold") == 0) {
        btn->repeat_on_hold = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(name, "hold_delay_ms") == 0) {
        btn->hold_delay_ms = atoi(value);
    } else if (strcmp(name, "repeat_interval_ms") == 0) {
        btn->repeat_interval_ms = atoi(value);
    }
    return 1;
}

/* ── Directory helpers ──────────────────────────────────────────────────────── */

static void mkdirs(char *dir) {
#ifdef _WIN32
    char *start = dir;
    if (dir[0] && dir[1] == ':') start = dir + 2;
    for (char *p = start + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            char save = *p; *p = '\0';
            _mkdir(dir);
            *p = save;
        }
    }
    _mkdir(dir);
#else
    for (char *p = dir + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(dir, 0755);
            *p = '/';
        }
    }
    mkdir(dir, 0755);
#endif
}

static void config_create_default(const char *path) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
#ifdef _WIN32
    char *slash = strrchr(dir, '\\');
    if (!slash) slash = strrchr(dir, '/');
#else
    char *slash = strrchr(dir, '/');
#endif
    if (slash) { *slash = '\0'; mkdirs(dir); }

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "warning: could not create default config at '%s'\n", path);
        return;
    }
    fprintf(f,
        "[settings]\n"
        "default_color         = black\n"
        "default_color_pressed = green_low\n"
        "\n"
        "# Layer 1 — add your macros below\n"
        "[layer1]\n"
        "# gif = /path/to/animation.gif\n"
        "# gif_mode = full\n"
        "# fps = 0\n"
        "# eq_enabled = false\n"
        "\n"
        "# Button example:\n"
        "# [layer1.grid_0_0]\n"
        "# color         = black\n"
        "# color_pressed = red_low\n"
        "# action        = app:firefox\n"
        "#\n"
        "# action prefixes: key:<combo>  media:<key>  app:<cmd>  terminal:<cmd>\n"
        "#                  string:<text>  layer:<n>\n"
        "# colours: black  red_low red_med red_max  green_low green_med green_max\n"
        "#          yellow_low yellow_med yellow_max\n"
    );
    fclose(f);
    printf("note: created default config at '%s'\n", path);
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

void config_default_path(char *buf, size_t size) {
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (!appdata || appdata[0] == '\0') appdata = "C:\\Users\\Default\\AppData\\Roaming";
    snprintf(buf, size, "%s\\launchpad-macro\\launchpad.conf", appdata);
#else
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') home = "/tmp";
    snprintf(buf, size, "%s/.config/launchpad-macro/launchpad.conf", home);
#endif
}

int config_load(Config *cfg, const char *path) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->default_color         = COLOUR_BLACK;
    cfg->default_color_pressed = COLOUR_GREEN_LOW;
    cfg->num_layers             = 1;
    /* Initialise layer 0 */
    cfg->layers[0].default_color         = COLOUR_BLACK;
    cfg->layers[0].default_color_pressed = COLOUR_GREEN_LOW;
    cfg->layers[0].eq_x      = 0;
    cfg->layers[0].eq_y      = 1;
    cfg->layers[0].eq_width  = 8;
    cfg->layers[0].eq_height = 7;

    int err = ini_parse(path, cfg_ini_handler, cfg);
    if (err == -1) {
        printf("note: config file not found at '%s', creating default\n", path);
        config_create_default(path);
        return 0;
    }
    if (err > 0)
        fprintf(stderr, "warning: config parse error at line %d in '%s'\n", err, path);
    return 0;
}

static const char *vel_to_name(uint8_t vel) {
    for (int i = 0; i < PALETTE_COUNT; i++)
        if (PALETTE[i].vel == vel) return PALETTE[i].name;
    return "black";
}

static const char *gif_mode_name(int mode) {
    switch (mode) {
        case 1: return "red";
        case 2: return "green";
        case 3: return "yellow";
        default: return "full";
    }
}

static const char *eq_color_name(int c) {
    switch (c) {
        case 1: return "red";
        case 2: return "yellow";
        default: return "green";
    }
}

int config_save(const Config *cfg, const char *path) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
#ifdef _WIN32
    char *slash = strrchr(dir, '\\');
    if (!slash) slash = strrchr(dir, '/');
#else
    char *slash = strrchr(dir, '/');
#endif
    if (slash) { *slash = '\0'; mkdirs(dir); }

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "[settings]\n");
    fprintf(f, "default_color         = %s\n",   vel_to_name(cfg->default_color));
    fprintf(f, "default_color_pressed = %s\n\n", vel_to_name(cfg->default_color_pressed));

    for (int li = 0; li < cfg->num_layers; li++) {
        const LayerCfg *layer = &cfg->layers[li];
        fprintf(f, "[layer%d]\n", li + 1);
        if (layer->gif_path[0])
            fprintf(f, "gif                   = %s\n", layer->gif_path);
        if (layer->gif_mode > 0)
            fprintf(f, "gif_mode              = %s\n", gif_mode_name(layer->gif_mode));
        if (layer->fps > 0.0f)
            fprintf(f, "fps                   = %.2f\n", layer->fps);
        if (layer->eq_enabled) {
            fprintf(f, "eq_enabled            = true\n");
            fprintf(f, "eq_x                  = %d\n", layer->eq_x);
            fprintf(f, "eq_y                  = %d\n", layer->eq_y);
            fprintf(f, "eq_width              = %d\n", layer->eq_width);
            fprintf(f, "eq_height             = %d\n", layer->eq_height);
            fprintf(f, "eq_color              = %s\n", eq_color_name(layer->eq_color));
            if (layer->eq_device[0])
                fprintf(f, "eq_device             = %s\n", layer->eq_device);
        }
        fprintf(f, "\n");

        for (int i = 0; i < layer->num_buttons; i++) {
            const ButtonCfg *btn = &layer->buttons[i];
            fprintf(f, "[layer%d.%s]\n", li + 1, btn->id);
            fprintf(f, "color         = %s\n", vel_to_name(btn->color));
            fprintf(f, "color_pressed = %s\n", vel_to_name(btn->color_pressed));
            if (btn->action[0])
                fprintf(f, "action        = %s\n", btn->action);
            if (btn->gif_overlay)
                fprintf(f, "gif_overlay   = true\n");
            if (btn->repeat_on_hold) {
                fprintf(f, "repeat_on_hold     = true\n");
                fprintf(f, "hold_delay_ms      = %d\n",
                        btn->hold_delay_ms      > 0 ? btn->hold_delay_ms      : 500);
                fprintf(f, "repeat_interval_ms = %d\n",
                        btn->repeat_interval_ms > 0 ? btn->repeat_interval_ms : 100);
            }
            fprintf(f, "\n");
        }
    }

    fclose(f);
    return 0;
}

const ButtonCfg *config_find_button_in_layer(const Config *cfg, int layer_idx,
                                              const char *id) {
    if (layer_idx < 0 || layer_idx >= cfg->num_layers) return NULL;
    const LayerCfg *layer = &cfg->layers[layer_idx];
    for (int i = 0; i < layer->num_buttons; i++)
        if (strcmp(layer->buttons[i].id, id) == 0)
            return &layer->buttons[i];
    return NULL;
}

const ButtonCfg *config_find_button(const Config *cfg, const char *id) {
    return config_find_button_in_layer(cfg, 0, id);
}
