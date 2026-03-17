#pragma once
#include <stdint.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>

typedef struct {
    void   *win_in;       // HMIDIIN
    void   *win_out;      // HMIDIOUT
    void   *win_mutex;    // HANDLE
    uint8_t ring[256];
    int     ring_head;
    int     ring_tail;
    // running-status MIDI parser state
    uint8_t status;
    uint8_t buf[2];
    int     nbuf;
} Launchpad;

#else
#  include <alsa/asoundlib.h>

typedef struct {
    snd_rawmidi_t *in;
    snd_rawmidi_t *out;
    // running-status MIDI parser state
    uint8_t status;
    uint8_t buf[2];
    int     nbuf;
} Launchpad;
#endif

/* open the first MIDI device whose name contains "launchpad"
 * returns 0 on success, -1 on failure */
int  lp_open(Launchpad *lp);
void lp_close(Launchpad *lp);

// set all LEDs off
void lp_clear(Launchpad *lp);

// LED control, all take a MIDI velocity byte (colour)
void lp_set_top(Launchpad *lp, int col, uint8_t vel);   // col 0-7
void lp_set_side(Launchpad *lp, int row, uint8_t vel);  // row 0-7
void lp_set_grid(Launchpad *lp, int row, int col, uint8_t vel);  // row,col 0-7

// set LED by button-id string: "top_N", "side_N", "grid_R_C"
void lp_set_button(Launchpad *lp, const char *id, uint8_t vel);

/*
 * set LED by full 9×9 coordinate (row=0 → top row, col=8 -> side column)
 * ignores (0,8) corner
 */
void lp_set_rc(Launchpad *lp, int row, int col, uint8_t vel);

// convert button id to 9×9 (row,col), returns 1 on success
int lp_id_to_rc(const char *id, int *row, int *col);

typedef struct {
    char id[16];  // "top_N", "side_N", "grid_R_C"
    int  pressed;
} ButtonEvent;

/*
 * poll for one MIDI button event, blocks up to timeout_ms milliseconds
 * returns 1 if *ev was filled, 0 for timeout, -1 on error
 */
int lp_poll(Launchpad *lp, ButtonEvent *ev, int timeout_ms);
