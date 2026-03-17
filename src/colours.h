#pragma once
#include <stdint.h>

// velocity byte encoding: vel = red + (green << 4), each channel 0-3
#define COLOUR_BLACK       0x00
#define COLOUR_RED_LOW     0x01
#define COLOUR_RED_MED     0x02
#define COLOUR_RED_MAX     0x03
#define COLOUR_GREEN_LOW   0x10
#define COLOUR_GREEN_MED   0x20
#define COLOUR_GREEN_MAX   0x30
#define COLOUR_YELLOW_LOW  0x11
#define COLOUR_YELLOW_MED  0x22
#define COLOUR_YELLOW_MAX  0x33

typedef struct {
    const char *name;
    uint8_t     r, g, b;   // approximate RGB for GIF mapping
    uint8_t     vel;       // MIDI velocity byte
} PaletteEntry;

extern const PaletteEntry PALETTE[];
extern const int           PALETTE_COUNT;

// exact name lookup, returns COLOUR_BLACK (0) for unknown names 
uint8_t colour_velocity(const char *name);

/*
 * find the nearest Launchpad colour for an RGB pixel
 * mode: 0=full, 1=red-only, 2=green-only, 3=yellow-only
 */
uint8_t closest_colour_vel(uint8_t r, uint8_t g, uint8_t b, int mode);
