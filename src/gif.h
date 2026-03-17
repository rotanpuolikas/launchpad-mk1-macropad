#pragma once
#ifdef HAVE_GIF
#include <stdint.h>

typedef struct {
    uint8_t **frames;  // count pointers, each to 9*9*3 bytes (RGB)
    int      *delays;  // per-frame delay in milliseconds
    int       count;
} GifData;

GifData *gif_load(const char *path);
void     gif_free(GifData *gd);

#endif // HAVE_GIF
