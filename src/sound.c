/* Vortex's sound: the iPod game's 16-bit sounds mixed here and played through ALSA, the way
 * Q2 Pod's own q2video plays and q2-doom does (patch/video.c sound_open, docs/librespot.md):
 *
 *   - Q2 Pod passes the output as argv[1]: "plughw:0,0" (the headphone DAC), "plug:bluealsa" or
 *     "usb" (the USB-Audio card in /proc/asound/cards), and off the jack argv[2], the volume:
 *     "NN" (0-100, applied here as hciplayer's Bluetooth curve) or "hNN" (the DAC applies it).
 *   - hciplayer lets go of the PCM a moment after Q2 Pod stops it, so the open is retried (2 s on
 *     the jack, 5 s elsewhere, and set_params again while bluealsa answers EBUSY).
 *   - On the jack, the DAC's PCM mode and mute are cleared through /dev/shanling_dac after the
 *     open, and the mute set again on close, as hciplayer leaves a stopped DAC.
 *   - Mono Audio and Swap L/R follow /tmp/q2mono and /tmp/q2swap.
 *
 * The device is opened on the mixer thread, so the game starts drawing while it waits. */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "vx.h"

#define RATE 44100
#define CHUNK 256 /* frames a write, about 6 ms */
#define CHANNELS 16
#define LATENCY_JACK 60000
#define LATENCY_OTHER 200000 /* Bluetooth needs the slack, as q2video gives it */
#define DAC "/dev/shanling_dac"
#define DAC_PCM 0xc0044d1bu
#define DAC_MUTE 0xc0044d1fu
#define MONO_FILE "/tmp/q2mono"
#define SWAP_FILE "/tmp/q2swap"

/* alsa-lib, as q2video declares it */
int snd_pcm_open(void **, const char *, int, int), snd_pcm_close(void *), snd_pcm_drop(void *);
int snd_pcm_set_params(void *, int, int, unsigned, unsigned, int, unsigned);
int snd_pcm_recover(void *, int, int);
long snd_pcm_writei(void *, const void *, unsigned long);
const char *snd_strerror(int);
int pthread_timedjoin_np(pthread_t, void **, const struct timespec *);

static struct {
    const int16_t *data;
    unsigned len, pos, step; /* pos and step 16.16 */
    int left, right;         /* 0-256 */
    int loop;
} chan[CHANNELS];

static struct {
    char device[32];
    int gain; /* 1/65536; 65536 where the DAC applies the volume */
    int ok;   /* a device was given */
    void *pcm;
    int dac; /* /dev/shanling_dac on the jack, -1 */
    pthread_t thread;
    int running;
    volatile int stop, opened;
    volatile int *mono_on, *swap_on;
} snd = { .dac = -1 };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int master = 256;

void snd_master(int vol) { master = clampi(vol, 0, 256); }

/* hciplayer's Bluetooth soft volume for Q2 Pod's v (0-100), as q2video's bt_gain:
 * 0.002 v under 50, then 0.1 + 0.018 (v - 50), 1 at 100. "hNN", or none, is unity. */
static int gain_for(const char *volume) {
    if (!volume || volume[0] == 'h') return 65536;
    int v = atoi(volume);
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    return 65536 * (v < 50 ? 2 * v : 18 * v - 800) / 1000;
}

/* The mix of every playing channel, n stereo frames. Called with the lock held. */
static void mix(int16_t *out, unsigned frames) {
    static int acc[2 * CHUNK];
    memset(acc, 0, sizeof acc);
    for (int c = 0; c < CHANNELS; ++c) {
        if (!chan[c].data) continue;
        const int16_t *d = chan[c].data;
        unsigned pos = chan[c].pos, step = chan[c].step, len = chan[c].len;
        int l = chan[c].left, r = chan[c].right;
        for (unsigned f = 0; f < frames; ++f) {
            if (pos >> 16 >= len) {
                if (!chan[c].loop) {
                    chan[c].data = 0;
                    break;
                }
                pos -= len << 16;
            }
            int s = d[pos >> 16];
            acc[2 * f] += s * l >> 8;
            acc[2 * f + 1] += s * r >> 8;
            pos += step;
        }
        chan[c].pos = pos;
    }
    for (unsigned i = 0; i < 2 * frames; ++i) {
        int v = acc[i] * master >> 8; /* the game's sfx level */
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

/* /tmp/q2mono and /tmp/q2swap: Q2 Pod's ints, mapped once they exist */
static volatile int *map_flag(volatile int *have, const char *path) {
    if (have) return have;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    void *p = mmap(0, sizeof(int), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    return p == MAP_FAILED ? 0 : p;
}

/* "usb": the card /proc/asound/cards lists as USB-Audio, as plughw:N,0 */
static void usb_device(char *out, unsigned size) {
    char b[1024];
    int fd = open("/proc/asound/cards", O_RDONLY | O_CLOEXEC), n = fd >= 0 ? (int)read(fd, b, sizeof b - 1) : 0;
    if (fd >= 0) close(fd);
    b[n > 0 ? n : 0] = 0;
    char *u = strstr(b, "USB-Audio");
    while (u && u > b && u[-1] != '\n') --u; /* the line's start: " N [id ]: USB-Audio" */
    snprintf(out, size, "plughw:%d,0", u ? atoi(u) : 1);
}

static int sound_open(void) {
    char dev[32];
    int err = 0, off = 0;
    if (!strcmp(snd.device, "usb")) usb_device(dev, sizeof dev);
    else snprintf(dev, sizeof dev, "%s", snd.device);
    int jack = !strcmp(dev, "plughw:0,0");
    for (int i = 0; i < (jack ? 20 : 50) && !snd.stop && (err = snd_pcm_open(&snd.pcm, dev, 0, 0)); ++i)
        snd.pcm = 0, usleep(100000);
    if (!snd.pcm) {
        fprintf(stderr, "Sound: can't open %s: %s\n", dev, snd_strerror(err));
        return 0;
    }
    /* S16_LE, RW_INTERLEAVED, stereo, resampled as needed; bluealsa claims its PCM here */
    for (int i = 0; (err = snd_pcm_set_params(snd.pcm, 2, 3, 2, RATE, 1, jack ? LATENCY_JACK : LATENCY_OTHER)) == -EBUSY &&
                    !jack && i < 50 && !snd.stop;
         ++i)
        usleep(100000);
    if (err) {
        fprintf(stderr, "Sound: %s: %s\n", dev, snd_strerror(err));
        snd_pcm_close(snd.pcm);
        snd.pcm = 0;
        return 0;
    }
    if (jack && (snd.dac = open(DAC, O_RDWR | O_CLOEXEC)) >= 0)
        ioctl(snd.dac, DAC_PCM, &off), ioctl(snd.dac, DAC_MUTE, &off);
    printf("Sound: %s at %d Hz\n", dev, RATE);
    return 1;
}

static void *mixer(void *unused) {
    (void)unused;
    int16_t buf[2 * CHUNK];
    if (!sound_open()) return 0;
    snd.opened = 1;
    for (unsigned n = 0; !snd.stop; ++n) {
        if (!(n % 200)) snd.mono_on = map_flag(snd.mono_on, MONO_FILE), snd.swap_on = map_flag(snd.swap_on, SWAP_FILE);
        pthread_mutex_lock(&lock);
        mix(buf, CHUNK);
        pthread_mutex_unlock(&lock);
        int gain = snd.gain, mono = snd.mono_on && *snd.mono_on, swap = snd.swap_on && *snd.swap_on;
        for (unsigned i = 0; i < 2 * CHUNK; i += 2) {
            int l = buf[i] * gain >> 16, r = buf[i + 1] * gain >> 16;
            if (mono) l = r = (l + r) >> 1;
            buf[i] = (int16_t)(swap ? r : l), buf[i + 1] = (int16_t)(swap ? l : r);
        }
        long w = snd_pcm_writei(snd.pcm, buf, CHUNK);
        if (w < 0 && snd_pcm_recover(snd.pcm, (int)w, 1) < 0) usleep(20000);
    }
    return 0;
}

void snd_start(const char *device, const char *volume) {
    snd.ok = device && device[0] && strcmp(device, "-");
    if (!snd.ok) {
        printf("Sound: none (no output given)\n");
        return;
    }
    snprintf(snd.device, sizeof snd.device, "%s", device);
    snd.gain = gain_for(volume);
    snd.stop = 0;
    snd.running = !pthread_create(&snd.thread, 0, mixer, 0);
}

void snd_shutdown(void) {
    if (!snd.running) return;
    snd.stop = 1;
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += 1;
    /* an output that takes no sound can hold the writer in ALSA: leave it, the process ends */
    if (pthread_timedjoin_np(snd.thread, 0, &t)) return;
    snd.running = 0;
    if (snd.pcm) snd_pcm_drop(snd.pcm), snd_pcm_close(snd.pcm), snd.pcm = 0;
    int on = 1;
    if (snd.dac >= 0) ioctl(snd.dac, DAC_MUTE, &on), close(snd.dac), snd.dac = -1;
}

static void levels(int c, int vol, int pan) {
    vol = clampi(vol, 0, 256);
    pan = clampi(pan, -256, 256);
    chan[c].left = vol * (pan > 0 ? 256 - pan : 256) >> 8;
    chan[c].right = vol * (pan < 0 ? 256 + pan : 256) >> 8;
}

int snd_play(const Sound *s, int vol, int pan, int loop) {
    if (!snd.running || !s || !s->data || !s->len || vol <= 0) return -1;
    pthread_mutex_lock(&lock);
    /* a free channel, else the one nearest its end that isn't looping */
    int pick = -1;
    unsigned best = 0xffffffffu;
    for (int c = 0; c < CHANNELS; ++c) {
        if (!chan[c].data) {
            pick = c;
            break;
        }
        if (chan[c].loop) continue;
        unsigned left = ((chan[c].len << 16) - chan[c].pos) / (chan[c].step ? chan[c].step : 1);
        if (left < best) best = left, pick = c;
    }
    if (pick >= 0) {
        chan[pick].data = s->data;
        chan[pick].len = s->len;
        chan[pick].pos = 0;
        chan[pick].step = (unsigned)(((uint64_t)s->rate << 16) / RATE);
        chan[pick].loop = loop;
        levels(pick, vol, pan);
    }
    pthread_mutex_unlock(&lock);
    return pick;
}

void snd_stop(int c) {
    if (c < 0 || c >= CHANNELS) return;
    pthread_mutex_lock(&lock);
    chan[c].data = 0;
    pthread_mutex_unlock(&lock);
}

void snd_stop_all(void) {
    pthread_mutex_lock(&lock);
    for (int c = 0; c < CHANNELS; ++c) chan[c].data = 0;
    pthread_mutex_unlock(&lock);
}
