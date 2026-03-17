#include "keys.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_HELD 32

#ifdef _WIN32

// windows implementation (SendInput)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct Keys {
    int held_vk[MAX_HELD];
    int nheld;
};

static const struct { const char *name; int vk; } KEY_TABLE[] = {
    // modifiers
    {"ctrl",         VK_LCONTROL},  {"control",      VK_LCONTROL},
    {"shift",        VK_LSHIFT},    {"alt",          VK_LMENU},
    {"meta",         VK_LWIN},      {"super",        VK_LWIN},
    {"cmd",          VK_LWIN},      {"win",          VK_LWIN},
    // specials
    {"space",        VK_SPACE},     {"enter",        VK_RETURN},
    {"return",       VK_RETURN},    {"tab",          VK_TAB},
    {"esc",          VK_ESCAPE},    {"escape",       VK_ESCAPE},
    {"backspace",    VK_BACK},      {"delete",       VK_DELETE},
    {"del",          VK_DELETE},    {"home",         VK_HOME},
    {"end",          VK_END},       {"page_up",      VK_PRIOR},
    {"pageup",       VK_PRIOR},     {"page_down",    VK_NEXT},
    {"pagedown",     VK_NEXT},      {"up",           VK_UP},
    {"down",         VK_DOWN},      {"left",         VK_LEFT},
    {"right",        VK_RIGHT},     {"insert",       VK_INSERT},
    {"caps_lock",    VK_CAPITAL},   {"print_screen", VK_SNAPSHOT},
    {"pause",        VK_PAUSE},     {"num_lock",     VK_NUMLOCK},
    // F-keys
    {"f1",  VK_F1},  {"f2",  VK_F2},  {"f3",  VK_F3},  {"f4",  VK_F4},
    {"f5",  VK_F5},  {"f6",  VK_F6},  {"f7",  VK_F7},  {"f8",  VK_F8},
    {"f9",  VK_F9},  {"f10", VK_F10}, {"f11", VK_F11}, {"f12", VK_F12},
    {"f13", VK_F13}, {"f14", VK_F14}, {"f15", VK_F15}, {"f16", VK_F16},
    {"f17", VK_F17}, {"f18", VK_F18}, {"f19", VK_F19}, {"f20", VK_F20},
    {"f21", VK_F21}, {"f22", VK_F22}, {"f23", VK_F23}, {"f24", VK_F24},
    // letters
    {"a", 'A'}, {"b", 'B'}, {"c", 'C'}, {"d", 'D'}, {"e", 'E'},
    {"f", 'F'}, {"g", 'G'}, {"h", 'H'}, {"i", 'I'}, {"j", 'J'},
    {"k", 'K'}, {"l", 'L'}, {"m", 'M'}, {"n", 'N'}, {"o", 'O'},
    {"p", 'P'}, {"q", 'Q'}, {"r", 'R'}, {"s", 'S'}, {"t", 'T'},
    {"u", 'U'}, {"v", 'V'}, {"w", 'W'}, {"x", 'X'}, {"y", 'Y'},
    {"z", 'Z'},
    // digits
    {"0", '0'}, {"1", '1'}, {"2", '2'}, {"3", '3'}, {"4", '4'},
    {"5", '5'}, {"6", '6'}, {"7", '7'}, {"8", '8'}, {"9", '9'},
    // media
    {"play_pause",  VK_MEDIA_PLAY_PAUSE}, {"play",         VK_MEDIA_PLAY_PAUSE},
    {"next",        VK_MEDIA_NEXT_TRACK}, {"prev",         VK_MEDIA_PREV_TRACK},
    {"previous",    VK_MEDIA_PREV_TRACK}, {"volume_up",    VK_VOLUME_UP},
    {"vol_up",      VK_VOLUME_UP},        {"volume_down",  VK_VOLUME_DOWN},
    {"vol_down",    VK_VOLUME_DOWN},      {"mute",         VK_VOLUME_MUTE},
};
#define KEY_TABLE_LEN (int)(sizeof(KEY_TABLE)/sizeof(KEY_TABLE[0]))

static int resolve_key(const char *name) {
    char lo[64]; int i;
    for (i = 0; name[i] && i < 63; i++)
        lo[i] = (name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i];
    lo[i] = '\0';
    for (i = 0; i < KEY_TABLE_LEN; i++)
        if (strcmp(KEY_TABLE[i].name, lo) == 0) return KEY_TABLE[i].vk;
    fprintf(stderr, "warning: unknown key '%s'\n", name);
    return -1;
}

Keys *keys_init(void) {
    Keys *k = calloc(1, sizeof(Keys));
    return k;
}

void keys_free(Keys *k) {
    if (!k) return;
    keys_release_all(k);
    free(k);
}

static void press_key(Keys *k, int vk) {
    INPUT in = {0};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = (WORD)vk;
    in.ki.dwFlags = 0;
    SendInput(1, &in, sizeof(INPUT));
    if (k->nheld < MAX_HELD) k->held_vk[k->nheld++] = vk;
}

static void release_key(Keys *k, int vk) {
    INPUT in = {0};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = (WORD)vk;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
    for (int i = 0; i < k->nheld; i++) {
        if (k->held_vk[i] == vk) { k->held_vk[i] = k->held_vk[--k->nheld]; break; }
    }
}

int keys_send_combo(Keys *k, const char *combo) {
    char buf[256];
    strncpy(buf, combo, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int codes[16]; int ncodes = 0;
    char *tok = strtok(buf, "+");
    while (tok && ncodes < 16) {
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ') *end-- = '\0';
        int code = resolve_key(tok);
        if (code < 0) return -1;
        codes[ncodes++] = code;
        tok = strtok(NULL, "+");
    }
    for (int i = 0;        i < ncodes; i++) press_key(k, codes[i]);
    for (int i = ncodes-1; i >= 0;    i--) release_key(k, codes[i]);
    return 0;
}

int keys_send_media(Keys *k, const char *name) {
    int code = resolve_key(name);
    if (code < 0) return -1;
    press_key(k, code);
    release_key(k, code);
    return 0;
}

void keys_release_all(Keys *k) {
    for (int i = k->nheld - 1; i >= 0; i--) {
        INPUT in = {0};
        in.type       = INPUT_KEYBOARD;
        in.ki.wVk     = (WORD)k->held_vk[i];
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    }
    k->nheld = 0;
}

#else // Linux / uinput

#include <fcntl.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <linux/input-event-codes.h>

struct Keys {
    int fd;
    int held[MAX_HELD];
    int nheld;
};

static void emit_ev(int fd, int type, int code, int val) {
    struct input_event ev = {0};
    ev.type  = (unsigned short)type;
    ev.code  = (unsigned short)code;
    ev.value = val;
    write(fd, &ev, sizeof(ev));
}
static void sync_ev(int fd) { emit_ev(fd, EV_SYN, SYN_REPORT, 0); }

// key name -> linux keycode

static const struct { const char *name; int code; } KEY_TABLE[] = {
    // modifiers
    {"ctrl",         KEY_LEFTCTRL},   {"control",      KEY_LEFTCTRL},
    {"shift",        KEY_LEFTSHIFT},  {"alt",          KEY_LEFTALT},
    {"meta",         KEY_LEFTMETA},   {"super",        KEY_LEFTMETA},
    {"cmd",          KEY_LEFTMETA},   {"win",          KEY_LEFTMETA},
    // specials
    {"space",        KEY_SPACE},      {"enter",        KEY_ENTER},
    {"return",       KEY_ENTER},      {"tab",          KEY_TAB},
    {"esc",          KEY_ESC},        {"escape",       KEY_ESC},
    {"backspace",    KEY_BACKSPACE},  {"delete",       KEY_DELETE},
    {"del",          KEY_DELETE},     {"home",         KEY_HOME},
    {"end",          KEY_END},        {"page_up",      KEY_PAGEUP},
    {"pageup",       KEY_PAGEUP},     {"page_down",    KEY_PAGEDOWN},
    {"pagedown",     KEY_PAGEDOWN},   {"up",           KEY_UP},
    {"down",         KEY_DOWN},       {"left",         KEY_LEFT},
    {"right",        KEY_RIGHT},      {"insert",       KEY_INSERT},
    {"caps_lock",    KEY_CAPSLOCK},   {"print_screen", KEY_SYSRQ},
    {"pause",        KEY_PAUSE},      {"num_lock",     KEY_NUMLOCK},
    // F-keys
    {"f1",  KEY_F1},  {"f2",  KEY_F2},  {"f3",  KEY_F3},  {"f4",  KEY_F4},
    {"f5",  KEY_F5},  {"f6",  KEY_F6},  {"f7",  KEY_F7},  {"f8",  KEY_F8},
    {"f9",  KEY_F9},  {"f10", KEY_F10}, {"f11", KEY_F11}, {"f12", KEY_F12},
    {"f13", KEY_F13}, {"f14", KEY_F14}, {"f15", KEY_F15}, {"f16", KEY_F16},
    {"f17", KEY_F17}, {"f18", KEY_F18}, {"f19", KEY_F19}, {"f20", KEY_F20},
    {"f21", KEY_F21}, {"f22", KEY_F22}, {"f23", KEY_F23}, {"f24", KEY_F24},
    // letters
    {"a", KEY_A}, {"b", KEY_B}, {"c", KEY_C}, {"d", KEY_D}, {"e", KEY_E},
    {"f", KEY_F}, {"g", KEY_G}, {"h", KEY_H}, {"i", KEY_I}, {"j", KEY_J},
    {"k", KEY_K}, {"l", KEY_L}, {"m", KEY_M}, {"n", KEY_N}, {"o", KEY_O},
    {"p", KEY_P}, {"q", KEY_Q}, {"r", KEY_R}, {"s", KEY_S}, {"t", KEY_T},
    {"u", KEY_U}, {"v", KEY_V}, {"w", KEY_W}, {"x", KEY_X}, {"y", KEY_Y},
    {"z", KEY_Z},
    // digits
    {"0", KEY_0}, {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}, {"4", KEY_4},
    {"5", KEY_5}, {"6", KEY_6}, {"7", KEY_7}, {"8", KEY_8}, {"9", KEY_9},
    // media
    {"play_pause",  KEY_PLAYPAUSE},   {"play",         KEY_PLAYPAUSE},
    {"next",        KEY_NEXTSONG},    {"prev",         KEY_PREVIOUSSONG},
    {"previous",    KEY_PREVIOUSSONG},{"volume_up",    KEY_VOLUMEUP},
    {"vol_up",      KEY_VOLUMEUP},    {"volume_down",  KEY_VOLUMEDOWN},
    {"vol_down",    KEY_VOLUMEDOWN},  {"mute",         KEY_MUTE},
};
#define KEY_TABLE_LEN (int)(sizeof(KEY_TABLE)/sizeof(KEY_TABLE[0]))

static int resolve_key(const char *name) {
    char lo[64]; int i;
    for (i = 0; name[i] && i < 63; i++)
        lo[i] = (name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i];
    lo[i] = '\0';
    for (i = 0; i < KEY_TABLE_LEN; i++)
        if (strcmp(KEY_TABLE[i].name, lo) == 0) return KEY_TABLE[i].code;
    fprintf(stderr, "warning: unknown key '%s'\n", name);
    return -1;
}

// uinput setup

Keys *keys_init(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/uinput");
        fprintf(stderr, "hint: add yourself to the 'input' group, or create a udev rule:\n"
                        "      KERNEL==\"uinput\", GROUP=\"input\", MODE=\"0660\"\n");
        return NULL;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int i = 0; i < KEY_TABLE_LEN; i++)
        ioctl(fd, UI_SET_KEYBIT, KEY_TABLE[i].code);

    struct uinput_setup setup = {0};
    setup.id.bustype = BUS_USB;
    setup.id.vendor  = 0x1209;
    setup.id.product = 0x4C50;  // 'LP'
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "launc-macro virtual keyboard");

    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("uinput dev create");
        close(fd);
        return NULL;
    }
    usleep(150000);  // let the kernel register the new device

    Keys *k = calloc(1, sizeof(Keys));
    k->fd = fd;
    return k;
}

void keys_free(Keys *k) {
    if (!k) return;
    keys_release_all(k);
    ioctl(k->fd, UI_DEV_DESTROY);
    close(k->fd);
    free(k);
}

// key send helpers
static void press_key(Keys *k, int code) {
    emit_ev(k->fd, EV_KEY, code, 1);
    sync_ev(k->fd);
    if (k->nheld < MAX_HELD) k->held[k->nheld++] = code;
}

static void release_key(Keys *k, int code) {
    emit_ev(k->fd, EV_KEY, code, 0);
    sync_ev(k->fd);
    for (int i = 0; i < k->nheld; i++) {
        if (k->held[i] == code) { k->held[i] = k->held[--k->nheld]; break; }
    }
}

int keys_send_combo(Keys *k, const char *combo) {
    char buf[256];
    strncpy(buf, combo, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int codes[16]; int ncodes = 0;
    char *tok = strtok(buf, "+");
    while (tok && ncodes < 16) {
        // trim spaces
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ') *end-- = '\0';
        int code = resolve_key(tok);
        if (code < 0) return -1;
        codes[ncodes++] = code;
        tok = strtok(NULL, "+");
    }
    for (int i = 0;        i < ncodes; i++) press_key(k, codes[i]);
    for (int i = ncodes-1; i >= 0;    i--) release_key(k, codes[i]);
    return 0;
}

int keys_send_media(Keys *k, const char *name) {
    int code = resolve_key(name);
    if (code < 0) return -1;
    press_key(k, code);
    release_key(k, code);
    return 0;
}

void keys_release_all(Keys *k) {
    for (int i = k->nheld - 1; i >= 0; i--) {
        emit_ev(k->fd, EV_KEY, k->held[i], 0);
        sync_ev(k->fd);
    }
    k->nheld = 0;
}

#endif // _WIN32
