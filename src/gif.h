#pragma once
#ifdef HAVE_GIF
#include <stdint.h>

typedef struct {
    uint8_t **frames;  // count pointers, each to 9*9 bytes (pre-mapped velocity)
    int      *delays;  // per-frame delay in milliseconds
    int       count;
} GifData;

GifData *gif_load(const char *path, int mode);
void     gif_free(GifData *gd);

#endif // HAVE_GIF
