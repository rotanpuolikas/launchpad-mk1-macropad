#include "app.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
#  include <time.h>
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

/* row=0, col=0..8  → top row; row=1..8, col=8 → side; row=1..8, col=0..7 → grid */
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

/* ── Action execution (mirrors main.c) ─────────────────────────────────────── */

static void execute_action(Keys *keys, const char *action) {
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
                           DETACHED_PROCESS | CREATE_NO_WINDOW,
                           NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
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
    } else if (strncmp(action, "terminal:", 9) == 0) {
#ifdef _WIN32
        char cmd_line[512];
        snprintf(cmd_line, sizeof(cmd_line),
                 "powershell.exe -NoExit -Command \"%s\"", action + 9);
        STARTUPINFOA si; PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));
        if (CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE,
                           CREATE_NEW_CONSOLE,
                           NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
#else
        const char *shell = getenv("SHELL");
        if (!shell || !shell[0]) shell = "/bin/sh";
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            execl(shell, shell, "-c", action + 9, NULL);
            _exit(127);
        } else if (pid > 0) {
            waitpid(pid, NULL, WNOHANG);
        }
#endif
    }
}

/* ── GIF thread ─────────────────────────────────────────────────────────────── */

static int is_protected(int row, int col, const AppState *app) {
    for (int i = 0; i < app->nprot; i++)
        if (app->prot_row[i] == row && app->prot_col[i] == col) return 1;
    return 0;
}

static void *gif_thread_func(void *arg) {
    AppState *app = (AppState *)arg;
    while (!app->gif_stop) {
        for (int fi = 0; fi < app->gif->count && !app->gif_stop; fi++) {
            const uint8_t *frame = app->gif->frames[fi];

            /* render to device */
            for (int row = 0; row < 9; row++)
                for (int col = 0; col < 9; col++) {
                    if (is_protected(row, col, app)) continue;
                    lp_set_rc(&app->lp, row, col, frame[row * 9 + col]);
                }

            /* batch-update led_state for GUI */
            pthread_mutex_lock(&app->mutex);
            for (int row = 0; row < 9; row++)
                for (int col = 0; col < 9; col++) {
                    if (is_protected(row, col, app)) continue;
                    int idx = rc_to_index(row, col);
                    if (idx >= 0) app->led_state[idx] = frame[row * 9 + col];
                }
            pthread_mutex_unlock(&app->mutex);

            /* sleep in 10 ms chunks so stop flag is checked promptly */
            int ms = app->gif->delays[fi];
            for (int t = 0; t < ms && !app->gif_stop; ) {
                int chunk = ms - t < 10 ? ms - t : 10;
                sleep_ms(chunk);
                t += chunk;
            }
        }
    }
    return NULL;
}

/* ── Event thread ───────────────────────────────────────────────────────────── */

static void *event_thread_func(void *arg) {
    AppState *app = (AppState *)arg;
    while (!app->stop_requested) {
        ButtonEvent ev;
        int r = lp_poll(&app->lp, &ev, 10);
        if (r < 0) break;
        if (r == 0) continue;

        const ButtonCfg *btn = config_find_button(&app->config, ev.id);
        uint8_t idle_c    = btn ? btn->color         : app->config.default_color;
        uint8_t pressed_c = btn ? btn->color_pressed : app->config.default_color_pressed;
        int is_overlay    = btn && btn->gif_overlay;
        int show_feedback = !app->gif || is_overlay;

        int idx = button_id_to_index(ev.id);

        if (ev.pressed) {
            if (show_feedback) lp_set_button(&app->lp, ev.id, pressed_c);
            if (btn && btn->action[0] && app->keys)
                execute_action(app->keys, btn->action);
            if (idx >= 0) {
                pthread_mutex_lock(&app->mutex);
                app->led_state[idx] = pressed_c;
                pthread_mutex_unlock(&app->mutex);
            }
        } else {
            if (show_feedback) lp_set_button(&app->lp, ev.id, idle_c);
            if (idx >= 0) {
                pthread_mutex_lock(&app->mutex);
                app->led_state[idx] = idle_c;
                pthread_mutex_unlock(&app->mutex);
            }
        }
    }
    return NULL;
}

/* ── led_state sync helper ──────────────────────────────────────────────────── */

/* Populate led_state from the current in-memory config so the button grid
 * reflects configured colours even before a device is connected or started. */
static void sync_led_from_config(AppState *app) {
    memset(app->led_state, 0, sizeof(app->led_state));
    /* apply default colour to every button */
    for (int i = 0; i < BUTTON_COUNT; i++)
        app->led_state[i] = app->config.default_color;
    /* override with per-button colours */
    for (int i = 0; i < app->config.num_buttons; i++) {
        int idx = button_id_to_index(app->config.buttons[i].id);
        if (idx >= 0) app->led_state[idx] = app->config.buttons[i].color;
    }
}

/* ── AppState lifecycle ─────────────────────────────────────────────────────── */

AppState *app_create(void) {
    init_ids();
    AppState *app = calloc(1, sizeof(AppState));
    if (!app) return NULL;
    pthread_mutex_init(&app->mutex, NULL);
    config_default_path(app->config_path, sizeof(app->config_path));
    /* Pre-load config so the button grid is visible immediately */
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
    if (app->gif) { gif_free(app->gif); app->gif = NULL; }
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
    app->keys = keys_init();  /* NULL is non-fatal */
    config_load(&app->config, app->config_path);
    sync_led_from_config(app);
    app->device_connected = 1;
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Connected — %d button(s) configured.", app->config.num_buttons);
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

    app->stop_requested = 0;
    app->gif_stop       = 0;

    /* light up configured buttons */
    for (int i = 0; i < app->config.num_buttons; i++) {
        ButtonCfg *b = &app->config.buttons[i];
        if (!app->gif || b->gif_overlay)
            lp_set_button(&app->lp, b->id, b->color);
    }

    /* sync led_state with config */
    pthread_mutex_lock(&app->mutex);
    memset(app->led_state, 0, sizeof(app->led_state));
    for (int i = 0; i < app->config.num_buttons; i++) {
        int idx = button_id_to_index(app->config.buttons[i].id);
        if (idx >= 0) app->led_state[idx] = app->config.buttons[i].color;
    }
    pthread_mutex_unlock(&app->mutex);

#ifdef HAVE_GIF
    if (app->gif) {
        /* apply FPS override */
        if (app->fps > 0.0f) {
            int ms = (int)(1000.0f / app->fps);
            for (int i = 0; i < app->gif->count; i++) app->gif->delays[i] = ms;
        }

        /* build protected button set */
        app->nprot = 0;
        for (int i = 0; i < app->config.num_buttons; i++) {
            if (!app->config.buttons[i].gif_overlay) continue;
            int row, col;
            if (lp_id_to_rc(app->config.buttons[i].id, &row, &col) &&
                app->nprot < MAX_BUTTONS) {
                app->prot_row[app->nprot] = row;
                app->prot_col[app->nprot] = col;
                app->nprot++;
            }
        }

        app->gif_thread_running = 1;
        pthread_create(&app->gif_thread, NULL, gif_thread_func, app);
    }
#endif

    app->event_thread_running = 1;
    pthread_create(&app->event_thread, NULL, event_thread_func, app);

    app->running = 1;
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Running — %s", app->gif ? "GIF + macro pad." : "macro pad only.");
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
    if (app->gif_thread_running) {
        pthread_join(app->gif_thread, NULL);
        app->gif_thread_running = 0;
    }

    if (app->keys) keys_release_all(app->keys);
    lp_clear(&app->lp);
    app->running = 0;
    snprintf(app->status_msg, sizeof(app->status_msg), "Stopped.");
}

int app_load_gif(AppState *app, const char *path) {
    if (app->gif) { gif_free(app->gif); app->gif = NULL; }
    app->gif = gif_load(path, app->gif_mode);
    if (!app->gif) {
        snprintf(app->status_msg, sizeof(app->status_msg),
                 "Failed to load: %s", path);
        return -1;
    }
    snprintf(app->gif_path, sizeof(app->gif_path), "%s", path);
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Loaded GIF: %d frame(s).", app->gif->count);
    return 0;
}

void app_clear_gif(AppState *app) {
    if (app->gif) { gif_free(app->gif); app->gif = NULL; }
    app->gif_path[0] = '\0';
    snprintf(app->status_msg, sizeof(app->status_msg), "GIF cleared.");
}

void app_reload_config(AppState *app) {
    config_load(&app->config, app->config_path);
    if (!app->running) sync_led_from_config(app);
    snprintf(app->status_msg, sizeof(app->status_msg),
             "Config reloaded — %d button(s) configured.", app->config.num_buttons);
}

void app_save_config(AppState *app) {
    if (config_save(&app->config, app->config_path) == 0)
        snprintf(app->status_msg, sizeof(app->status_msg),
                 "Config saved — %s", app->config_path);
    else
        snprintf(app->status_msg, sizeof(app->status_msg),
                 "Failed to save config — %s", app->config_path);
}

static ButtonCfg *get_or_create_button(AppState *app, int btn_idx) {
    const char *id = button_index_to_id(btn_idx);
    if (!id || id[0] == '?') return NULL;
    for (int i = 0; i < app->config.num_buttons; i++)
        if (strcmp(app->config.buttons[i].id, id) == 0)
            return &app->config.buttons[i];
    if (app->config.num_buttons >= MAX_BUTTONS) return NULL;
    ButtonCfg *btn = &app->config.buttons[app->config.num_buttons++];
    memset(btn, 0, sizeof(*btn));
    strncpy(btn->id, id, sizeof(btn->id) - 1);
    btn->color         = app->config.default_color;
    btn->color_pressed = app->config.default_color_pressed;
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
