/*
 * launc-macro — Launchpad Mini MK1 GIF display + macro pad (C port)
 *
 * Usage:
 *   launc-macro                          # macro-pad mode
 *   launc-macro animation.gif            # GIF mode + macro-pad
 *   launc-macro animation.gif -f 15      # custom fps
 *   launc-macro animation.gif -m red     # colour filter
 *   launc-macro -c /path/to/my.conf      # custom config
 *   launc-macro --edit                   # open config in system default app
 *   launc-macro --edit vim               # open config in vim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <direct.h>
// map linux/unix/the normal helpers to Windows equivalents
#  define usleep(us)      Sleep((DWORD)((us) / 1000))
#  define mkdir(p, m)     _mkdir(p)
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#endif

#include "midi.h"
#include "config.h"
#include "keys.h"
#ifdef HAVE_GIF
#include "gif.h"
#endif

// globals for signal handler
static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

// GIF playback thread

#ifdef HAVE_GIF
typedef struct {
    Launchpad *lp;
    GifData   *gd;
    int        mode;             // 0=full 1=red 2=green 3=yellow
    // protected cells: gif_overlay buttons, GIF must not overwrite them
    int  prot_row[MAX_BUTTONS];
    int  prot_col[MAX_BUTTONS];
    int  nprot;
    volatile int *stop;
} GifArg;

static int is_protected(int row, int col, const GifArg *a) {
    for (int i = 0; i < a->nprot; i++)
        if (a->prot_row[i] == row && a->prot_col[i] == col) return 1;
    return 0;
}

static void render_frame(Launchpad *lp, const uint8_t *vels, const GifArg *a) {
    for (int row = 0; row < 9; row++) {
        for (int col = 0; col < 9; col++) {
            if (is_protected(row, col, a)) continue;
            lp_set_rc(lp, row, col, vels[row * 9 + col]);
        }
    }
}

static void *gif_thread(void *arg) {
    GifArg *a = (GifArg *)arg;
    while (!*a->stop) {
        for (int fi = 0; fi < a->gd->count && !*a->stop; fi++) {
            render_frame(a->lp, a->gd->frames[fi], a);
            int ms = a->gd->delays[fi];
            // sleep in small chunks so the stop flag is checked promptly
            for (int t = 0; t < ms && !*a->stop; ) {
                int chunk = ms - t < 10 ? ms - t : 10;
                usleep((unsigned)(chunk * 1000));
                t += chunk;
            }
        }
    }
    return NULL;
}
#endif // HAVE_GIF

// action executor
static void execute_action(Keys *keys, const char *action) {
    if (strncmp(action, "key:", 4) == 0) {
        keys_send_combo(keys, action + 4);
    } else if (strncmp(action, "media:", 6) == 0) {
        keys_send_media(keys, action + 6);
    } else if (strncmp(action, "app:", 4) == 0) {
#ifdef _WIN32
        // run detached via cmd.exe so we don't block the event loop
        char cmd_line[512];
        snprintf(cmd_line, sizeof(cmd_line), "cmd.exe /c %s", action + 4);
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
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
            // reap asynchronously, SIGCHLD is ignored by default
            waitpid(pid, NULL, WNOHANG);
        }
#endif
    } else {
        fprintf(stderr, "warning: unrecognised action '%s'\n", action);
    }
}

// argument parsing
typedef struct {
    const char *gif;           // NULL = macro-pad only
    float       fps;           // 0 = use GIF metadata
    int         mode;          // 0=full 1=red 2=green 3=yellow
    char        config[512];
    int         edit_config;   // 1 = open config in editor then exit
    char        editor[256];   // editor to use, empty = system default (on Linux, doesnt work on Windows)
} Args;

static void usage(const char *argv0, FILE *out) {
    fprintf(out,
        "Usage: %s [GIF] [-f FPS] [-m MODE] [-c CONFIG] [--edit [EDITOR]]\n"
        "  GIF           image/gif file to display\n"
        "  -f FPS        playback fps (default: from GIF metadata)\n"
        "  -m MODE       colour filter: full|red|green|yellow  (default: full)\n"
        "  -c FILE       config file  (default: platform config dir)\n"
        "  --edit/-e     open config file in EDITOR (default: system default app)\n"
        "  --help/-h     show this help and exit\n",
        argv0);
}

static int parse_args(int argc, char **argv, Args *a) {
    config_default_path(a->config, sizeof(a->config));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-fps") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -f requires a value\n"); return -1; }
            a->fps = (float)atof(argv[i]);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -m requires a value\n"); return -1; }
            if      (strcmp(argv[i], "red")    == 0) a->mode = 1;
            else if (strcmp(argv[i], "green")  == 0) a->mode = 2;
            else if (strcmp(argv[i], "yellow") == 0) a->mode = 3;
            else if (strcmp(argv[i], "full")   == 0) a->mode = 0;
            else { fprintf(stderr, "error: unknown mode '%s'\n", argv[i]); return -1; }
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -c requires a value\n"); return -1; }
            snprintf(a->config, sizeof(a->config), "%s", argv[i]);
        } else if (strcmp(argv[i], "--edit") == 0 || strcmp(argv[i], "-e") == 0) {
            a->edit_config = 1;
            // optional next arg: editor name (must not start with '-')
            if (i + 1 < argc && argv[i + 1][0] != '-')
                snprintf(a->editor, sizeof(a->editor), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0], stdout);
            exit(0);
        } else if (argv[i][0] != '-') {
            a->gif = argv[i];
        } else {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            usage(argv[0], stderr); return -1;
        }
    }
    return 0;
}

// mkdir -p equivalent
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

// main
int main(int argc, char **argv) {
    Args args = {0};
    if (parse_args(argc, argv, &args) < 0) return 1;

    if (args.edit_config) {
        // ensure the config directory exists
        char dir[512];
        snprintf(dir, sizeof(dir), "%s", args.config);
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

#ifdef _WIN32
        if (args.editor[0]) {
            char cmd[768];
            snprintf(cmd, sizeof(cmd), "%s %s", args.editor, args.config);
            return system(cmd) < 0 ? 1 : 0;
        } else {
            // open with the system default application
            HINSTANCE r = ShellExecuteA(NULL, "open", args.config, NULL, NULL, SW_SHOWNORMAL);
            return ((INT_PTR)r > 32) ? 0 : 1;
        }
#else
        const char *editor = args.editor[0] ? args.editor : "xdg-open";
        char cmd[768];
        snprintf(cmd, sizeof(cmd), "%s %s", editor, args.config);
        return system(cmd) < 0 ? 1 : 0;
#endif
    }

#ifndef HAVE_GIF
    if (args.gif) {
        fprintf(stderr, "error: built without GIF support (HAVE_GIF not defined)\n");
        return 1;
    }
#endif

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
#ifndef _WIN32
    signal(SIGCHLD, SIG_DFL);   // let waitpid work
#endif

    printf("connecting to launchpad…\n");
    Launchpad lp;
    if (lp_open(&lp) < 0) return 1;
    printf("connected\n");

    Config cfg;
    config_load(&cfg, args.config);

    Keys *keys = keys_init();
    if (!keys) {
#ifdef _WIN32
        fprintf(stderr, "warning: key/media actions disabled\n");
#else
        fprintf(stderr, "warning: key/media actions disabled (uinput unavailable)\n");
#endif
    }

    // build set of gif_overlay buttons
    int ov_row[MAX_BUTTONS], ov_col[MAX_BUTTONS], nov = 0;
    for (int i = 0; i < cfg.num_buttons; i++) {
        if (cfg.buttons[i].gif_overlay) {
            int row, col;
            if (lp_id_to_rc(cfg.buttons[i].id, &row, &col) && nov < MAX_BUTTONS) {
                ov_row[nov] = row; ov_col[nov] = col; nov++;
            }
        }
    }

#ifdef HAVE_GIF
    GifData *gd = NULL;
    pthread_t tid;
    GifArg    garg = {0};
    volatile int gif_stop = 0;

    if (args.gif) {
        printf("loading '%s'…\n", args.gif);
        gd = gif_load(args.gif, args.mode);
        if (!gd) { lp_clear(&lp); lp_close(&lp); return 1; }

        float fps = args.fps;
        if (fps <= 0.0f)
            fps = (gd->delays[0] > 0) ? 1000.0f / gd->delays[0] : 10.0f;

        // recalculate per-frame delays from fps override if -f was given
        if (args.fps > 0.0f) {
            int ms = (int)(1000.0f / fps);
            for (int i = 0; i < gd->count; i++) gd->delays[i] = ms;
        }

        // light up overlay buttons before GIF starts
        for (int i = 0; i < cfg.num_buttons; i++) {
            if (!cfg.buttons[i].gif_overlay) continue;
            lp_set_button(&lp, cfg.buttons[i].id, cfg.buttons[i].color);
        }

        garg.lp   = &lp;
        garg.gd   = gd;
        garg.mode = args.mode;
        garg.stop = &gif_stop;
        garg.nprot = nov;
        for (int i = 0; i < nov; i++) { garg.prot_row[i]=ov_row[i]; garg.prot_col[i]=ov_col[i]; }

        pthread_create(&tid, NULL, gif_thread, &garg);
        printf("displaying %d frame(s) at %.1f fps\n", gd->count, fps);
        if (nov) printf("gif_overlay buttons: %d pinned\n", nov);
    } else
#endif
    {
        // static colour mode
        for (int i = 0; i < cfg.num_buttons; i++)
            lp_set_button(&lp, cfg.buttons[i].id, cfg.buttons[i].color);
        printf("macro-pad mode — buttons loaded from config\n");
    }

    printf("ctrl+c to exit\n");

    // button event loop
    while (!g_stop) {
        ButtonEvent ev;
        int r = lp_poll(&lp, &ev, 10);
        if (r < 0) break;
        if (r == 0) continue;

        const ButtonCfg *btn = config_find_button(&cfg, ev.id);
        uint8_t idle_c    = btn ? btn->color         : cfg.default_color;
        uint8_t pressed_c = btn ? btn->color_pressed : cfg.default_color_pressed;

        // is this an overlay button?
        int is_overlay = 0;
        for (int i = 0; i < cfg.num_buttons; i++)
            if (cfg.buttons[i].gif_overlay && strcmp(cfg.buttons[i].id, ev.id) == 0)
                { is_overlay = 1; break; }

        if (ev.pressed) {
            int show_feedback = !args.gif || is_overlay;
            if (show_feedback) lp_set_button(&lp, ev.id, pressed_c);
            if (btn && btn->action[0] && keys)
                execute_action(keys, btn->action);
        } else {
            int show_feedback = !args.gif || is_overlay;
            if (show_feedback) lp_set_button(&lp, ev.id, idle_c);
        }
    }

    // cleanup
#ifdef HAVE_GIF
    if (args.gif) {
        gif_stop = 1;
        pthread_join(tid, NULL);
        gif_free(gd);
    }
#endif
    if (keys) keys_release_all(keys);
    lp_clear(&lp);
    lp_close(&lp);
    if (keys) keys_free(keys);

    return 0;
}
