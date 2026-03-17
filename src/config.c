#include "config.h"
#include "colours.h"
#include "ini.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <direct.h>
#  define mkdir_compat(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  define mkdir_compat(p) mkdir(p, 0755)
#endif

static ButtonCfg *find_or_create(Config *cfg, const char *section) {
    for (int i = 0; i < cfg->num_buttons; i++)
        if (strcmp(cfg->buttons[i].id, section) == 0)
            return &cfg->buttons[i];
    if (cfg->num_buttons >= MAX_BUTTONS) return NULL;
    ButtonCfg *btn = &cfg->buttons[cfg->num_buttons++];
    memset(btn, 0, sizeof(*btn));
    strncpy(btn->id, section, sizeof(btn->id) - 1);
    btn->color         = cfg->default_color;
    btn->color_pressed = cfg->default_color_pressed;
    return btn;
}

static int cfg_ini_handler(void *user, const char *section, const char *name, const char *value) {
    Config *cfg = (Config *)user;

    if (strcmp(section, "settings") == 0) {
        if (strcmp(name, "default_color") == 0) {
            uint8_t v = colour_velocity(value);
            if (v == COLOUR_BLACK && strcmp(value, "black") != 0)
                fprintf(stderr, "warning: unknown colour '%s' in [settings] default_color\n", value);
            cfg->default_color = v;
        } else if (strcmp(name, "default_color_pressed") == 0) {
            uint8_t v = colour_velocity(value);
            if (v == COLOUR_BLACK && strcmp(value, "black") != 0)
                fprintf(stderr, "warning: unknown colour '%s' in [settings] default_color_pressed\n", value);
            cfg->default_color_pressed = v;
        }
        return 1;
    }

    ButtonCfg *btn = find_or_create(cfg, section);
    if (!btn) return 1;  // silently ignore overflow

    if (strcmp(name, "color") == 0) {
        btn->color = colour_velocity(value);
    } else if (strcmp(name, "color_pressed") == 0) {
        btn->color_pressed = colour_velocity(value);
    } else if (strcmp(name, "action") == 0) {
        strncpy(btn->action, value, MAX_ACTION_LEN - 1);
    } else if (strcmp(name, "gif_overlay") == 0) {
        btn->gif_overlay = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    }
    return 1;
}

// create all intermediate directories in path (mkdir -p equivalent)
static void mkdirs(char *dir) {
#ifdef _WIN32
    // skip drive letter prefix ("C:\")
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
    if (slash) {
        *slash = '\0';
        mkdirs(dir);
    }

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
        "# Button configuration example:\n"
        "# [A1]\n"
        "# color         = black\n"
        "# color_pressed = red_low\n"
        "# action        = app:firefox\n"
        "#\n"
        "# action prefixes: key:<combo>  media:<key>  app:<shell command>\n"
        "# colours: black red_low red_med red_max green_low green_med green_max\n"
        "#          yellow_low yellow_med yellow_max\n"
    );
    fclose(f);
    printf("note: created default config at '%s'\n", path);
}

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
    // defaults matching launchpad.conf
    cfg->default_color         = COLOUR_BLACK;
    cfg->default_color_pressed = COLOUR_GREEN_LOW;

    int err = ini_parse(path, cfg_ini_handler, cfg);
    if (err == -1) {
        printf("note: config file not found at '%s', creating default\n", path);
        config_create_default(path);
        return 0;
    }
    if (err > 0) {
        fprintf(stderr, "warning: config parse error at line %d in '%s'\n", err, path);
    }
    return 0;
}

const ButtonCfg *config_find_button(const Config *cfg, const char *id) {
    for (int i = 0; i < cfg->num_buttons; i++)
        if (strcmp(cfg->buttons[i].id, id) == 0)
            return &cfg->buttons[i];
    return NULL;
}
