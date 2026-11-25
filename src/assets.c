/* The iPod game's files, as worked out for this port (the .ipg's executable is FairPlay-encrypted
 * and isn't read):
 *
 *   pictures   Backgrounds/NAME.ipd, UI3/NAME.ipd: a 16-byte header (width, height, format, 0 as
 *              little-endian words) and the pixels, top row first.
 *   textures   "tex": the entry count, the largest entry's size, then each entry's size, then the
 *              entries, each a picture as above but stored bottom row first.
 *   formats    0x1001 RGB565; 0x1003 RGBA4444; 0x2005 RGBA8888; 0x0800 and 0x1000 one byte of
 *              alpha (white); 0x0809 a 256-colour RGBA palette, then one byte a pixel.
 *   sounds     media/sfx: the count, the largest size, each entry's size, then the entries, each a
 *              RIFF WAV (16-bit PCM, mono but one, 22-32 kHz). They carry no names.
 *   levels     gamedata/levels/levels: 40 records of 216 bytes from byte 2, little-endian words:
 *              5 rings x 16 bricks, the rings' turning speeds, then the time to beat (word 95) and
 *              the ball's speed and top speed (98, 104).
 *   words      Localization/<lang>.lproj/text.strings: "id"="text"; lines, // comments.
 *   launch     Vortex.raw.lcd5: 320 x 216 RGB565 after a 16-byte header. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vx.h"

static unsigned u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned u16(const uint8_t *p) { return p[0] | p[1] << 8; }

void img_free(Img *i) {
    free(i->px);
    i->px = 0, i->w = i->h = 0;
}

static uint32_t c565(unsigned v) {
    unsigned r = v >> 11, g = v >> 5 & 63, b = v & 31;
    return 0xff000000u | (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

/* pixels of one picture into ARGB; 0 if the format or size is unknown */
static int decode(Img *out, unsigned w, unsigned h, unsigned fmt, const uint8_t *p, unsigned len) {
    out->w = out->h = 0, out->px = 0;
    if (!w || !h || w > 4096 || h > 4096) return 0;
    unsigned n = w * h, need = fmt == 0x2005 ? 4 * n : fmt == 0x1001 || fmt == 0x1003 ? 2 * n : fmt == 0x0809 ? 1024 + n
                             : fmt == 0x0800 || fmt == 0x1000 ? n : 0;
    if (!need || len < need) return 0;
    uint32_t *px = malloc(n * 4);
    if (!px) return 0;
    for (unsigned i = 0; i < n; ++i) {
        switch (fmt) {
        case 0x2005: px[i] = (uint32_t)p[4 * i + 3] << 24 | p[4 * i] << 16 | p[4 * i + 1] << 8 | p[4 * i + 2]; break;
        case 0x1001: px[i] = c565(u16(p + 2 * i)); break;
        case 0x1003: {
            unsigned v = u16(p + 2 * i);
            px[i] = (v & 15) * 17u << 24 | (v >> 12) * 17u << 16 | (v >> 8 & 15) * 17u << 8 | (v >> 4 & 15) * 17u;
            break;
        }
        case 0x0809: {
            const uint8_t *c = p + 4 * p[1024 + i];
            px[i] = (uint32_t)c[3] << 24 | c[0] << 16 | c[1] << 8 | c[2];
            break;
        }
        default: px[i] = (uint32_t)p[i] << 24 | 0xffffff; break;
        }
    }
    out->w = (int)w, out->h = (int)h, out->px = px;
    return 1;
}

Img img_ipd(Zip *z, const char *member) {
    Img im = { 0 };
    unsigned size;
    uint8_t *d = zip_read(z, member, &size);
    if (d && size >= 16 && !decode(&im, u32(d), u32(d + 4), u32(d + 8), d + 16, size - 16))
        logf_("%s: format %#x %u x %u not known", member, u32(d + 8), u32(d), u32(d + 4));
    if (!d) logf_("%s: missing", member);
    free(d);
    return im;
}

Img img_lcd5(Zip *z, const char *member) {
    Img im = { 0 };
    unsigned size;
    uint8_t *d = zip_read(z, member, &size);
    if (d && size >= 16) {
        unsigned w = u32(d), h = u32(d + 4);
        if (w && h && w <= 640 && h <= 480 && 16 + 2 * w * h <= size) decode(&im, w, h, 0x1001, d + 16, size - 16);
    }
    free(d);
    return im;
}

/* bilinear, scaled to cover VIEW_W x VIEW_H and centred */
Img img_cover(const Img *src) {
    Img o = { 0 };
    if (!src || !src->px) return o;
    o.px = malloc(VIEW_W * VIEW_H * 4);
    if (!o.px) return o;
    o.w = VIEW_W, o.h = VIEW_H;
    float s = (float)VIEW_W / src->w > (float)VIEW_H / src->h ? (float)VIEW_W / src->w : (float)VIEW_H / src->h;
    float ox = (src->w - VIEW_W / s) / 2, oy = (src->h - VIEW_H / s) / 2;
    for (int y = 0; y < VIEW_H; ++y) {
        float fy = clampf(oy + (y + 0.5f) / s - 0.5f, 0, src->h - 1.001f);
        int y0 = (int)fy, ay = (int)((fy - y0) * 256);
        const uint32_t *r0 = src->px + y0 * src->w, *r1 = src->px + (y0 + 1 < src->h ? y0 + 1 : y0) * src->w;
        for (int x = 0; x < VIEW_W; ++x) {
            float fx = clampf(ox + (x + 0.5f) / s - 0.5f, 0, src->w - 1.001f);
            int x0 = (int)fx, ax = (int)((fx - x0) * 256), x1 = x0 + 1 < src->w ? x0 + 1 : x0;
            uint32_t out = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                int a = (int)(r0[x0] >> sh & 255), b = (int)(r0[x1] >> sh & 255), c = (int)(r1[x0] >> sh & 255), d = (int)(r1[x1] >> sh & 255);
                out |= (uint32_t)(((a * (256 - ax) + b * ax) * (256 - ay) + (c * (256 - ax) + d * ax) * ay) >> 16) << sh;
            }
            o.px[y * VIEW_W + x] = out;
        }
    }
    return o;
}

/* ---------------------------------------------------------------------------------------------
 * Banks: a count, the largest size, the sizes, the entries */

/* where each entry starts and its size, for a bank of size bytes; the count, 0 if it doesn't add up.
 * Every size is listed; a bank listing all but the last is taken too. */
static int bank_layout(const uint8_t *d, unsigned size, unsigned *off, unsigned *len, int max) {
    if (size < 8) return 0;
    unsigned n = u32(d);
    if (!n || (int)n > max) return 0;
    for (unsigned listed = n; listed + 1 >= n && listed; --listed) { /* every size listed, or all but the last */
        unsigned at = 8 + 4 * listed, sum = 0;
        if (at > size) continue;
        for (unsigned i = 0; i < listed; ++i) sum += u32(d + 8 + 4 * i);
        if (at + sum > size || (listed == n && at + sum != size)) continue;
        for (unsigned i = 0; i < n; ++i) {
            off[i] = at;
            len[i] = i < listed ? u32(d + 8 + 4 * i) : size - at;
            at += len[i];
        }
        return (int)n;
    }
    return 0;
}

#define MAX_TEX 64
static Img texs[MAX_TEX];
static int ntex;

int tex_load(Zip *z, const char *member) {
    unsigned size, off[MAX_TEX], len[MAX_TEX];
    uint8_t *d = zip_read(z, member, &size);
    int n = d ? bank_layout(d, size, off, len, MAX_TEX) : 0;
    for (int i = 0; i < n; ++i) {
        const uint8_t *e = d + off[i];
        if (len[i] < 16 || !decode(&texs[i], u32(e), u32(e + 4), u32(e + 8), e + 16, len[i] - 16))
            logf_("tex %d: format %#x %u x %u not read", i, len[i] >= 16 ? u32(e + 8) : 0, len[i] >= 16 ? u32(e) : 0,
                  len[i] >= 16 ? u32(e + 4) : 0);
        /* the bank's pictures are stored bottom row first (OpenGL's way): turned the right way up */
        Img *t = &texs[i];
        for (int y = 0; t->px && y < t->h / 2; ++y)
            for (int x = 0; x < t->w; ++x) {
                uint32_t a = t->px[y * t->w + x];
                t->px[y * t->w + x] = t->px[(t->h - 1 - y) * t->w + x];
                t->px[(t->h - 1 - y) * t->w + x] = a;
            }
    }
    ntex = n;
    free(d);
    logf_("%s: %d textures", member, n);
    return n;
}

static const Img none_img;
const Img *tex(int i) { return i >= 0 && i < ntex && texs[i].px ? &texs[i] : &none_img; }
int tex_count(void) { return ntex; }

Spr spr_cell(const Img *im, int cols, int rows, int i) {
    Spr s = { im, 0, 0, 0, 0 };
    if (!im || !im->px || cols < 1 || rows < 1) return s;
    int c = i % cols, r = i / cols;
    s.x = (short)(c * im->w / cols), s.y = (short)(r * im->h / rows);
    s.w = (short)((c + 1) * im->w / cols - s.x), s.h = (short)((r + 1) * im->h / rows - s.y);
    return s;
}

Spr spr_whole(const Img *im) { return spr_cell(im, 1, 1, 0); }

/* a cell numbered as the sheet is stored (bottom row first), in the sheet turned upright */
Spr spr_cell_up(const Img *im, int cols, int rows, int i) { return spr_cell(im, cols, rows, (rows - 1 - i / cols) * cols + i % cols); }

/* ---------------------------------------------------------------------------------------------
 * Sounds */

#define MAX_SFX 64
static Sound sfxs[MAX_SFX];
static int16_t *sfx_data;
static int nsfx;

int sfx_load(Zip *z, const char *member) {
    unsigned size, off[MAX_SFX], len[MAX_SFX];
    uint8_t *d = zip_read(z, member, &size);
    int n = d ? bank_layout(d, size, off, len, MAX_SFX) : 0;
    sfx_data = n ? malloc(size) : 0; /* mono samples take no more room than the bank */
    unsigned used = 0;
    for (int i = 0; i < n && sfx_data; ++i) {
        const uint8_t *e = d + off[i], *end = e + len[i];
        if (len[i] < 12 || memcmp(e, "RIFF", 4) || memcmp(e + 8, "WAVE", 4)) continue;
        unsigned ch = 0, rate = 0, bits = 0;
        for (const uint8_t *c = e + 12; c + 8 <= end;) {
            unsigned cl = u32(c + 4);
            const uint8_t *body = c + 8;
            if (cl > (unsigned)(end - body)) cl = (unsigned)(end - body);
            if (!memcmp(c, "fmt ", 4) && cl >= 16) ch = u16(body + 2), rate = u32(body + 4), bits = u16(body + 14);
            if (!memcmp(c, "data", 4) && ch && bits == 16 && rate) {
                unsigned frames = cl / (2 * ch);
                int16_t *out = sfx_data + used;
                for (unsigned f = 0; f < frames; ++f) {
                    int v = 0;
                    for (unsigned k = 0; k < ch; ++k) v += (int16_t)u16(body + 2 * (f * ch + k));
                    out[f] = (int16_t)(v / (int)ch);
                }
                sfxs[i].data = out, sfxs[i].len = frames, sfxs[i].rate = rate;
                used += frames;
                break;
            }
            c = body + cl + (cl & 1);
        }
    }
    nsfx = n;
    free(d);
    logf_("%s: %d sounds", member, n);
    return n;
}

const Sound *sfx(int i) { return i >= 0 && i < nsfx && sfxs[i].data ? &sfxs[i] : 0; }
int sfx_count(void) { return nsfx; }

/* ---------------------------------------------------------------------------------------------
 * Words */

#define MAX_STR 256
static struct {
    int id;
    char *s;
} strs[MAX_STR];
static int nstr;

int strings_load(Zip *z, const char *member) {
    unsigned size;
    char *d = (char *)zip_read(z, member, &size);
    if (!d) return 0;
    char *p = d, *end = d + size;
    if (size >= 3 && (uint8_t)p[0] == 0xef && (uint8_t)p[1] == 0xbb && (uint8_t)p[2] == 0xbf) p += 3;
    while (p < end && nstr < MAX_STR) {
        char *nl = memchr(p, '\n', (size_t)(end - p));
        char *le = nl ? nl : end;
        /* "id"="text"; */
        if (*p == '"') {
            int id = atoi(p + 1);
            char *q = memchr(p + 1, '"', (size_t)(le - p - 1));
            if (q && q + 2 < le && q[1] == '=' && q[2] == '"') {
                char *s = q + 3, *t = le;
                while (t > s && t[-1] != '"') --t; /* the last quote on the line */
                if (t > s) {
                    int n = (int)(t - 1 - s);
                    char *out = malloc((size_t)n + 1);
                    int k = 0;
                    for (int i = 0; i < n; ++i) {
                        if (s[i] == '\\' && i + 1 < n) ++i;
                        out[k++] = s[i];
                    }
                    out[k] = 0;
                    if (id == -1) {
                        free(out);
                        break;
                    }
                    strs[nstr].id = id, strs[nstr].s = out, ++nstr;
                }
            }
        }
        p = le + 1;
    }
    free(d);
    logf_("%s: %d strings", member, nstr);
    return nstr;
}

const char *str(int id, const char *fallback) {
    for (int i = 0; i < nstr; ++i)
        if (strs[i].id == id) return strs[i].s;
    return fallback;
}

/* ---------------------------------------------------------------------------------------------
 * Levels */

#define LEVEL_BYTES 216
#define MAX_LEVELS 64
static Level levels[MAX_LEVELS];
static int nlevels;

int levels_load(Zip *z, const char *member) {
    unsigned size;
    uint8_t *d = zip_read(z, member, &size);
    if (!d) return 0;
    uint8_t *e = calloc(size + 2, 1); /* the last record runs two bytes past the end */
    memcpy(e, d, size);
    for (unsigned at = 2; at + LEVEL_BYTES <= size + 2 && nlevels < MAX_LEVELS; at += LEVEL_BYTES) {
        Level *l = &levels[nlevels++];
        const uint8_t *w = e + at;
        for (int r = 0; r < RINGS; ++r)
            for (int s = 0; s < SEGS; ++s) l->brick[r][s] = (uint16_t)u16(w + 2 * (r * SEGS + s));
        for (int r = 0; r < RINGS; ++r) l->spin[r] = (short)u16(w + 2 * (80 + r));
        l->time_to_beat = (int)u16(w + 2 * 95);
        l->speed = (int)u16(w + 2 * 98);
        l->speed_max = (int)u16(w + 2 * 104);
        if (l->time_to_beat < 30 || l->time_to_beat > 900) l->time_to_beat = 150;
        if (l->speed < 60 || l->speed > 400) l->speed = 120;
        if (l->speed_max < l->speed || l->speed_max > 500) l->speed_max = l->speed + 20;
    }
    free(e);
    free(d);
    logf_("%s: %d levels", member, nlevels);
    return nlevels;
}

int level_count(void) { return nlevels; }
const Level *level(int i) { return i >= 0 && i < nlevels ? &levels[i] : 0; }
