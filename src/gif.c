#ifdef HAVE_GIF

/*
 * stb_image and stb_image_resize2 are header-only
 * define their implementations exactly once here
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include "gif.h"
#include "colours.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GifData *gif_load(const char *path, int mode) {
    // read the whole file into memory so we can use stbi_load_gif_from_memory
    FILE *f = fopen(path, "rb");
    if (!f) { perror("gif_load: fopen"); return NULL; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *raw = malloc((size_t)fsize);
    if (!raw) { fclose(f); return NULL; }
    fread(raw, 1, (size_t)fsize, f);
    fclose(f);

    int x = 0, y = 0, z = 0, comp = 0;
    int *delays = NULL;

    // fry animated GIF
    unsigned char *pixels = stbi_load_gif_from_memory(raw, (int)fsize,
                                                       &delays, &x, &y, &z, &comp, 3);
    if (!pixels) {
        // fall back: single-frame image (PNG, JPEG, static GIF …)
        pixels = stbi_load_from_memory(raw, (int)fsize, &x, &y, &comp, 3);
        z = (pixels) ? 1 : 0;
        delays = NULL;
    }
    free(raw);

    if (!pixels) {
        fprintf(stderr, "error: cannot decode image: %s\n", stbi_failure_reason());
        return NULL;
    }

    GifData *gd = calloc(1, sizeof(GifData));
    gd->count  = z;
    gd->frames = calloc((size_t)z, sizeof(uint8_t *));
    gd->delays = calloc((size_t)z, sizeof(int));

    uint8_t tmp[9 * 9 * 3];
    for (int i = 0; i < z; i++) {
        // resize to 9x9 RGB into a temporary buffer
        stbir_resize_uint8_linear(pixels + i * x * y * 3, x, y, 0,
                                  tmp, 9, 9, 0, STBIR_RGB);
        // pre-map every pixel to the closest Launchpad velocity
        gd->frames[i] = malloc(9 * 9);
        for (int p = 0; p < 9 * 9; p++)
            gd->frames[i][p] = closest_colour_vel(tmp[p*3], tmp[p*3+1], tmp[p*3+2], mode);
        gd->delays[i] = (delays && delays[i] > 0) ? delays[i] : 100;
    }

    stbi_image_free(pixels);
    free(delays);  // allocated by stbi with STBI_MALLOC = malloc
    return gd;
}

void gif_free(GifData *gd) {
    if (!gd) return;
    for (int i = 0; i < gd->count; i++) free(gd->frames[i]);
    free(gd->frames);
    free(gd->delays);
    free(gd);
}

#endif // HAVE_GIF
