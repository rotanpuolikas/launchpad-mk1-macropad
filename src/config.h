#pragma once
#include <stdint.h>
#include <stddef.h>

#define MAX_BUTTONS    80   // 8 top + 8 side + 64 grid
#define MAX_ACTION_LEN 256

typedef struct {
    char    id[32];
    uint8_t color;
    uint8_t color_pressed;
    char    action[MAX_ACTION_LEN];
    int     gif_overlay;
} ButtonCfg;

typedef struct {
    ButtonCfg buttons[MAX_BUTTONS];
    int       num_buttons;
    uint8_t   default_color;
    uint8_t   default_color_pressed;
} Config;

// build the default config path (~/.config/launchpad-macro/launchpad.conf) into buf
void config_default_path(char *buf, size_t size);

/*
 * load an INI config file, missing file is not fatal, sensible defaults are used
 * returns 0 on success, -1 on hard parse error
 */
int config_load(Config *cfg, const char *path);

// find a button config by id, returns NULL if not configured
const ButtonCfg *config_find_button(const Config *cfg, const char *id);

// write the current config back to an INI file, returns 0 on success, -1 on error
int config_save(const Config *cfg, const char *path);
