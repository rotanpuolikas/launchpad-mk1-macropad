#include "colours.h"
#include <string.h>
#include <limits.h>

static const struct { const char *name; uint8_t vel; } COLOUR_TABLE[] = {
    {"black",      COLOUR_BLACK},
    {"red_low",    COLOUR_RED_LOW},
    {"red_med",    COLOUR_RED_MED},
    {"red_max",    COLOUR_RED_MAX},
    {"green_low",  COLOUR_GREEN_LOW},
    {"green_med",  COLOUR_GREEN_MED},
    {"green_max",  COLOUR_GREEN_MAX},
    {"yellow_low", COLOUR_YELLOW_LOW},
    {"yellow_med", COLOUR_YELLOW_MED},
    {"yellow_max", COLOUR_YELLOW_MAX},
};

const PaletteEntry PALETTE[] = {
    {"black",      0,   0,   0,   COLOUR_BLACK},
    {"red_low",    85,  0,   0,   COLOUR_RED_LOW},
    {"red_med",    170, 0,   0,   COLOUR_RED_MED},
    {"red_max",    255, 0,   0,   COLOUR_RED_MAX},
    {"green_low",  0,   85,  0,   COLOUR_GREEN_LOW},
    {"green_med",  0,   170, 0,   COLOUR_GREEN_MED},
    {"green_max",  0,   255, 0,   COLOUR_GREEN_MAX},
    {"yellow_low", 85,  85,  0,   COLOUR_YELLOW_LOW},
    {"yellow_med", 170, 170, 0,   COLOUR_YELLOW_MED},
    {"yellow_max", 255, 255, 0,   COLOUR_YELLOW_MAX},
};

const int PALETTE_COUNT = (int)(sizeof(PALETTE) / sizeof(PALETTE[0]));

uint8_t colour_velocity(const char *name) {
    for (int i = 0; i < (int)(sizeof(COLOUR_TABLE)/sizeof(COLOUR_TABLE[0])); i++)
        if (strcmp(COLOUR_TABLE[i].name, name) == 0)
            return COLOUR_TABLE[i].vel;
    return COLOUR_BLACK;
}

uint8_t closest_colour_vel(uint8_t r, uint8_t g, uint8_t b, int mode) {
    long best_dist = LONG_MAX;
    uint8_t best_vel = COLOUR_BLACK;
    for (int i = 0; i < PALETTE_COUNT; i++) {
        const PaletteEntry *e = &PALETTE[i];
        // mode filtering, keep black always
        if (mode == 1 && strncmp(e->name, "red",    3) != 0 && strcmp(e->name, "black") != 0) continue;
        if (mode == 2 && strncmp(e->name, "green",  5) != 0 && strcmp(e->name, "black") != 0) continue;
        if (mode == 3 && strncmp(e->name, "yellow", 6) != 0 && strcmp(e->name, "black") != 0) continue;
        long dr = (long)r - e->r;
        long dg = (long)g - e->g;
        long db = (long)b - e->b;
        long dist = dr*dr + dg*dg + db*db;
        if (dist < best_dist) { best_dist = dist; best_vel = e->vel; }
    }
    return best_vel;
}
