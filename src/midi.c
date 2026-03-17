#include "midi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// shared: MIDI message parser
static int parse_msg(uint8_t status, uint8_t b1, uint8_t b2, ButtonEvent *ev) {
    uint8_t type = status & 0xF0;
    if (type == 0xB0) {                         // control_change -> top row
        int col = b1 - 104;
        if (col >= 0 && col <= 7) {
            snprintf(ev->id, sizeof(ev->id), "top_%d", col);
            ev->pressed = b2 > 0;
            return 1;
        }
    } else if (type == 0x90 || type == 0x80) {  // note_on / note_off
        int row = b1 >> 4;
        int col = b1 & 0x0F;
        int pressed = (type == 0x90) ? (b2 > 0) : 0;
        if (col == 8 && row >= 0 && row <= 7) {
            snprintf(ev->id, sizeof(ev->id), "side_%d", row);
            ev->pressed = pressed; return 1;
        }
        if (col >= 0 && col <= 7 && row >= 0 && row <= 7) {
            snprintf(ev->id, sizeof(ev->id), "grid_%d_%d", row, col);
            ev->pressed = pressed; return 1;
        }
    }
    return 0;
}

// platform-specific: open / close / send3 / poll
#ifdef _WIN32

#include <mmsystem.h>

static void CALLBACK midi_in_callback(HMIDIIN hmi, UINT wMsg, DWORD_PTR dwInst,
                                       DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    (void)hmi; (void)dwParam2;
    if (wMsg != MIM_DATA) return;
    Launchpad *lp = (Launchpad *)dwInst;
    uint8_t bytes[3] = {
        (uint8_t)(dwParam1 & 0xFF),
        (uint8_t)((dwParam1 >> 8) & 0xFF),
        (uint8_t)((dwParam1 >> 16) & 0xFF),
    };
    WaitForSingleObject((HANDLE)lp->win_mutex, INFINITE);
    for (int i = 0; i < 3; i++) {
        int next = (lp->ring_head + 1) & 0xFF;
        if (next != lp->ring_tail) {
            lp->ring[lp->ring_head] = bytes[i];
            lp->ring_head = next;
        }
    }
    ReleaseMutex((HANDLE)lp->win_mutex);
}

int lp_open(Launchpad *lp) {
    memset(lp, 0, sizeof(*lp));

    // find output device
    UINT numOut = midiOutGetNumDevs();
    UINT outId  = (UINT)-1;
    for (UINT i = 0; i < numOut; i++) {
        MIDIOUTCAPSA caps;
        if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            if (strstr(caps.szPname, "Launchpad") || strstr(caps.szPname, "launchpad")) {
                outId = i;
                printf("found MIDI output: %s\n", caps.szPname);
                break;
            }
        }
    }

    // find input device
    UINT numIn = midiInGetNumDevs();
    UINT inId  = (UINT)-1;
    for (UINT i = 0; i < numIn; i++) {
        MIDIINCAPSA caps;
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            if (strstr(caps.szPname, "Launchpad") || strstr(caps.szPname, "launchpad")) {
                inId = i;
                printf("found MIDI input:  %s\n", caps.szPname);
                break;
            }
        }
    }

    if (outId == (UINT)-1 || inId == (UINT)-1) {
        fprintf(stderr, "error: Launchpad not found — is it plugged in?\n");
        return -1;
    }

    lp->win_mutex = (void *)CreateMutex(NULL, FALSE, NULL);
    if (!lp->win_mutex) {
        fprintf(stderr, "error: CreateMutex failed (%lu)\n", GetLastError());
        return -1;
    }

    HMIDIOUT hOut;
    if (midiOutOpen(&hOut, outId, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        fprintf(stderr, "error: midiOutOpen failed\n");
        CloseHandle((HANDLE)lp->win_mutex);
        return -1;
    }
    lp->win_out = (void *)hOut;

    HMIDIIN hIn;
    MMRESULT r = midiInOpen(&hIn, inId, (DWORD_PTR)midi_in_callback,
                             (DWORD_PTR)lp, CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR) {
        fprintf(stderr, "error: midiInOpen failed (%u)\n", r);
        midiOutClose(hOut);
        CloseHandle((HANDLE)lp->win_mutex);
        return -1;
    }
    lp->win_in = (void *)hIn;
    midiInStart(hIn);
    return 0;
}

void lp_close(Launchpad *lp) {
    if (lp->win_in) {
        midiInStop((HMIDIIN)lp->win_in);
        midiInClose((HMIDIIN)lp->win_in);
        lp->win_in = NULL;
    }
    if (lp->win_out) {
        midiOutReset((HMIDIOUT)lp->win_out);
        midiOutClose((HMIDIOUT)lp->win_out);
        lp->win_out = NULL;
    }
    if (lp->win_mutex) {
        CloseHandle((HANDLE)lp->win_mutex);
        lp->win_mutex = NULL;
    }
}

static void send3(Launchpad *lp, uint8_t a, uint8_t b, uint8_t c) {
    DWORD msg = (DWORD)a | ((DWORD)b << 8) | ((DWORD)c << 16);
    midiOutShortMsg((HMIDIOUT)lp->win_out, msg);
}

int lp_poll(Launchpad *lp, ButtonEvent *ev, int timeout_ms) {
    for (int elapsed = 0; elapsed <= timeout_ms; elapsed += 10) {
        WaitForSingleObject((HANDLE)lp->win_mutex, INFINITE);
        while (lp->ring_head != lp->ring_tail) {
            uint8_t b = lp->ring[lp->ring_tail];
            lp->ring_tail = (lp->ring_tail + 1) & 0xFF;
            if (b & 0x80) {
                lp->status = b; lp->nbuf = 0;
            } else if (lp->status) {
                lp->buf[lp->nbuf++] = b;
                if (lp->nbuf >= 2) {
                    int ok = parse_msg(lp->status, lp->buf[0], lp->buf[1], ev);
                    lp->nbuf = 0;
                    if (ok) {
                        ReleaseMutex((HANDLE)lp->win_mutex);
                        return 1;
                    }
                }
            }
        }
        ReleaseMutex((HANDLE)lp->win_mutex);
        Sleep(10);
    }
    return 0;
}

#else // Linux / ALSA

#include <strings.h>   // strcasestr
#include <poll.h>

static int find_launchpad(char *hw_out, size_t sz) {
    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0) {
        char ctl_name[16];
        snprintf(ctl_name, sizeof(ctl_name), "hw:%d", card);
        snd_ctl_t *ctl;
        if (snd_ctl_open(&ctl, ctl_name, 0) < 0) continue;

        snd_ctl_card_info_t *info;
        snd_ctl_card_info_alloca(&info);
        if (snd_ctl_card_info(ctl, info) < 0) { snd_ctl_close(ctl); continue; }

        const char *cname = snd_ctl_card_info_get_name(info);
        if (strcasestr(cname, "launchpad")) {
            int dev = -1;
            if (snd_ctl_rawmidi_next_device(ctl, &dev) == 0 && dev >= 0) {
                snprintf(hw_out, sz, "hw:%d,%d", card, dev);
                snd_ctl_close(ctl);
                return 0;
            }
        }
        snd_ctl_close(ctl);
    }
    return -1;
}

int lp_open(Launchpad *lp) {
    memset(lp, 0, sizeof(*lp));
    char hw[32];
    if (find_launchpad(hw, sizeof(hw)) < 0) {
        fprintf(stderr, "error: Launchpad not found — is it plugged in?\n");
        return -1;
    }
    printf("connecting to %s…\n", hw);
    int err = snd_rawmidi_open(&lp->in, &lp->out, hw, SND_RAWMIDI_NONBLOCK);
    if (err < 0) {
        fprintf(stderr, "error: cannot open rawmidi %s: %s\n", hw, snd_strerror(err));
        return -1;
    }
    // flush any stale bytes in the input buffer
    snd_rawmidi_read(lp->in, NULL, 0);
    return 0;
}

void lp_close(Launchpad *lp) {
    if (lp->in)  { snd_rawmidi_close(lp->in);  lp->in  = NULL; }
    if (lp->out) { snd_rawmidi_close(lp->out); lp->out = NULL; }
}

static void send3(Launchpad *lp, uint8_t a, uint8_t b, uint8_t c) {
    uint8_t msg[3] = {a, b, c};
    snd_rawmidi_write(lp->out, msg, 3);
    snd_rawmidi_drain(lp->out);
}

int lp_poll(Launchpad *lp, ButtonEvent *ev, int timeout_ms) {
    struct pollfd pfd;
    if (snd_rawmidi_poll_descriptors(lp->in, &pfd, 1) < 1) return -1;

    if (poll(&pfd, 1, timeout_ms) <= 0) return 0;

    uint8_t raw[64];
    ssize_t n = snd_rawmidi_read(lp->in, raw, sizeof(raw));
    if (n <= 0) return 0;

    for (int i = 0; i < (int)n; i++) {
        uint8_t b = raw[i];
        if (b & 0x80) {
            lp->status = b; lp->nbuf = 0;
        } else if (lp->status) {
            lp->buf[lp->nbuf++] = b;
            // CC and note_on/off are all 3-byte messages
            if (lp->nbuf >= 2) {
                if (parse_msg(lp->status, lp->buf[0], lp->buf[1], ev)) {
                    lp->nbuf = 0;
                    return 1;
                }
                lp->nbuf = 0;
            }
        }
    }
    return 0;
}

#endif // _WIN32

// shared: LED helpers (call send3 defined above)
void lp_set_top(Launchpad *lp, int col, uint8_t vel) {
    send3(lp, 0xB0, (uint8_t)(104 + col), vel);
}
void lp_set_side(Launchpad *lp, int row, uint8_t vel) {
    send3(lp, 0x90, (uint8_t)(row * 16 + 8), vel);
}
void lp_set_grid(Launchpad *lp, int row, int col, uint8_t vel) {
    send3(lp, 0x90, (uint8_t)(row * 16 + col), vel);
}

void lp_set_rc(Launchpad *lp, int row, int col, uint8_t vel) {
    if (row == 0 && col < 8)      lp_set_top(lp, col, vel);
    else if (col == 8 && row > 0) lp_set_side(lp, row - 1, vel);
    else if (row > 0 && col < 8)  lp_set_grid(lp, row - 1, col, vel);
}

void lp_set_button(Launchpad *lp, const char *id, uint8_t vel) {
    int row, col;
    if (lp_id_to_rc(id, &row, &col)) lp_set_rc(lp, row, col, vel);
}

int lp_id_to_rc(const char *id, int *row, int *col) {
    if (strncmp(id, "top_", 4) == 0) {
        *row = 0; *col = atoi(id + 4); return 1;
    } else if (strncmp(id, "side_", 5) == 0) {
        *row = atoi(id + 5) + 1; *col = 8; return 1;
    } else if (strncmp(id, "grid_", 5) == 0) {
        int r, c; sscanf(id + 5, "%d_%d", &r, &c); *row = r + 1; *col = c; return 1;
    }
    return 0;
}

void lp_clear(Launchpad *lp) {
    for (int col = 0; col < 8; col++) lp_set_top(lp, col, 0);
    for (int row = 0; row < 8; row++) {
        lp_set_side(lp, row, 0);
        for (int col = 0; col < 8; col++) lp_set_grid(lp, row, col, 0);
    }
}
