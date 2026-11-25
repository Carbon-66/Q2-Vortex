/* Drawing the tube: the level's picture, the far door, the bricks on the wall, the ball, the bat,
 * bonuses and effects, far to near, then the HUD.
 *
 * The camera sits on the tube's axis, so a brick's faces are annular sectors on the view: its
 * inner face (towards the axis) between its far and near ends, and its front face (towards the
 * camera) between the inner face and the wall. Its sides lie in planes through the axis and are
 * seen edge on. Each face is filled pixel by pixel from the view's polar table. */
#include <stdio.h>
#include <string.h>

#include "game.h"

Art art;

/* the bricks' colours, by the level file's colour number */
uint32_t brick_colour(int c) {
    static const uint32_t pal[8] = { 0xff8a2a, 0xffd23a, 0x63e04a, 0x3aa8ff, 0xb46cff, 0xff4f9a, 0x3ae0d8, 0xe8e8f0 };
    return pal[c & 7];
}

static inline uint32_t scale_c(uint32_t c, int k) { /* k 0-256, may go to 320 for a lift */
    int r = (int)(c >> 16 & 255) * k >> 8, gg = (int)(c >> 8 & 255) * k >> 8, b = (int)(c & 255) * k >> 8;
    return (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(gg > 255 ? 255 : gg) << 8 | (uint32_t)(b > 255 ? 255 : b);
}

static inline uint32_t mul_c(uint32_t s, uint32_t t) {
    return ((s >> 16 & 255) * (t >> 16 & 255) >> 8) << 16 | ((s >> 8 & 255) * (t >> 8 & 255) >> 8) << 8 | (s & 255) * (t & 255) >> 8;
}

static inline uint32_t lift_c(uint32_t c, int a) { /* towards white */
    int r = (int)(c >> 16 & 255), gg = (int)(c >> 8 & 255), b = (int)(c & 255);
    r += (255 - r) * a >> 8, gg += (255 - gg) * a >> 8, b += (255 - b) * a >> 8;
    return (uint32_t)r << 16 | (uint32_t)gg << 8 | (uint32_t)b;
}

/* the view's box around an annular sector */
static void sector_box(float a0, float span, float r1, int *x0, int *y0, int *x1, int *y1, float r0) {
    float xs[8], ys[8];
    int n = 0;
    float ends[2] = { a0, a0 + span };
    for (int i = 0; i < 2; ++i) {
        float c = fcos(ends[i]), s = fsin(ends[i]);
        xs[n] = CX + r1 * c, ys[n++] = CY - r1 * s;
        xs[n] = CX + r0 * c, ys[n++] = CY - r0 * s;
    }
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for (int i = 0; i < n; ++i) {
        if (xs[i] < bx0) bx0 = xs[i];
        if (xs[i] > bx1) bx1 = xs[i];
        if (ys[i] < by0) by0 = ys[i];
        if (ys[i] > by1) by1 = ys[i];
    }
    /* the axis directions the sector crosses reach out to r1 */
    for (int q = 0; q < 4; ++q) {
        float qa = q * PI / 2;
        if (wrap_tau(qa - a0) <= span) {
            float x = CX + r1 * fcos(qa), y = CY - r1 * fsin(qa);
            if (x < bx0) bx0 = x;
            if (x > bx1) bx1 = x;
            if (y < by0) by0 = y;
            if (y > by1) by1 = y;
        }
    }
    *x0 = clampi((int)bx0 - 1, 0, VIEW_W), *x1 = clampi((int)bx1 + 2, 0, VIEW_W);
    *y0 = clampi((int)by0 - 1, 0, VIEW_H), *y1 = clampi((int)by1 + 2, 0, VIEW_H);
}

/* a brick's front face: the frame t laid across it, top row at the wall, times tint */
static void face_front(float a0, float span, float r0, float r1, const Spr *t, uint32_t tint, int lift) {
    if (r1 - r0 < 0.3f || !t->im || !t->im->px) return;
    int x0, y0, x1, y1;
    sector_box(a0, span, r1, &x0, &y0, &x1, &y1, r0);
    unsigned A0 = (unsigned)(wrap_tau(a0) * (65536 / TAU)) & 0xffff, SPAN = (unsigned)(span * (65536 / TAU));
    unsigned R0 = (unsigned)(r0 * 16), R1 = (unsigned)(r1 * 16);
    if (R1 <= R0 || !SPAN) return;
    unsigned us = ((unsigned)t->w << 16) / SPAN, vs = ((unsigned)t->h << 16) / (R1 - R0);
    const uint32_t *tp = t->im->px;
    int tw = t->im->w;
    for (int y = y0; y < y1; ++y) {
        const uint16_t *pr = polar_r + y * VIEW_W, *pa = polar_a + y * VIEW_W;
        uint32_t *out = fb + y * VIEW_W;
        for (int x = x0; x < x1; ++x) {
            unsigned da = (pa[x] - A0) & 0xffff, r = pr[x];
            if (da >= SPAN || r < R0 || r >= R1) continue;
            unsigned u = da * us >> 16, v = (r - R0) * vs >> 16;
            if (u >= (unsigned)t->w) u = t->w - 1;
            if (v >= (unsigned)t->h) v = t->h - 1;
            /* the frame's bottom at the wall: the texture runs from the wall in */
            uint32_t c = mul_c(tp[(t->y + v) * tw + t->x + u] | 0x010101, tint);
            if (lift) c = lift_c(c, lift);
            /* the edges darker, so neighbours part */
            if (da < 90 || SPAN - da < 90 || r - R0 < 8) c = scale_c(c, 150);
            out[x] = c;
        }
    }
}

/* a brick's inner face: its colour, lit nearest the camera and darker to the far end */
static void face_inner(float a0, float span, float r0, float r1, uint32_t col, int lift) {
    if (r1 - r0 < 0.3f) return;
    int x0, y0, x1, y1;
    sector_box(a0, span, r1, &x0, &y0, &x1, &y1, r0);
    unsigned A0 = (unsigned)(wrap_tau(a0) * (65536 / TAU)) & 0xffff, SPAN = (unsigned)(span * (65536 / TAU));
    unsigned R0 = (unsigned)(r0 * 16), R1 = (unsigned)(r1 * 16);
    if (R1 <= R0 || !SPAN) return;
    unsigned vs = (256u << 16) / (R1 - R0);
    for (int y = y0; y < y1; ++y) {
        const uint16_t *pr = polar_r + y * VIEW_W, *pa = polar_a + y * VIEW_W;
        uint32_t *out = fb + y * VIEW_W;
        for (int x = x0; x < x1; ++x) {
            unsigned da = (pa[x] - A0) & 0xffff, r = pr[x];
            if (da >= SPAN || r < R0 || r >= R1) continue;
            unsigned v = (r - R0) * vs >> 16; /* 0 at the far end, 255 at the near edge */
            int k = 110 + (int)(v * 90 >> 8);
            /* across the face, a soft highlight down the middle as on a rounded top */
            int across = (int)(da * 512 / SPAN) - 256;
            k += 40 - (across * across >> 11);
            uint32_t c = scale_c(col, k);
            if (R1 - r < 10) c = lift_c(c, 150); /* the near edge catches the light */
            if (lift) c = lift_c(c, lift);
            out[x] = c;
        }
    }
}

/* where a brick is: its angle range and depth range */
static void brick_place(int k, int j, float *a0, float *span, float *z0, float *z1) {
    const Brick *b = &g.brick[k][j];
    *a0 = g.ring_a[k] + j * SEG_W + 0.012f;
    *span = SEG_W - 0.024f;
    *z0 = RING_Z(k) + b->dz - BRICK_D / 2;
    *z1 = *z0 + BRICK_D;
}

static float anim_t;

static void draw_brick(int k, int j) {
    const Brick *b = &g.brick[k][j];
    float a0, span, z0, z1;
    brick_place(k, j, &a0, &span, &z0, &z1);
    uint32_t col = brick_colour(b->colour);
    const Spr *t = &art.brick_plain;
    int lift = b->flash > 0 ? (int)(b->flash * 4 * 200) : 0;
    if (lift > 220) lift = 220;
    switch (b->type) {
    case B_TWO:
    case B_THREE: {
        int dmg = b->max_hits - b->hits; /* 0 none yet */
        static const int crack[3] = { 3, 4, 0 };
        t = &art.brick_crack[crack[dmg > 2 ? 2 : dmg]];
        col = scale_c(col, b->type == B_THREE ? 200 : 230);
        break;
    }
    case B_BONUS: t = &art.brick_bonus[(int)(anim_t * 8 + j) % 6], col = lift_c(col, 170); break;
    case B_BOMB:
        t = &art.brick_bomb[1 + (int)(anim_t * 6 + j) % 4], col = 0xf0e8e0;
        if (b->fuse > 0) lift = 200;
        break;
    case B_SOLID: col = 0x8c98a8; break;
    }
    if (!t->im || !t->im->px) t = &art.brick_plain;
    /* far bricks fall into shadow */
    int fog = 256 - (int)((z0 - 0.5f) * 70);
    col = scale_c(col, clampi(fog, 130, 256));
    face_inner(a0, span, px_r(R_IN, z1), px_r(R_IN, z0), scale_c(col, 235), lift);
    face_front(a0, span, px_r(R_IN, z0), px_r(1.0f, z0), t, col, lift);
}

/* ---------------------------------------------------------------------------------------------
 * The ball, bonuses, shots */

static void at(float a, float rad, float z, float *x, float *y) {
    float r = px_r(rad, z);
    *x = CX + r * fcos(a), *y = CY - r * fsin(a);
}

static void draw_ball(const Ball *b) {
    float x, y, sx, sy;
    at(b->a, R_BALL, b->z, &x, &y);
    at(b->a, 1.0f, b->z, &sx, &sy);
    float r = px_r(BALL_R, b->z);
    disc(sx, sy, r * 0.9f, 0x000000, 70); /* where it is on the wall: a shadow, for depth */
    if (g.power_t > 0 && art.fireball[0].im) {
        int f = (int)(anim_t * 18) % 9;
        spr_add(&art.fireball[f], (int)x, (int)y, (int)(r * 2.6f / 38 * 256 * 1.6f), 230);
    }
    glow(x, y, r * 2.4f, g.power_t > 0 ? 0xff7a20 : 0x7090ff, 90);
    if (art.ball.im) spr_c(&art.ball, (int)x, (int)y, (int)(r * 2.2f / 30 * 256), 256);
    else disc(x, y, r, 0xffffff, 256);
}

static const int icon_of[NBONUS] = { 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2 };

static void draw_cap(const Cap *c) {
    float x, y;
    at(c->a, R_BALL, c->z, &x, &y);
    float r = px_r(0.13f, c->z);
    glow(x, y, r * 2.0f, 0x40ff60, 120);
    const Spr *s = &art.icon[icon_of[c->kind]];
    if (s->im) spr_c(s, (int)x, (int)y, (int)(r * 2 / 56 * 256), 256);
    else disc(x, y, r, 0x40ff60, 256);
}

static void draw_shot(const Shot *s) {
    float x, y, x2, y2;
    at(s->a, R_BALL, s->z, &x, &y);
    at(s->a, R_BALL, s->z + 0.12f, &x2, &y2);
    line(x, y, x2, y2, 2.2f, 0xffe070, 230);
    glow(x, y, 6, 0xff9030, 120);
}

/* ---------------------------------------------------------------------------------------------
 * The bat */

static void draw_bat_at(float a) {
    const Spr *s = &art.bat[g.bat_size][g.sticky_t > 0 ? 1 : 0];
    float r = px_r(R_BAT, 0), x = CX + r * fcos(a), y = CY - r * fsin(a);
    if (s->im) spr_rot(s, x, y, a + PI / 2, 1.33f, 256, 0xffffff);
    else {
        float h = bat_half();
        for (int i = 0; i < 12; ++i) {
            float t0 = a - h + 2 * h * i / 12, t1 = a - h + 2 * h * (i + 1) / 12;
            line(CX + r * fcos(t0), CY - r * fsin(t0), CX + r * fcos(t1), CY - r * fsin(t1), 6, 0xd0d8e0, 256);
        }
    }
    if (g.guns_t > 0 || g.laser_t > 0) {
        float h = bat_half() * 0.82f;
        for (int side = -1; side <= 1; side += 2) {
            float ga = a + side * h, gx = CX + (r - 4) * fcos(ga), gy = CY - (r - 4) * fsin(ga);
            /* the mounts' sheet: a row at rest, a row firing, a row for the laser at rest */
            int f = g.fire_cool > 0.15f ? 4 + (int)(anim_t * 20) % 4 : (g.laser_t > 0 ? 8 : 0) + (int)(anim_t * 8) % 4;
            if (art.gun[f].im) spr_rot(&art.gun[f], gx, gy, ga + PI / 2, 0.95f, 256, 0xffffff);
            else disc(gx, gy, 4, 0xff4040, 256);
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * Effects */

static void draw_fx(const Fx *f) {
    float x, y, k = f->t / f->dur;
    at(f->a, R_BALL, f->z, &x, &y);
    float s = px_r(1.0f, f->z) / 120; /* sizes made for the near rim */
    switch (f->kind) {
    case FX_DEBRIS: {
        int fr = 15 - (int)(k * 16);
        const Spr *sp = &art.debris[clampi(fr, 0, 15)];
        if (sp->im) {
            /* the debris sheet is white; tint by drawing it as light in the brick's colour */
            spr_rot(sp, x, y, f->a + PI / 2, s * 1.1f * (0.8f + k * 0.6f), (int)(256 * (1 - k)), f->colour);
        }
        break;
    }
    case FX_BOOM: {
        static const int order[8] = { 4, 5, 6, 7, 0, 1, 2, 3 };
        const Spr *sp = &art.boom[order[clampi((int)(k * 8), 0, 7)]];
        if (sp->im) spr_c(sp, (int)x, (int)y, (int)(s * 1.5f * 256), 256);
        glow(x, y, 40 * s * (1 - k) + 4, 0xff8030, (int)(160 * (1 - k)));
        break;
    }
    case FX_SPARK: glow(x, y, 14 * s * (1 - k) + 2, f->colour, (int)(220 * (1 - k))); break;
    case FX_TEXT: vtext_glow(f->text, x, y - 10 - k * 18, 9, f->colour, (int)(256 * (1 - k * k)), 0); break;
    }
}

/* ---------------------------------------------------------------------------------------------
 * The scene */

typedef struct {
    float z;
    short kind, i, j;
} Item;

void tube_draw(int show_balls) {
    anim_t += 1 / 60.0f;
    if (art.bg.px) blit(&art.bg, 0, 0);
    else fill(0x050810);

    /* the far end: the door, its 16 frames round in a circle */
    if (art.door.px) {
        int f = clampi(g.door_frame, 0, 15);
        Spr d = spr_cell(&art.door, 4, 4, f);
        float r = px_r(1.0f, ZFAR) * 1.02f;
        int sz = (int)(2 * r);
        /* masked to a circle: drawn pixel by pixel */
        float sc = d.w / (2 * r);
        for (int y = (int)(CY - r); y <= (int)(CY + r); ++y) {
            if (y < 0 || y >= VIEW_H) continue;
            for (int x = (int)(CX - r); x <= (int)(CX + r); ++x) {
                float dx = x + 0.5f - CX, dy = y + 0.5f - CY, dd = dx * dx + dy * dy;
                if (dd > r * r) continue;
                int u = clampi((int)((dx + r) * sc), 0, d.w - 1), v = clampi((int)((dy + r) * sc), 0, d.h - 1);
                uint32_t c = art.door.px[(d.y + v) * art.door.w + d.x + u];
                float edge = (r - fsqrt(dd));
                int a = edge > 2 ? 256 : (int)(edge * 128);
                fb[y * VIEW_W + x] = a >= 256 ? c : scale_c(fb[y * VIEW_W + x], 256 - a) + scale_c(c, a);
            }
        }
        (void)sz;
    }

    /* everything with a depth, far to near */
    static Item items[RINGS * SEGS + MAX_BALLS + MAX_CAPS + MAX_SHOTS];
    int n = 0;
    for (int k = 0; k < RINGS; ++k)
        for (int j = 0; j < SEGS; ++j)
            if (g.brick[k][j].type) items[n++] = (Item){ RING_Z(k) + g.brick[k][j].dz - BRICK_D / 2, 0, (short)k, (short)j };
    if (show_balls)
        for (int i = 0; i < MAX_BALLS; ++i)
            if (g.ball[i].alive) items[n++] = (Item){ g.ball[i].z, 1, (short)i, 0 };
    for (int i = 0; i < MAX_CAPS; ++i)
        if (g.cap[i].alive) items[n++] = (Item){ g.cap[i].z, 2, (short)i, 0 };
    for (int i = 0; i < MAX_SHOTS; ++i)
        if (g.shot[i].alive) items[n++] = (Item){ g.shot[i].z, 3, (short)i, 0 };
    for (int i = 1; i < n; ++i) { /* by depth, far first */
        Item t = items[i];
        int j = i - 1;
        while (j >= 0 && items[j].z < t.z) items[j + 1] = items[j], --j;
        items[j + 1] = t;
    }
    for (int i = 0; i < n; ++i) {
        const Item *it = &items[i];
        if (it->kind == 0) draw_brick(it->i, it->j);
        else if (it->kind == 1) draw_ball(&g.ball[it->i]);
        else if (it->kind == 2) draw_cap(&g.cap[it->i]);
        else draw_shot(&g.shot[it->i]);
    }

    /* the laser's beam: from the bat down the tube to what it hit */
    if (g.laser_beam > 0) {
        float x0, y0, x1, y1;
        at(g.laser_a, R_BALL, 0, &x0, &y0);
        at(g.laser_a, R_BALL, g.laser_len, &x1, &y1);
        int a = (int)(g.laser_beam / 0.18f * 256);
        line(x0, y0, x1, y1, 5, 0xff3030, a / 2);
        line(x0, y0, x1, y1, 2, 0xffe0e0, a);
        glow(x1, y1, 12, 0xff4040, a);
    }

    for (int i = 0; i < MAX_FX; ++i)
        if (g.fx[i].dur > 0 && g.fx[i].kind != FX_TEXT) draw_fx(&g.fx[i]);

    /* the rim the bat runs on */
    if (art.ring.im) spr_add(&art.ring, CX, CY, (int)(px_r(1.0f, 0) * 2.35f / art.ring.w * 256), 40);
    draw_bat_at(g.bat_a);
    if (g.multibat_t > 0) draw_bat_at(g.bat_a + PI);

    for (int i = 0; i < MAX_FX; ++i)
        if (g.fx[i].dur > 0 && g.fx[i].kind == FX_TEXT) draw_fx(&g.fx[i]);

    /* losing the ball: the rim burns red */
    if (g.lost_t > 0) {
        int a = (int)(g.lost_t / 1.2f * 120);
        for (int i = 0; i < 48; ++i) {
            float t0 = i * TAU / 48, t1 = (i + 1) * TAU / 48, r = px_r(1.0f, 0) + 4;
            line(CX + r * fcos(t0), CY - r * fsin(t0), CX + r * fcos(t1), CY - r * fsin(t1), 6, 0xff3010, a);
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * The HUD, as the iPod game's: the level's time bottom left and the score bottom right, each on
 * its green-edged bar (the bar's art mirrored for the left one). The glass's corners are rounded
 * (80 px), so the bars keep in from them. */

#define BAR_Y 288
#define BAR_H 30
#define HUD_TEXT 0xd8ffcc
#define SLANT 43 /* the bar art's slanted end, in its columns; the rest is one column over and over */

/* a bar from its slanted inner end at x out past the view's edge: the slant, then the art's straight
 * run stretched to the edge, so its lines run on into the glass's rounded corner and no end shows.
 * right: the bar runs off the right edge (else the left); upside: its art turned over */
void hud_bar(int x, int y, int h, int right, int upside) {
    const Img *im = art.bar.im;
    if (!im || !im->px || im->w <= SLANT) return;
    int sw = SLANT * h / im->h;
    Spr slant = { im, 0, 0, SLANT, (short)im->h }, run = { im, SLANT, 0, (short)(im->w - SLANT), (short)im->h };
    int flip = upside ? 2 : 0;
    if (right) {
        spr_box(&slant, x, y, sw, h, flip, 256);
        spr_box(&run, x + sw, y, VIEW_W - x - sw, h, flip, 256);
    } else {
        spr_box(&slant, x - sw, y, sw, h, flip | 1, 256);
        spr_box(&run, 0, y, x - sw, h, flip | 1, 256);
    }
}

void hud_draw(void) {
    char b[48];
    /* time: the clock, then m:ss */
    hud_bar(150, BAR_Y, BAR_H, 0, 1);
    if (art.clock.im) spr_c(&art.clock, 56, BAR_Y + 15, 180, 256);
    int t = (int)g.clock;
    snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    vtext_glow(b, 72, BAR_Y + 9, 13, g.clock > g.time_to_beat ? 0xffb080 : HUD_TEXT, 256, -1);
    /* score: six digits */
    hud_bar(VIEW_W - 150, BAR_Y, BAR_H, 1, 1);
    snprintf(b, sizeof b, "%06d", g.score % 1000000);
    vtext_glow(b, VIEW_W - 50, BAR_Y + 9, 13, HUD_TEXT, 256, 1);
    /* the balls in hand, over the score */
    for (int i = 0; i < g.lives - 1 && i < 8; ++i) {
        int x = VIEW_W - 66 - i * 13, y = BAR_Y - 8;
        if (art.ball.im) spr_c(&art.ball, x, y, 90, 256);
        else disc(x, y, 4, 0xffffff, 256);
    }
    /* what the bat carries, and for how long */
    struct {
        float t, full;
        int kind;
    } on[] = { { g.sticky_t, 20, BN_STICKY }, { g.guns_t, 20, BN_GUNS }, { g.laser_t, 20, BN_LASER },
               { g.power_t, 12, BN_POWER }, { g.multibat_t, 20, BN_MULTIBAT } };
    int x = 64;
    for (unsigned i = 0; i < sizeof on / sizeof on[0]; ++i) {
        if (on[i].t <= 0) continue;
        const Spr *s = &art.icon[icon_of[on[i].kind]];
        if (s->im) spr_c(s, x, BAR_Y - 13, 72, on[i].t < 3 && (int)(on[i].t * 6) % 2 ? 120 : 256);
        rect(x - 8, BAR_Y - 3, (int)(16 * on[i].t / on[i].full), 2, 0x60ff60, 220);
        x += 22;
    }
}
