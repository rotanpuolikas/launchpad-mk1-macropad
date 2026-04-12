#include "audio.h"

#ifdef HAVE_PULSEAUDIO

#include <pulse/simple.h>
#include <pulse/error.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* Simple complex number used for in-place FFT */
typedef struct { float r, i; } Cpx;

/* Cooley-Tukey radix-2 DIT FFT, in-place.  N must be a power of 2. */
static void fft(Cpx *x, int N) {
    /* bit-reversal permutation */
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { Cpx t = x[i]; x[i] = x[j]; x[j] = t; }
    }
    /* butterfly stages */
    for (int len = 2; len <= N; len <<= 1) {
        float ang   = -2.0f * 3.14159265358979f / (float)len;
        Cpx   wlen  = { cosf(ang), sinf(ang) };
        for (int i = 0; i < N; i += len) {
            Cpx w = { 1.0f, 0.0f };
            for (int j = 0; j < len / 2; j++) {
                Cpx u = x[i + j];
                Cpx v = { x[i+j+len/2].r*w.r - x[i+j+len/2].i*w.i,
                           x[i+j+len/2].r*w.i + x[i+j+len/2].i*w.r };
                x[i + j]           = (Cpx){ u.r + v.r, u.i + v.i };
                x[i + j + len/2]   = (Cpx){ u.r - v.r, u.i - v.i };
                float wr = w.r*wlen.r - w.i*wlen.i;
                w.i      = w.r*wlen.i + w.i*wlen.r;
                w.r      = wr;
            }
        }
    }
}

struct AudioCapture {
    pa_simple       *pa;
    pthread_t        thread;
    pthread_mutex_t  mutex;
    volatile int     running;
    float            bands[AUDIO_MAX_BANDS];
};

static void *audio_thread(void *arg) {
    AudioCapture *ac = (AudioCapture *)arg;

    int16_t pcm[AUDIO_FFT_SIZE];
    Cpx     fft_buf[AUDIO_FFT_SIZE];

    while (ac->running) {
        int err = 0;
        if (pa_simple_read(ac->pa, pcm, sizeof(pcm), &err) < 0) {
            /* non-fatal — just retry after a short delay */
            struct timespec ts = { 0, 50000000L };
            nanosleep(&ts, NULL);
            continue;
        }

        /* Hann window + copy to FFT input */
        for (int i = 0; i < AUDIO_FFT_SIZE; i++) {
            float w  = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (AUDIO_FFT_SIZE - 1)));
            fft_buf[i].r = (float)pcm[i] / 32768.0f * w;
            fft_buf[i].i = 0.0f;
        }

        fft(fft_buf, AUDIO_FFT_SIZE);

        /* Compute magnitudes for the positive half of the spectrum */
        float mags[AUDIO_FFT_SIZE / 2];
        float peak = 1e-9f;
        for (int i = 0; i < AUDIO_FFT_SIZE / 2; i++) {
            mags[i] = sqrtf(fft_buf[i].r * fft_buf[i].r + fft_buf[i].i * fft_buf[i].i);
            if (mags[i] > peak) peak = mags[i];
        }

        /* Aggregate into AUDIO_MAX_BANDS bands using logarithmic spacing (20 Hz – 22 kHz) */
        float new_bands[AUDIO_MAX_BANDS];
        const float log_lo = logf(20.0f);
        const float log_hi = logf((float)(AUDIO_SAMPLE_RATE / 2));
        for (int b = 0; b < AUDIO_MAX_BANDS; b++) {
            float f_lo  = expf(log_lo + (log_hi - log_lo) * (float)b       / AUDIO_MAX_BANDS);
            float f_hi  = expf(log_lo + (log_hi - log_lo) * (float)(b + 1) / AUDIO_MAX_BANDS);
            int   blo   = (int)(f_lo * AUDIO_FFT_SIZE / AUDIO_SAMPLE_RATE);
            int   bhi   = (int)(f_hi * AUDIO_FFT_SIZE / AUDIO_SAMPLE_RATE);
            if (blo >= AUDIO_FFT_SIZE / 2) blo = AUDIO_FFT_SIZE / 2 - 1;
            if (bhi >= AUDIO_FFT_SIZE / 2) bhi = AUDIO_FFT_SIZE / 2 - 1;
            if (bhi < blo) bhi = blo;
            float sum = 0.0f;
            int   cnt = 0;
            for (int i = blo; i <= bhi; i++) { sum += mags[i]; cnt++; }
            new_bands[b] = cnt > 0 ? sum / (float)cnt / peak : 0.0f;
            if (new_bands[b] > 1.0f) new_bands[b] = 1.0f;
        }

        pthread_mutex_lock(&ac->mutex);
        memcpy(ac->bands, new_bands, sizeof(float) * AUDIO_MAX_BANDS);
        pthread_mutex_unlock(&ac->mutex);
    }
    return NULL;
}

AudioCapture *audio_open(const char *device) {
    static const pa_sample_spec ss = {
        .format   = PA_SAMPLE_S16LE,
        .rate     = AUDIO_SAMPLE_RATE,
        .channels = 1
    };
    int err = 0;
    pa_simple *pa = pa_simple_new(
        NULL,               /* PulseAudio server    */
        "launc-macro",      /* application name     */
        PA_STREAM_RECORD,
        (device && device[0]) ? device : NULL, /* source device */
        "equalizer",        /* stream description   */
        &ss,
        NULL,               /* channel map          */
        NULL,               /* buffering attributes */
        &err
    );
    if (!pa) {
        fprintf(stderr, "audio_open: pa_simple_new failed: %s\n", pa_strerror(err));
        return NULL;
    }

    AudioCapture *ac = calloc(1, sizeof(*ac));
    if (!ac) { pa_simple_free(pa); return NULL; }
    ac->pa      = pa;
    ac->running = 1;
    pthread_mutex_init(&ac->mutex, NULL);
    pthread_create(&ac->thread, NULL, audio_thread, ac);
    return ac;
}

void audio_close(AudioCapture *ac) {
    if (!ac) return;
    ac->running = 0;
    pthread_join(ac->thread, NULL);
    pa_simple_free(ac->pa);
    pthread_mutex_destroy(&ac->mutex);
    free(ac);
}

void audio_get_bands(AudioCapture *ac, float *out, int n_bands) {
    if (!ac || !out || n_bands <= 0) return;
    if (n_bands > AUDIO_MAX_BANDS) n_bands = AUDIO_MAX_BANDS;
    pthread_mutex_lock(&ac->mutex);
    memcpy(out, ac->bands, sizeof(float) * (size_t)n_bands);
    pthread_mutex_unlock(&ac->mutex);
}

#endif /* HAVE_PULSEAUDIO */
