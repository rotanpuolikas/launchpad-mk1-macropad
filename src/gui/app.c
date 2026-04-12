#include "app.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  define sleep_ms(ms) Sleep(ms)
#else
#  include <unistd.h>
#  include <sys/wait.h>
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

/* ── Button ID helpers ──────────────────────────────────────────────────────── */

static char s_ids[BUTTON_COUNT][16];
static int  s_ids_init = 0;

static void init_ids(void) {
    if (s_ids_init) return;
    for (int i = 0; i < 9; i++)
        snprintf(s_ids[i], 16, "top_%d", i);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            snprintf(s_ids[9 + r * 8 + c], 16, "grid_%d_%d", r, c);
    for (int i = 0; i < 8; i++)
        snprintf(s_ids[73 + i], 16, "side_%d", i);
    s_ids_init = 1;
}

const char *button_index_to_id(int idx) {
    init_ids();
    if (idx < 0 || idx >= BUTTON_COUNT) return "?";
    return s_ids[idx];
}

int button_id_to_index(const char *id) {
    init_ids();
    for (int i = 0; i < BUTTON_COUNT; i++)
        if (strcmp(s_ids[i], id) == 0) return i;
    return -1;
}

/* row=0, col=0..8 → top; row=1..8, col=8 → side; row=1..8, col=0..7 → grid */
static int rc_to_index(int row, int col) {
    if (row == 0)                        return col;
    if (col == 8 && row >= 1 && row < 9) return 73 + (row - 1);
    if (row >= 1 && col < 8)             return 9 + (row - 1) * 8 + col;
    return -1;
}

void vel_to_rgb_bytes(uint8_t vel, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = (uint8_t)((vel & 0x03) * 85);
    *g = (uint8_t)(((vel >> 4) & 0x03) * 85);
    *b = 0;
}

/* ── Action execution ───────────────────────────────────────────────────────── */

static void execute_action(AppState *app, const char *action) {
    if (!action || !action[0]) return;

    if (strncmp(action, "layer:", 6) == 0) {
        int n = atoi(action + 6);   /* 1-based in config */
        if (n >= 1) app_switch_layer(app, n - 1);
        return;
    }

    Keys *keys = app->keys;

    if (strncmp(action, "key:", 4) == 0) {
        keys_send_combo(keys, action + 4);
    } else if (strncmp(action, "media:", 6) == 0) {
        keys_send_media(keys, action + 6);
    } else if (strncmp(action, "app:", 4) == 0) {
#ifdef _WIN32
        char cmd_line[512];
        snprintf(cmd_line, sizeof(cmd_line), "cmd.exe /c %s", action + 4);
        STARTUPINFOA si; PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));
        if (CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE,
                           DETACHED_PROCESS | CREATE_NEW_CONSOLE,
                           NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
#else
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", action + 4, NULL);
            _exit(127);
        } else if (pid > 0) {
            waitpid(pid, NULL, WNOHANG);
        }
#endif
    } else if (strncmp(action, "string:", 7) == 0) {
        keys_send_string(keys, action + 7);
    } else if (strncmp(action, "terminal:", 9) == 0) {
        const char *cmd = action + 9;
#ifdef _WIN32
        char cmd_line[512];
        snprintf(cmd_line, sizeof(cmd_line),
                 "powershell.exe -NoExit -Command \"%s\"", cmd);
        STARTUPINFOA si; PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));
        if (CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE,
                           CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
#else
        size_t len = strlen(cmd);
        const char *p = cmd + len - 1;
        while (p >= cmd && (*p == ' ' || *p == '\t')) p--;
        int background = (p >= cmd && *p == '&');
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            if (background) {
                char bg_cmd[512];
                size_t clen = (size_t)(p - cmd);
                if (clen >= sizeof(bg_cmd)) clen = sizeof(bg_cmd) - 1;
                strncpy(bg_cmd, cmd, clen);
                bg_cmd[clen] = '\0';
                execl("/bin/sh", "sh", "-c", bg_cmd, NULL);
            } else {
                const char *term = getenv("TERMINAL");
                if (term && term[0])
                    execlp(term,             term,             "-e", "sh", "-c", cmd, NULL);
                execlp("ghostty",        "ghostty",        "-e", "sh", "-c", cmd, NULL);
                execlp("alacritty",      "alacritty",      "-e", "sh", "-c", cmd, NULL);
                execlp("kitty",          "kitty",          "sh", "-c", cmd, NULL);
                execlp("foot",           "foot",           "sh", "-c", cmd, NULL);
                execlp("wezterm",        "wezterm",        "start", "--", "sh", "-c", cmd, NULL);
                execlp("xterm",          "xterm",          "-e", "sh", "-c", cmd, NULL);
                execlp("konsole",        "konsole",        "-e", "sh", "-c", cmd, NULL);
                execlp("gnome-terminal", "gnome-terminal", "--", "sh", "-c", cmd, NULL);
            }
            _exit(127);
        } else if (pid > 0) {
            waitpid(pid, NULL, WNOHANG);
        }
#endif
    }
}

/* ── Protected-button helpers ───────────────────────────────────────────────── */

static int is_protected(int row, int col, const AppState *app) {
    for (int i = 0; i < app->nprot; i++)
        if (app->prot_row[i] == row && app->prot_col[i] == col) return 1;
    return 0;
}

/* Rebuild the protected list from the active layer's config + active eq region */
static void rebuild_protected(AppState *app) {
    app->nprot = 0;
    int li = app->active_layer;
    if (li >= app->config.num_layers) return;
    const LayerCfg *layer = &app->config.layers[li];

    /* gif_overlay buttons */
    for (int i = 0; i < layer->num_buttons; i++) {
        if (!layer->buttons[i].gif_overlay) continue;
        int row, col;
        if (lp_id_to_rc(layer->buttons[i].id, &row, &col) &&
            app->nprot < MAX_BUTTONS) {
            app->prot_row[app->nprot] = row;
            app->prot_col[app->nprot] = col;
            app->nprot++;
        }
    }

    /* equalizer region buttons */
    if (layer->eq_enabled) {
        int w = layer->eq_width  > 0 ? layer->eq_width  : 8;
        int h = layer->eq_height > 0 ? layer->eq_height : 7;
        for (int bc = 0; bc < w && app->nprot < MAX_BUTTONS; bc++) {
            for (int br = 0; br < h && app->nprot < MAX_BUTTONS; br++) {
                app->prot_row[app->nprot] = layer->eq_y + br;
                app->prot_col[app->nprot] = layer->eq_x + bc;
                app->nprot++;
            }
        }
    }
}

/* ── GIF thread ─────────────────────────────────────────────────────────────── */

#ifdef HAVE_GIF
static void *gif_thread_func(void *arg) {
    AppState *app = (AppState *)arg;
    int fi        = 0;
    int cur_layer = app->active_layer;

    while (!app->gif_stop) {
        /* Check for a layer switch */
        pthread_mutex_lock(&app->mutex);
        if (app->layer_switch_flag) {
            app->layer_switch_flag = 0;
            cur_layer = app->active_layer;
            fi        = 0;
        }
        pthread_mutex_unlock(&app->mutex);

        GifData *gif = app->layer_gif[cur_layer];
        if (!gif || gif->count == 0) {
            sleep_ms(50);
            continue;
        }
        if (fi >= gif->count) fi = 0;

        const uint8_t *frame = gif->frames[fi];

        /* Render non-protected pixels to device and led_state */
        for (int row = 0; row < 9; row++)
            for (int col = 0; col < 9; col++) {
                if (is_protected(row, col, app)) continue;
                lp_set_rc(&app->lp, row, col, frame[row * 9 + col]);
            }

        pthread_mutex_lock(&app->mutex);
        for (int row = 0; row < 9; row++)
            for (int col = 0; col < 9; col++) {
                if (is_protected(row, col, app)) continue;
                int idx = rc_to_index(row, col);
                if (idx >= 0) app->led_state[idx] = frame[row * 9 + col];
            }
        pthread_mutex_unlock(&app->mutex);

        int ms = gif->delays[fi];
        for (int t = 0; t < ms && !app->gif_stop; ) {
            /* Also check for layer switch mid-frame */
            if (app->layer_switch_flag) break;
            int chunk = (ms - t < 10) ? (ms - t) : 10;
            sleep_ms(chunk);
            t += chunk;
        }
        fi++;
    }
    return NULL;
}
#endif /* HAVE_GIF */

/* ── Equalizer thread ───────────────────────────────────────────────────────── */

#ifdef HAVE_PULSEAUDIO
#include "../audio.h"

/* Map a normalised amplitude [0,1] and level index to a velocity byte */
static uint8_t eq_level_vel(float amplitude, int level, int total_levels, int eq_color) {
    /* Pixel is lit if its level is within the current amplitude */
    float threshold = (float)(total_levels - 1 - level) / (float)total_levels;
    if (amplitude <= threshold) return 0x00; /* off */

    /* Color gradients: green, red, yellow — brightest at top of bar */
    float frac = amplitude; /* brighter = higher signal */
    (void)frac;

    /* Three brightness tiers based on height within lit section */
    float bar_frac = ((float)(total_levels - 1 - level) / (float)total_levels) / (amplitude + 1e-6f);
    int tier = (int)(bar_frac * 3.0f); /* 0=top(bright), 1=mid, 2=bottom(dim) */
    if (tier > 2) tier = 2;

    switch (eq_color) {
        case 1: /* red */
            return (uint8_t[]){ 0x03, 0x02, 0x01 }[tier];
        case 2: /* yellow */
            return (uint8_t[]){ 0x33, 0x22, 0x11 }[tier];
        default: /* green */
            return (uint8_t[]){ 0x30, 0x20, 0x10 }[tier];
    }
}

static void *eq_thread_func(void *arg) {
    AppState *app = (AppState *)arg;

    while (!app->stop_requested) {
        int li = app->active_layer;
        if (li >= app->config.num_layers) { sleep_ms(50); continue; }

        const LayerCfg *layer = &app->config.layers[li];
        if (!layer->eq_enabled || !app->audio) { sleep_ms(50); continue; }

        int eq_w = layer->eq_width  > 0 ? layer->eq_width  : 8;
        int eq_h = layer->eq_height > 0 ? layer->eq_height : 7;
        int eq_x = layer->eq_x;
        int eq_y = layer->eq_y;

        /* Get band amplitudes from audio (max eq_w bands) */
        float bands[AUDIO_MAX_BANDS] = {0};
        int n_bands = eq_w < AUDIO_MAX_BANDS ? eq_w : AUDIO_MAX_BANDS;
        audio_get_bands(app->audio, bands, n_bands);

        /* Render each band column */
        for (int bc = 0; bc < eq_w; bc++) {
            float amp   = bc < AUDIO_MAX_BANDS ? bands[bc] : 0.0f;
            int   col   = eq_x + bc;
            if (col < 0 || col >= 9) continue;

            for (int br = 0; br < eq_h; br++) {
                int row = eq_y + br;
                if (row < 0 || row >= 9) continue;

                uint8_t vel = eq_level_vel(amp, br, eq_h, layer->eq_color);
                lp_set_rc(&app->lp, row, col, vel);
                int idx = rc_to_index(row, col);
                if (idx >= 0) {
                    pthread_mutex_lock(&app->mutex);
                    app->led_state[idx] = vel;
                    pthread_mutex_unlock(&app->mutex);
                }
            }
        }

        sleep_ms(30); /* ~33 fps refresh rate */
    }
    return NULL;
}
#endif /* HAVE_PULSEAUDIO */

/* ── Event thread ───────────────────────────────────────────────────────────── */

typedef struct {
    int             active;
    struct timespec press_time;
    struct timespec last_fire;
    int             repeat_started;
} HeldSlot;

static long timespec_diff_ms(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000L + (b.tv_nsec - a.tv_nsec) / 1000000L;
}

static void *event_thread_func(void *arg) {
    AppState *app = (AppState *)arg;
    HeldSlot held[BUTTON_COUNT];
    memset(held, 0, sizeof(held));

    while (!app->stop_requested) {
        ButtonEvent ev;
        int r = lp_poll(&app->lp, &ev, 10);
        if (r < 0) break;

        if (r > 0) {
            int li = app->active_layer;
            const ButtonCfg *btn = config_find_button_in_layer(&app->config, li, ev.id);
            uint8_t idle_c    = btn ? btn->color         : app->config.default_color;
            uint8_t pressed_c = btn ? btn->color_pressed : app->config.default_color_pressed;
            int is_overlay    = btn && btn->gif_overlay;
#ifdef HAVE_GIF
            int show_feedback = !app->layer_gif[li] || is_overlay;
#else
            int show_feedback = 1;
            (void)is_overlay;
#endif

            int idx = button_id_to_index(ev.id);

            if (ev.pressed) {
                if (show_feedback) lp_set_button(&app->lp, ev.id, pressed_c);
                if (btn && btn->action[0] && app->keys)
                    execute_action(app, btn->action);
                if (idx >= 0) {
                    pthread_mutex_lock(&app->mutex);
                    app->led_state[idx] = pressed_c;
                    pthread_mutex_unlock(&app->mutex);
                    if (btn && btn->repeat_on_hold) {
                        clock_gettime(CLOCK_MONOTONIC, &held[idx].press_time);
                        held[idx].last_fire      = held[idx].press_time;
                        held[idx].active         = 1;
                        held[idx].repeat_started = 0;
                    }
                }
            } else {
                if (show_feedback) lp_set_button(&app->lp, ev.id, idle_c);
                if (idx >= 0) {
                    pthread_mutex_lock(&app->mutex);
                    app->led_state[idx] = idle_c;
                    pthread_mutex_unlock(&app->mutex);
                    held[idx].active = 0;
                }
            }
        }

        /* Repeat-on-hold */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        for (int i = 0; i < BUTTON_COUNT; i++) {
            if (!held[i].active) continue;
            int li = app->active_layer;
            const ButtonCfg *btn = config_find_button_in_layer(
                &app->config, li, button_index_to_id(i));
            if (!btn || !btn->repeat_on_hold || !btn->action[0] || !app->keys) {
                held[i].active = 0; continue;
            }
            int hold_delay = btn->hold_delay_ms      > 0 ? btn->hold_delay_ms      : 500;
            int interval   = btn->repeat_interval_ms > 0 ? btn->repeat_interval_ms : 100;
            if (!held[i].repeat_started) {
                if (timespec_diff_ms(held[i].press_time, now) >= hold_delay) {
                    held[i].repeat_started = 1;
                    held[i].last_fire      = now;
                    execute_action(app, btn->action);
                }
            } else {
                if (timespec_diff_ms(held[i].last_fire, now) >= interval) {
                    held[i].last_fire = now;
                    execute_action(app, btn->action);
                }
            }
        }
    }
    return NULL;
}

/* ── LED state sync ─────────────────────────────────────────────────────────── */

static void sync_led_from_config(AppState *app) {
    int li = app->active_layer;
    if (li >= app->config.num_layers) li = 0;
    const LayerCfg *layer = &app->config.layers[li];

    memset(app->led_state, 0, sizeof(app->led_state));
    for (int i = 0; i < BUTTON_COUNT; i++)
        app->led_state[i] = app->config.default_color;

    for (int i = 0; i < layer->num_buttons; i++) {
        int idx = button_id_to_index(layer->buttons[i].id);
        if (idx >= 0) app->led_state[idx] = layer->buttons[i].color;
    }
}

/* ── AppState lifecycle ─────────────────────────────────────────────────────── */

AppState *app_create(void) {
    init_ids();
    AppState *app = calloc(1, sizeof(AppState));
    if (!app) return NULL;
    pthread_mutex_init(&app->mutex, NULL);
    config_default_path(app->config_path, sizeof(app->config_path));
    config_load(&app->config, app->config_path);
    sync_led_from_config(app);
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Ready — connect a Launchpad to get started.");
    return app;
}

void app_destroy(AppState *app) {
    if (!app) return;
    app_stop(app);
    app_disconnect(app);
#ifdef HAVE_GIF
    for (int i = 0; i < MAX_LAYERS; i++) {
        if (app->layer_gif[i]) { gif_free(app->layer_gif[i]); app->layer_gif[i] = NULL; }
    }
#endif
    pthread_mutex_destroy(&app->mutex);
    free(app);
}

int app_connect(AppState *app) {
    if (app->device_connected) return 0;
    if (lp_open(&app->lp) < 0) {
        snprintf(app->status_msg, sizeof(app->status_msg),
                 "No Launchpad found — is it plugged in?");
        return -1;
    }
    app->keys = keys_init();
    config_load(&app->config, app->config_path);
    sync_led_from_config(app);
    app->device_connected = 1;
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Connected — %d layer(s), %d button(s) in active layer.",
             app->config.num_layers,
             app->config.layers[app->active_layer].num_buttons);
    return 0;
}

void app_disconnect(AppState *app) {
    if (!app->device_connected) return;
    app_stop(app);
    if (app->keys) { keys_release_all(app->keys); keys_free(app->keys); app->keys = NULL; }
    lp_clear(&app->lp);
    lp_close(&app->lp);
    app->device_connected = 0;
    memset(app->led_state, 0, sizeof(app->led_state));
    snprintf(app->status_msg, sizeof(app->status_msg), "Disconnected.");
}

int app_start(AppState *app) {
    if (!app->device_connected || app->running) return -1;

    app->stop_requested    = 0;
    app->gif_stop          = 0;
    app->layer_switch_flag = 0;

    int li = app->active_layer;
    if (li >= app->config.num_layers) li = 0;
    const LayerCfg *layer = &app->config.layers[li];

    /* Light up configured buttons for current layer */
    for (int i = 0; i < layer->num_buttons; i++) {
        ButtonCfg *b = &app->config.layers[li].buttons[i];
#ifdef HAVE_GIF
        if (!app->layer_gif[li] || b->gif_overlay)
#endif
            lp_set_button(&app->lp, b->id, b->color);
    }

    /* Sync led_state */
    pthread_mutex_lock(&app->mutex);
    sync_led_from_config(app);
    pthread_mutex_unlock(&app->mutex);

#ifdef HAVE_GIF
    /* Pre-load GIFs for all layers */
    for (int i = 0; i < app->config.num_layers; i++) {
        const char *gp = app->config.layers[i].gif_path;
        if (gp[0] && !app->layer_gif[i]) {
            float fps_override = app->config.layers[i].fps;
            app->layer_gif[i] = gif_load(gp, app->config.layers[i].gif_mode);
            if (app->layer_gif[i] && fps_override > 0.0f) {
                int ms = (int)(1000.0f / fps_override);
                for (int f = 0; f < app->layer_gif[i]->count; f++)
                    app->layer_gif[i]->delays[f] = ms;
            }
        }
    }

    /* Rebuild protected list */
    rebuild_protected(app);

    /* Start GIF thread if any layer has a GIF */
    int has_gif = 0;
    for (int i = 0; i < app->config.num_layers; i++)
        if (app->layer_gif[i]) { has_gif = 1; break; }
    if (has_gif) {
        app->gif_thread_running = 1;
        pthread_create(&app->gif_thread, NULL, gif_thread_func, app);
    }
#endif

#ifdef HAVE_PULSEAUDIO
    /* Start equalizer thread if any layer has eq_enabled */
    int has_eq = 0;
    for (int i = 0; i < app->config.num_layers; i++)
        if (app->config.layers[i].eq_enabled) { has_eq = 1; break; }
    if (has_eq) {
        /* Open audio using the active layer's device setting */
        const char *dev = layer->eq_device[0] ? layer->eq_device : NULL;
        app->audio = audio_open(dev);
        if (app->audio) {
            app->eq_thread_running = 1;
            pthread_create(&app->eq_thread, NULL, eq_thread_func, app);
        } else {
            snprintf(app->status_msg, sizeof(app->status_msg),
                     "Equalizer: failed to open audio source.");
        }
    }
#endif

    app->event_thread_running = 1;
    pthread_create(&app->event_thread, NULL, event_thread_func, app);

    app->running = 1;
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Running — layer %d / %d.", li + 1, app->config.num_layers);
    return 0;
}

void app_stop(AppState *app) {
    if (!app->running) return;

    app->stop_requested = 1;
    app->gif_stop       = 1;

    if (app->event_thread_running) {
        pthread_join(app->event_thread, NULL);
        app->event_thread_running = 0;
    }
#ifdef HAVE_GIF
    if (app->gif_thread_running) {
        pthread_join(app->gif_thread, NULL);
        app->gif_thread_running = 0;
    }
    /* Free all layer GIFs */
    for (int i = 0; i < MAX_LAYERS; i++) {
        if (app->layer_gif[i]) { gif_free(app->layer_gif[i]); app->layer_gif[i] = NULL; }
    }
#endif
#ifdef HAVE_PULSEAUDIO
    if (app->eq_thread_running) {
        pthread_join(app->eq_thread, NULL);
        app->eq_thread_running = 0;
    }
    if (app->audio) { audio_close(app->audio); app->audio = NULL; }
#endif

    if (app->keys) keys_release_all(app->keys);
    lp_clear(&app->lp);
    app->running = 0;
    snprintf(app->status_msg, sizeof(app->status_msg), "Stopped.");
}

int app_load_layer_gif(AppState *app, int layer, const char *path) {
    if (layer < 0) layer = app->active_layer;
    if (layer >= MAX_LAYERS) return -1;

#ifdef HAVE_GIF
    if (app->layer_gif[layer]) {
        gif_free(app->layer_gif[layer]);
        app->layer_gif[layer] = NULL;
    }
    if (!path || !path[0]) {
        strncpy(app->config.layers[layer].gif_path, "", 1);
        snprintf(app->status_msg, sizeof(app->status_msg), "GIF cleared for layer %d.", layer + 1);
        return 0;
    }
    app->layer_gif[layer] = gif_load(path, app->config.layers[layer].gif_mode);
    if (!app->layer_gif[layer]) {
        snprintf(app->status_msg, sizeof(app->status_msg),
                 "Failed to load GIF: %s", path);
        return -1;
    }
    float fps_override = app->config.layers[layer].fps;
    if (fps_override > 0.0f) {
        int ms = (int)(1000.0f / fps_override);
        for (int f = 0; f < app->layer_gif[layer]->count; f++)
            app->layer_gif[layer]->delays[f] = ms;
    }
    strncpy(app->config.layers[layer].gif_path, path,
            sizeof(app->config.layers[layer].gif_path) - 1);
    snprintf(app->status_msg, sizeof(app->status_msg),
             "GIF loaded for layer %d: %d frame(s).",
             layer + 1, app->layer_gif[layer]->count);
    return 0;
#else
    (void)path;
    return -1;
#endif
}

void app_clear_layer_gif(AppState *app, int layer) {
    if (layer < 0) layer = app->active_layer;
    app_load_layer_gif(app, layer, NULL);
}

void app_reload_config(AppState *app) {
    config_load(&app->config, app->config_path);
    if (app->active_layer >= app->config.num_layers)
        app->active_layer = app->config.num_layers - 1;
    if (!app->running) sync_led_from_config(app);
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Config reloaded — %d layer(s).", app->config.num_layers);
}

void app_save_config(AppState *app) {
    if (config_save(&app->config, app->config_path) == 0)
        snprintf(app->status_msg, sizeof(app->status_msg),
                 "Config saved — %s", app->config_path);
    else
        snprintf(app->status_msg, sizeof(app->status_msg),
                 "Failed to save config — %s", app->config_path);
}

void app_switch_layer(AppState *app, int layer_idx) {
    if (layer_idx < 0 || layer_idx >= app->config.num_layers) return;
    if (layer_idx == app->active_layer) return;

    app->active_layer = layer_idx;

    /* Rebuild protected button list for the new layer */
    rebuild_protected(app);

    /* Update led_state from new layer's button colors */
    pthread_mutex_lock(&app->mutex);
    sync_led_from_config(app);
    pthread_mutex_unlock(&app->mutex);

    /* Signal the GIF thread to switch frames */
    pthread_mutex_lock(&app->mutex);
    app->layer_switch_flag = 1;
    pthread_mutex_unlock(&app->mutex);

    /* Light up new layer's buttons on hardware (if not overridden by GIF) */
    if (app->device_connected) {
        lp_clear(&app->lp);
        const LayerCfg *layer = &app->config.layers[layer_idx];
        for (int i = 0; i < layer->num_buttons; i++) {
#ifdef HAVE_GIF
            if (!app->layer_gif[layer_idx] || layer->buttons[i].gif_overlay)
#endif
                lp_set_button(&app->lp, layer->buttons[i].id, layer->buttons[i].color);
        }
    }

    snprintf(app->status_msg, sizeof(app->status_msg),
             "Switched to layer %d.", layer_idx + 1);
}

int app_add_layer(AppState *app) {
    if (app->config.num_layers >= MAX_LAYERS) return -1;
    int new_idx = app->config.num_layers;
    LayerCfg *layer = &app->config.layers[new_idx];
    memset(layer, 0, sizeof(*layer));
    layer->default_color         = app->config.default_color;
    layer->default_color_pressed = app->config.default_color_pressed;
    layer->eq_x      = 0;
    layer->eq_y      = 1;
    layer->eq_width  = 8;
    layer->eq_height = 7;
    app->config.num_layers++;
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Added layer %d.", new_idx + 1);
    return new_idx;
}

void app_delete_layer(AppState *app, int layer_idx) {
    if (app->config.num_layers <= 1) return; /* always keep at least 1 */
    if (layer_idx < 0 || layer_idx >= app->config.num_layers) return;

#ifdef HAVE_GIF
    if (app->layer_gif[layer_idx]) {
        gif_free(app->layer_gif[layer_idx]);
        app->layer_gif[layer_idx] = NULL;
    }
    /* Shift GIF pointers */
    for (int i = layer_idx; i < app->config.num_layers - 1; i++)
        app->layer_gif[i] = app->layer_gif[i + 1];
    app->layer_gif[app->config.num_layers - 1] = NULL;
#endif

    /* Shift layer configs */
    for (int i = layer_idx; i < app->config.num_layers - 1; i++)
        app->config.layers[i] = app->config.layers[i + 1];
    app->config.num_layers--;

    if (app->active_layer >= app->config.num_layers)
        app->active_layer = app->config.num_layers - 1;

    sync_led_from_config(app);
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Deleted layer. %d layer(s) remaining.", app->config.num_layers);
}

/* ── In-memory config editing ───────────────────────────────────────────────── */

static ButtonCfg *get_or_create_button(AppState *app, int btn_idx) {
    int li = app->active_layer;
    if (li >= app->config.num_layers) return NULL;
    LayerCfg *layer = &app->config.layers[li];

    const char *id = button_index_to_id(btn_idx);
    if (!id || id[0] == '?') return NULL;
    for (int i = 0; i < layer->num_buttons; i++)
        if (strcmp(layer->buttons[i].id, id) == 0) return &layer->buttons[i];
    if (layer->num_buttons >= MAX_BUTTONS) return NULL;
    ButtonCfg *btn = &layer->buttons[layer->num_buttons++];
    memset(btn, 0, sizeof(*btn));
    strncpy(btn->id, id, sizeof(btn->id) - 1);
    btn->color         = layer->default_color  ? layer->default_color         : app->config.default_color;
    btn->color_pressed = layer->default_color_pressed ? layer->default_color_pressed : app->config.default_color_pressed;
    return btn;
}

void app_set_button_color(AppState *app, int btn_idx, uint8_t color) {
    ButtonCfg *btn = get_or_create_button(app, btn_idx);
    if (!btn) return;
    btn->color = color;
    if (!app->running) {
        pthread_mutex_lock(&app->mutex);
        app->led_state[btn_idx] = color;
        pthread_mutex_unlock(&app->mutex);
    }
}

void app_set_button_color_pressed(AppState *app, int btn_idx, uint8_t color_pressed) {
    ButtonCfg *btn = get_or_create_button(app, btn_idx);
    if (btn) btn->color_pressed = color_pressed;
}

void app_set_button_action(AppState *app, int btn_idx, const char *action) {
    ButtonCfg *btn = get_or_create_button(app, btn_idx);
    if (btn) {
        strncpy(btn->action, action, MAX_ACTION_LEN - 1);
        btn->action[MAX_ACTION_LEN - 1] = '\0';
    }
}

void app_set_button_gif_overlay(AppState *app, int btn_idx, int overlay) {
    ButtonCfg *btn = get_or_create_button(app, btn_idx);
    if (btn) btn->gif_overlay = overlay;
}

void app_set_button_repeat(AppState *app, int btn_idx, int on_hold,
                           int hold_delay_ms, int repeat_interval_ms) {
    ButtonCfg *btn = get_or_create_button(app, btn_idx);
    if (!btn) return;
    btn->repeat_on_hold     = on_hold;
    btn->hold_delay_ms      = hold_delay_ms;
    btn->repeat_interval_ms = repeat_interval_ms;
}
