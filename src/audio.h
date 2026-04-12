#pragma once

#ifdef HAVE_PULSEAUDIO

#define AUDIO_FFT_SIZE    1024
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_MAX_BANDS   64

typedef struct AudioCapture AudioCapture;

/* Open a capture stream.  device may be NULL (use default source) or a PulseAudio
   source name such as "alsa_output.pci-0000_00_1b.0.analog-stereo.monitor". */
AudioCapture *audio_open(const char *device);
void          audio_close(AudioCapture *ac);

/* Copy current normalised band amplitudes [0.0, 1.0] into out[0..n_bands-1].
   n_bands must be <= AUDIO_MAX_BANDS. */
void          audio_get_bands(AudioCapture *ac, float *out, int n_bands);

#endif /* HAVE_PULSEAUDIO */
