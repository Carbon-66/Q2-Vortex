/* Drawing into the 375 x 320 frame: pictures, sprites (scaled, turned, added as light), soft
 * shapes, and the game's own vector font. Colours are 0xRRGGBB; alpha 0-256. */
#include <stdlib.h>
#include <string.h>

#include "vx.h"

static uint32_t frame[VIEW_W * VIEW_H];
uint32_t *fb = frame;

static inline uint32_t addc(uint32_t d, uint32_t s, int a) {
    int r = (int)(d >> 16 & 255) + ((int)(s >> 16 & 255) * a >> 8);
    int g = (int)(d >> 8 & 255) + ((int)(s >> 8 & 255) * a >> 8);
    int b = (int)(d & 255) + ((int)(s & 255) * a >> 8);
    return (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(b > 255 ? 255 : b);
}

static inline uint32_t mulc(uint32_t s, uint32_t t) {
    return ((s >> 16 & 255) * (t >> 16 & 255) / 255) << 16 | ((s >> 8 & 255) * (t >> 8 & 255) / 255) << 8 | (s & 255) * (t & 255) / 255;
}

/* d towards s by a/256, per channel */
static inline uint32_t blend(uint32_t d, uint32_t s, int a) {
    int r = (int)(d >> 16 & 255), g = (int)(d >> 8 & 255), b = (int)(d & 255);
    r += ((int)(s >> 16 & 255) - r) * a >> 8;
    g += ((int)(s >> 8 & 255) - g) * a >> 8;
    b += ((int)(s & 255) - b) * a >> 8;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

void fill(uint32_t c) {
    for (int i = 0; i < VIEW_W * VIEW_H; ++i) frame[i] = c;
}

void blit(const Img *im, int x, int y) {
    if (!im || !im->px) return;
    for (int r = 0; r < im->h; ++r) {
        int yy = y + r;
        if (yy < 0 || yy >= VIEW_H) continue;
        int x0 = x < 0 ? -x : 0, x1 = x + im->w > VIEW_W ? VIEW_W - x : im->w;
        if (x1 > x0) memcpy(frame + yy * VIEW_W + x + x0, im->px + r * im->w + x0, (size_t)(x1 - x0) * 4);
    }
}

void rect(int x, int y, int w, int h, uint32_t c, int alpha) {
    int x0 = clampi(x, 0, VIEW_W), x1 = clampi(x + w, 0, VIEW_W), y0 = clampi(y, 0, VIEW_H), y1 = clampi(y + h, 0, VIEW_H);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) frame[yy * VIEW_W + xx] = alpha >= 256 ? c : blend(frame[yy * VIEW_W + xx], c, alpha);
}

void darken(int alpha) {
    int k = 256 - clampi(alpha, 0, 256);
    for (int i = 0; i < VIEW_W * VIEW_H; ++i) {
        uint32_t c = frame[i];
        frame[i] = ((c & 0xff00ff) * (uint32_t)k >> 8 & 0xff00ff) | ((c & 0xff00) * (uint32_t)k >> 8 & 0xff00);
    }
}

void spr(const Spr *s, int x, int y, int alpha) {
    if (!s->im || !s->im->px) return;
    for (int r = 0; r < s->h; ++r) {
        int yy = y + r;
        if (yy < 0 || yy >= VIEW_H) continue;
        const uint32_t *src = s->im->px + (s->y + r) * s->im->w + s->x;
        for (int c = 0; c < s->w; ++c) {
            int xx = x + c;
            if (xx < 0 || xx >= VIEW_W) continue;
            uint32_t p = src[c];
            int a = (int)(p >> 24) * alpha >> 8;
            if (a) frame[yy * VIEW_W + xx] = blend(frame[yy * VIEW_W + xx], p, a + (a >> 7));
        }
    }
}

/* scaled, nearest-neighbour; scale 256 = 1:1 */
static void spr_scaled_(const Spr *s, int cx, int cy, int scale, int alpha, int add) {
    if (!s->im || !s->im->px || scale <= 0) return;
    int dw = s->w * scale >> 8, dh = s->h * scale >> 8;
    if (dw <= 0 || dh <= 0) return;
    int x0 = cx - dw / 2, y0 = cy - dh / 2;
    unsigned stepx = ((unsigned)s->w << 16) / (unsigned)dw, stepy = ((unsigned)s->h << 16) / (unsigned)dh;
    for (int r = 0; r < dh; ++r) {
        int yy = y0 + r;
        if (yy < 0 || yy >= VIEW_H) continue;
        const uint32_t *src = s->im->px + (s->y + (int)((unsigned)r * stepy >> 16)) * s->im->w + s->x;
        for (int c = 0; c < dw; ++c) {
            int xx = x0 + c;
            if (xx < 0 || xx >= VIEW_W) continue;
            uint32_t p = src[(unsigned)c * stepx >> 16];
            int a = (int)(p >> 24) * alpha >> 8;
            if (!a) continue;
            uint32_t *d = &frame[yy * VIEW_W + xx];
            *d = add ? addc(*d, p, a) : blend(*d, p, a + (a >> 7));
        }
    }
}

void spr_c(const Spr *s, int cx, int cy, int scale, int alpha) { spr_scaled_(s, cx, cy, scale, alpha, 0); }

/* into the box x, y, w, h, mirrored left-right (flip 1) and/or upside down (flip 2) */
void spr_box(const Spr *s, int x, int y, int w, int h, int flip, int alpha) {
    if (!s->im || !s->im->px || w <= 0 || h <= 0) return;
    for (int r = 0; r < h; ++r) {
        int yy = y + r;
        if (yy < 0 || yy >= VIEW_H) continue;
        int sy = r * s->h / h;
        if (flip & 2) sy = s->h - 1 - sy;
        const uint32_t *src = s->im->px + (s->y + sy) * s->im->w + s->x;
        for (int c = 0; c < w; ++c) {
            int xx = x + c;
            if (xx < 0 || xx >= VIEW_W) continue;
            int sx = c * s->w / w;
            if (flip & 1) sx = s->w - 1 - sx;
            uint32_t p = src[sx];
            int a = (int)(p >> 24) * alpha >> 8;
            if (a) frame[yy * VIEW_W + xx] = blend(frame[yy * VIEW_W + xx], p, a + (a >> 7));
        }
    }
}
void spr_add(const Spr *s, int cx, int cy, int scale, int alpha) { spr_scaled_(s, cx, cy, scale, alpha, 1); }

/* turned and scaled about its centre, bilinear; tint multiplies (0xffffff for none) */
void spr_rot(const Spr *s, float cx, float cy, float ang, float scale, int alpha, uint32_t tint) {
    if (!s->im || !s->im->px || scale <= 0.01f) return;
    float c = fcos(ang), sn = fsin(ang);
    float hw = s->w * scale / 2, hh = s->h * scale / 2;
    float ex = fabs_(c) * hw + fabs_(sn) * hh, ey = fabs_(sn) * hw + fabs_(c) * hh;
    int x0 = clampi((int)(cx - ex) - 1, 0, VIEW_W), x1 = clampi((int)(cx + ex) + 2, 0, VIEW_W);
    int y0 = clampi((int)(cy - ey) - 1, 0, VIEW_H), y1 = clampi((int)(cy + ey) + 2, 0, VIEW_H);
    float inv = 1 / scale;
    const uint32_t *px = s->im->px;
    int iw = s->im->w;
    for (int y = y0; y < y1; ++y) {
        float dy = y + 0.5f - cy;
        for (int x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx;
            /* the screen turned back: the view's y runs down, so anticlockwise is (c, -s; s, c) here */
            float u = (dx * c - dy * sn) * inv + s->w / 2.0f - 0.5f, v = (dx * sn + dy * c) * inv + s->h / 2.0f - 0.5f;
            if (u < -0.5f || v < -0.5f || u > s->w - 0.5f || v > s->h - 0.5f) continue;
            int ui = (int)(u + 1) - 1, vi = (int)(v + 1) - 1, au = (int)((u - ui) * 256), av = (int)((v - vi) * 256);
            uint32_t q[4];
            for (int k = 0; k < 4; ++k) {
                int uu = ui + (k & 1), vv = vi + (k >> 1);
                q[k] = uu < 0 || vv < 0 || uu >= s->w || vv >= s->h ? 0 : px[(s->y + vv) * iw + s->x + uu];
            }
            uint32_t out = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                int a = (int)(q[0] >> sh & 255), b = (int)(q[1] >> sh & 255), cc = (int)(q[2] >> sh & 255), d = (int)(q[3] >> sh & 255);
                out |= (uint32_t)(((a * (256 - au) + b * au) * (256 - av) + (cc * (256 - au) + d * au) * av) >> 16) << sh;
            }
            int a = (int)(out >> 24) * alpha >> 8;
            if (!a) continue;
            if (tint != 0xffffff) out = mulc(out, tint);
            frame[y * VIEW_W + x] = blend(frame[y * VIEW_W + x], out, a + (a >> 7));
        }
    }
}

void disc(float cx, float cy, float r, uint32_t col, int alpha) {
    int x0 = clampi((int)(cx - r) - 1, 0, VIEW_W), x1 = clampi((int)(cx + r) + 2, 0, VIEW_W);
    int y0 = clampi((int)(cy - r) - 1, 0, VIEW_H), y1 = clampi((int)(cy + r) + 2, 0, VIEW_H);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = fsqrt(dx * dx + dy * dy);
            float cov = clampf(r + 0.5f - d, 0, 1);
            if (cov > 0) frame[y * VIEW_W + x] = blend(frame[y * VIEW_W + x], col, (int)(cov * alpha));
        }
}

void glow(float cx, float cy, float r, uint32_t col, int alpha) {
    int x0 = clampi((int)(cx - r), 0, VIEW_W), x1 = clampi((int)(cx + r) + 1, 0, VIEW_W);
    int y0 = clampi((int)(cy - r), 0, VIEW_H), y1 = clampi((int)(cy + r) + 1, 0, VIEW_H);
    float inv = 1 / (r * r);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, t = 1 - (dx * dx + dy * dy) * inv;
            if (t > 0) frame[y * VIEW_W + x] = addc(frame[y * VIEW_W + x], col, (int)(t * t * alpha));
        }
}

/* an anti-aliased stroke of width w with round ends */
void line(float x0, float y0, float x1, float y1, float w, uint32_t col, int alpha) {
    float hw = w / 2;
    int bx0 = clampi((int)((x0 < x1 ? x0 : x1) - hw) - 1, 0, VIEW_W), bx1 = clampi((int)((x0 > x1 ? x0 : x1) + hw) + 2, 0, VIEW_W);
    int by0 = clampi((int)((y0 < y1 ? y0 : y1) - hw) - 1, 0, VIEW_H), by1 = clampi((int)((y0 > y1 ? y0 : y1) + hw) + 2, 0, VIEW_H);
    float dx = x1 - x0, dy = y1 - y0, ll = dx * dx + dy * dy;
    float inv = ll > 1e-6f ? 1 / ll : 0;
    for (int y = by0; y < by1; ++y)
        for (int x = bx0; x < bx1; ++x) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0;
            float t = clampf((px * dx + py * dy) * inv, 0, 1);
            float ex = px - t * dx, ey = py - t * dy, d = fsqrt(ex * ex + ey * ey);
            float cov = clampf(hw + 0.5f - d, 0, 1);
            if (cov > 0) frame[y * VIEW_W + x] = blend(frame[y * VIEW_W + x], col, (int)(cov * alpha));
        }
}

/* ---------------------------------------------------------------------------------------------
 * The vector font: each glyph strokes on a 4 x 6 grid ("xy" points, polylines split by '|'),
 * squared off with cut corners. Lower case is drawn as capitals. */

static const struct {
    char c;
    const char *s;
} glyphs[] = {
    { 'A', "06 01 10 30 41 46|03 43" },
    { 'B', "06 00 30 41 42 33 03|33 44 45 36 06" },
    { 'C', "41 30 10 01 05 16 36 45" },
    { 'D', "00 30 41 45 36 06 00" },
    { 'E', "40 00 06 46|03 33" },
    { 'F', "40 00 06|03 33" },
    { 'G', "41 30 10 01 05 16 36 45 43 23" },
    { 'H', "00 06|40 46|03 43" },
    { 'I', "10 30|20 26|16 36" },
    { 'J', "20 40 45 36 16 05" },
    { 'K', "00 06|03 13 40|13 46" },
    { 'L', "00 06 46" },
    { 'M', "06 00 23 40 46" },
    { 'N', "06 00 46 40" },
    { 'O', "10 30 41 45 36 16 05 01 10" },
    { 'P', "06 00 30 41 42 33 03" },
    { 'Q', "10 30 41 45 36 16 05 01 10|34 46" },
    { 'R', "06 00 30 41 42 33 03|23 46" },
    { 'S', "41 30 10 01 02 13 33 44 45 36 16 05" },
    { 'T', "00 40|20 26" },
    { 'U', "00 05 16 36 45 40" },
    { 'V', "00 03 26 43 40" },
    { 'W', "00 06 23 46 40" },
    { 'X', "00 01 45 46|40 41 05 06" },
    { 'Y', "00 01 23 41 40|23 26" },
    { 'Z', "00 40 41 05 06 46" },
    { '0', "10 30 41 45 36 16 05 01 10|05 41" },
    { '1', "11 20 26|16 36" },
    { '2', "01 10 30 41 42 06 46" },
    { '3', "01 10 30 41 42 33 13|33 44 45 36 16 05" },
    { '4', "36 30 03 04 44" },
    { '5', "40 00 03 33 44 45 36 06" },
    { '6', "41 30 10 01 05 16 36 45 44 33 03" },
    { '7', "00 40 42 26" },
    { '8', "13 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33" },
    { '9', "43 13 02 01 10 30 41 45 36 16 05" },
    { '.', "16 16" },
    { ',', "15 06" },
    { ':', "12 12|15 15" },
    { ';', "12 12|15 06" },
    { '!', "10 14|16 16" },
    { '?', "01 10 30 41 42 23 24|26 26" },
    { '-', "13 33" },
    { '+', "13 33|22 24" },
    { '=', "12 32|14 34" },
    { '/', "06 40" },
    { '%', "06 40|00 01|45 46" },
    { '(', "30 21 25 36" },
    { ')', "10 21 25 16" },
    { '\'', "10 12" },
    { '"', "10 12|30 32" },
    { '&', "46 02 01 10 20 31 32 05 16 26 44" },
    { '_', "06 46" },
    { '<', "30 03 36" },
    { '>', "10 43 16" },
    { '#', "10 16|30 36|02 42|04 44" },
    { '*', "11 35|31 15|03 43" },
    { '@', "32 22 23 33 31 41 45 36 16 05 01 10 30" },
};

static const char *glyph_of(int c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    for (unsigned i = 0; i < sizeof glyphs / sizeof glyphs[0]; ++i)
        if (glyphs[i].c == c) return glyphs[i].s;
    return 0;
}

static float advance(int c) {
    if (c == ' ') return 3.4f;
    if (c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '\'') return 2.6f;
    if (c == 'I' || c == 'i' || c == '1' || c == '(' || c == ')') return 4.4f;
    return 5.6f;
}

/* the next character: UTF-8 is read through, and what the font lacks is dropped or simplified */
static int next_char(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    int c = *s++;
    if (c >= 0x80) {
        int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        unsigned v = c & (0x3f >> extra);
        for (int i = 0; i < extra && (*s & 0xc0) == 0x80; ++i) v = v << 6 | (*s++ & 0x3f);
        c = v == 0xa9 ? 'C' : v >= 0xc0 && v <= 0xc5 ? 'A' : v >= 0xe0 && v <= 0xe5 ? 'A' : v >= 0xc8 && v <= 0xcb ? 'E'
          : v >= 0xe8 && v <= 0xeb ? 'E' : v >= 0xcc && v <= 0xcf ? 'I' : v >= 0xec && v <= 0xef ? 'I'
          : v >= 0xd2 && v <= 0xd6 ? 'O' : v >= 0xf2 && v <= 0xf6 ? 'O' : v == 0xd8 || v == 0xf8 ? 'O'
          : v >= 0xd9 && v <= 0xdc ? 'U' : v >= 0xf9 && v <= 0xfc ? 'U' : v == 0xd1 || v == 0xf1 ? 'N'
          : v == 0xc7 || v == 0xe7 ? 'C' : v == 0xdf ? 'S' : v == 0x2019 || v == 0x2018 ? '\'' : '?';
    }
    *p = (const char *)s;
    return c;
}

int vtext_w(const char *s, float h) {
    float u = h / 6, w = 0;
    while (*s) w += advance(next_char(&s)) * u;
    return (int)(w - 1.6f * u + 0.5f);
}

static void strokes(const char *s, float x, float y, float h, float lw, uint32_t c, int alpha) {
    float u = h / 6;
    while (*s) {
        int ch = next_char(&s);
        const char *g = glyph_of(ch);
        for (; g && *g;) {
            float px = 0, py = 0;
            int first = 1;
            while (*g && *g != '|') {
                if (*g == ' ') {
                    ++g;
                    continue;
                }
                float qx = x + (g[0] - '0') * u, qy = y + (g[1] - '0') * u;
                if (first) {
                    if (g[2] == '|' || !g[2]) disc(qx, qy, lw * 0.62f, c, alpha); /* a lone point: a dot */
                    else if (g[2] == ' ' && g[3] == g[0] && g[4] == g[1]) disc(qx, qy, lw * 0.62f, c, alpha);
                } else {
                    line(px, py, qx, qy, lw, c, alpha);
                }
                px = qx, py = qy, first = 0;
                g += 2;
            }
            if (*g == '|') ++g;
        }
        x += advance(ch) * u;
    }
}

int vtext(const char *s, float x, float y, float h, uint32_t c, int alpha, int align) {
    int w = vtext_w(s, h);
    if (align == 0) x -= w / 2.0f;
    else if (align > 0) x -= w;
    strokes(s, x, y, h, h < 10 ? 1.3f : h / 7.5f, c, alpha);
    return w;
}

void vtext_glow(const char *s, float x, float y, float h, uint32_t c, int alpha, int align) {
    int w = vtext_w(s, h);
    if (align == 0) x -= w / 2.0f;
    else if (align > 0) x -= w;
    float lw = h < 10 ? 1.3f : h / 7.5f;
    strokes(s, x + 1, y + 1.5f, h, lw + 1.2f, 0x000000, alpha * 3 / 4);
    strokes(s, x, y, h, lw, c, alpha);
}

/* the iPod game's own title style: white, outlined in green */
void vtext_outline(const char *s, float x, float y, float h, uint32_t fill_, uint32_t edge, int alpha, int align) {
    int w = vtext_w(s, h);
    if (align == 0) x -= w / 2.0f;
    else if (align > 0) x -= w;
    float lw = h < 10 ? 1.3f : h / 7.5f;
    strokes(s, x + 1, y + 2, h, lw + 3.4f, 0x000000, alpha / 2);
    strokes(s, x, y, h, lw + 2.6f, edge, alpha);
    strokes(s, x, y, h, lw, fill_, alpha);
}

/* centred lines broken at spaces to fit w; returns the height used */
int vtext_wrap(const char *s, float x, float y, float w, float h, uint32_t c, int alpha) {
    char line_[160];
    float y0 = y;
    while (*s) {
        while (*s == ' ') ++s;
        int n = 0, cut = -1;
        for (;; ++n) {
            if (!s[n] || n >= (int)sizeof line_ - 1) break;
            memcpy(line_, s, (size_t)n + 1), line_[n + 1] = 0;
            if (s[n] == ' ') cut = n;
            if (vtext_w(line_, h) > w && cut > 0) {
                n = cut;
                break;
            }
        }
        memcpy(line_, s, (size_t)n), line_[n] = 0;
        vtext_glow(line_, x, y, h, c, alpha, 0);
        s += n;
        y += h * 1.7f;
    }
    return (int)(y - y0);
}

/* ---------------------------------------------------------------------------------------------
 * The view about the tube's axis */

uint16_t *polar_r, *polar_a;

void polar_init(void) {
    polar_r = malloc(VIEW_W * VIEW_H * 2);
    polar_a = malloc(VIEW_W * VIEW_H * 2);
    for (int y = 0; y < VIEW_H; ++y)
        for (int x = 0; x < VIEW_W; ++x) {
            float dx = x + 0.5f - CX, dy = CY - (y + 0.5f);
            float r = fsqrt(dx * dx + dy * dy), a = wrap_tau(fatan2(dy, dx));
            polar_r[y * VIEW_W + x] = (uint16_t)clampf(r * 16, 0, 65535);
            polar_a[y * VIEW_W + x] = (uint16_t)((unsigned)(a * (65536 / TAU)) & 0xffff);
        }
}

/* ---------------------------------------------------------------------------------------------
 * Maths */

float wrap_pi(float a) {
    while (a > PI) a -= TAU;
    while (a < -PI) a += TAU;
    return a;
}

float wrap_tau(float a) {
    while (a >= TAU) a -= TAU;
    while (a < 0) a += TAU;
    return a;
}

/* sine by a 7th-order polynomial on -PI/2..PI/2: well within a pixel at the radii used */
float fsin(float a) {
    a = wrap_pi(a);
    if (a > PI / 2) a = PI - a;
    else if (a < -PI / 2) a = -PI - a;
    float a2 = a * a;
    return a * (1 - a2 / 6 * (1 - a2 / 20 * (1 - a2 / 42)));
}

float fcos(float a) { return fsin(a + PI / 2); }

float fatan2(float y, float x) {
    float ax = fabs_(x), ay = fabs_(y);
    if (ax < 1e-9f && ay < 1e-9f) return 0;
    float t = ax > ay ? ay / ax : ax / ay, t2 = t * t;
    /* a minimax fit on 0..1, about 1e-5 rad */
    float r = t * (0.99997726f + t2 * (-0.33262347f + t2 * (0.19354346f + t2 * (-0.11643287f + t2 * (0.05265332f - t2 * 0.01172120f)))));
    if (ay > ax) r = PI / 2 - r;
    if (x < 0) r = PI - r;
    return y < 0 ? -r : r;
}

static unsigned seed = 12345;
void rnd_seed(unsigned s) { seed = s ? s : 1; }
unsigned rnd(void) {
    seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
    return seed;
}
float rndf(void) { return (rnd() & 0xffffff) / 16777216.0f; }
