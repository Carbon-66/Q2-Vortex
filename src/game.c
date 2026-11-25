/* Vortex: the rules, the menus and the loop.
 *
 * Breakout on the inside of a tube. The bricks sit on the tube's wall in five rings of sixteen,
 * the rings turning; the ball runs round the wall and down the tube, off the far end and back to
 * the bat, which runs round the near rim.
 *
 *   the wheel        moves the bat round (a turn of the wheel is a turn of the tube)
 *   touch            the bat follows your finger (Control: finger position)
 *   Next / Prev      move the bat while held
 *   Centre, a tap    launches the ball; fires guns or laser
 *   Play/Pause       pauses (Return too)
 *
 * The ball's world: an angle round the tube and a depth z along it (see game.h). The physics
 * steps at 120 Hz. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "game.h"

#define STEP (1.0f / 120)
#define SAVE_FILE "vortex.sav"
#define SOUNDS_FILE "sounds.txt"
#define GREEN 0x8cff6a
#define WHITE 0xf4f6ff
#define START_LIVES 3
#define BAT_STEER 0.52f /* rad off the tube's line from the bat's very end */
#define MIN_ALONG 0.85f /* the share of the ball's speed always along the tube */

Game g;
static Zip *zip;

/* ---------------------------------------------------------------------------------------------
 * Sounds. The bank's 47 sounds carry no names: these are the ones picked for each event, and
 * sounds.txt in the card folder can change them ("bat=5", one a line; Options -> sound test plays
 * them by number). */

enum { S_BAT, S_BRICK, S_CRACK, S_SOLID, S_BOMB, S_DROP, S_BONUS, S_LOST, S_OVER, S_CLEAR, S_START, S_LASER,
       S_GUN, S_WALL, S_LAUNCH, S_LIFE, S_SHRINK, S_STRETCH, S_MOVE, S_PICK, S_BACK, S_DOOR, NCUES };
static const char *cue_name[NCUES] = { "bat", "brick", "crack", "solid", "bomb", "drop", "bonus", "lost",
                                       "over", "clear", "start", "laser", "gun", "wall", "launch", "life",
                                       "shrink", "stretch", "move", "pick", "back", "door" };
static int cue[NCUES] = { 5, 9, 38, 6, 13, 23, 18, 11, 14, 15, 12, 42, 35, 19, 17, 10, 27, 28, 39, 32, 36, 46 };

static void cues_read(void) {
    FILE *f = fopen(SOUNDS_FILE, "r");
    char line_[80];
    while (f && fgets(line_, sizeof line_, f)) {
        char name[32];
        int n;
        if (sscanf(line_, " %31[a-z_] = %d", name, &n) != 2) continue;
        for (int i = 0; i < NCUES; ++i)
            if (!strcmp(name, cue_name[i])) cue[i] = n;
    }
    if (f) fclose(f), logf_("Sounds: %s read", SOUNDS_FILE);
}

static void play(int c, int vol) { snd_play(sfx(cue[c]), vol, 0, 0); }

/* a sound from where it happens round the tube: panned by its side, quieter far down */
static void play_at(int c, float a, float z, int vol) {
    int v = (int)(vol * (1.0f - z * 0.16f));
    snd_play(sfx(cue[c]), clampi(v, 30, 256), (int)(fcos(a) * 140), 0);
}

/* ---------------------------------------------------------------------------------------------
 * Save: best score and levels, totals, last game, options, each level's best time */

static struct {
    int best_score, best_level, bricks, bonuses, games;
    int last_score, last_level, last_bricks, last_bonuses;
    int control; /* 0 wheel rotation, 1 finger position */
    int sfx;     /* 0-10 */
    int best_time[64];
} save = { .sfx = 10 };

static void save_read(void) {
    FILE *f = fopen(SAVE_FILE, "r");
    if (!f) return;
    char key[32];
    int v, i;
    while (fscanf(f, "%31s", key) == 1) {
        if (!strcmp(key, "time") && fscanf(f, "%d %d", &i, &v) == 2) {
            if (i >= 0 && i < 64) save.best_time[i] = v;
            continue;
        }
        if (fscanf(f, "%d", &v) != 1) break;
#define K_(n) if (!strcmp(key, #n)) save.n = v;
        K_(best_score) K_(best_level) K_(bricks) K_(bonuses) K_(games) K_(last_score) K_(last_level) K_(last_bricks)
        K_(last_bonuses) K_(control) K_(sfx)
#undef K_
    }
    fclose(f);
    save.sfx = clampi(save.sfx, 0, 10);
    save.control = clampi(save.control, 0, 1);
}

static void save_write(void) {
    FILE *f = fopen(SAVE_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "best_score %d\nbest_level %d\nbricks %d\nbonuses %d\ngames %d\nlast_score %d\nlast_level %d\n"
               "last_bricks %d\nlast_bonuses %d\ncontrol %d\nsfx %d\n",
            save.best_score, save.best_level, save.bricks, save.bonuses, save.games, save.last_score, save.last_level,
            save.last_bricks, save.last_bonuses, save.control, save.sfx);
    for (int i = 0; i < 64; ++i)
        if (save.best_time[i]) fprintf(f, "time %d %d\n", i, save.best_time[i]);
    fclose(f);
    rename(SAVE_FILE ".tmp", SAVE_FILE);
}

/* ---------------------------------------------------------------------------------------------
 * Words: the iPod game's, with %s filled in */

static void fmt(char *out, int size, int id, const char *fallback, const char *arg) {
    const char *s = str(id, fallback);
    int n = 0;
    for (; *s && n < size - 1; ++s) {
        if (s[0] == '%' && s[1] == 's') {
            for (const char *a = arg; a && *a && n < size - 1; ++a) out[n++] = *a;
            ++s;
        } else if (s[0] == '%' && s[1] == '%') {
            out[n++] = '%', ++s;
        } else {
            out[n++] = *s;
        }
    }
    out[n] = 0;
}

static void mmss(char *out, int size, int s) { snprintf(out, (unsigned)size, "%d:%02d", s / 60, s % 60); }

/* ---------------------------------------------------------------------------------------------
 * Art */

static const char *themes[] = { "airduct", "blackhole", "candy", "circuits", "dna", "greenGrid", "ice", "lava",
                                "metaltube", "nuclear", "pipes", "plasma", "sewer", "well", "wire" };
static const char *doors[] = { "airDuct", "blackhole", "candy", "circuits", "dna", "greenGrid", "ice", "lava",
                               "metalTube", "nuclear", "pipes", "plasma", "sewer", "well", "wire" };
#define NTHEMES 15

static Img menu_bg, splash_bg, help_pic[6];
static Spr icons_a[6], icons_b[4], icons_c[6], icons_d[5], dial[5];
static uint8_t *centre_shade;
static int loaded_theme = -1;

static void make_shade(void) {
    /* the backgrounds' own mask: a dark disc in the middle, where the tube runs away */
    Img a = img_ipd(zip, "Vortex/Backgrounds/bgAlpha.ipd"), c = img_cover(&a);
    centre_shade = malloc(VIEW_W * VIEW_H);
    for (int i = 0; i < VIEW_W * VIEW_H; ++i) {
        int m = c.px ? (int)(c.px[i] >> 24) : 255;
        float dx = i % VIEW_W - CX, dy = i / VIEW_W - CY, d = fsqrt(dx * dx + dy * dy);
        int rim = d > 150 ? (int)((d - 150) * 1.2f) : 0; /* the corners darker, beyond the rim */
        centre_shade[i] = (uint8_t)clampi(96 + m * 140 / 255 - rim, 40, 236);
    }
    img_free(&a), img_free(&c);
}

static void load_theme(int t) {
    t %= NTHEMES;
    if (t == loaded_theme) return;
    char path[96];
    img_free(&art.bg), img_free(&art.door);
    snprintf(path, sizeof path, "Vortex/Backgrounds/%s.ipd", themes[t]);
    Img raw = img_ipd(zip, path);
    art.bg = img_cover(&raw);
    img_free(&raw);
    /* the centre's shade, put in once: each frame then only copies the picture */
    for (int i = 0; art.bg.px && i < VIEW_W * VIEW_H; ++i) {
        uint32_t c = art.bg.px[i], k = centre_shade[i];
        art.bg.px[i] = 0xff000000u | ((c & 0xff00ff) * k >> 8 & 0xff00ff) | ((c & 0xff00) * k >> 8 & 0xff00);
    }
    snprintf(path, sizeof path, "Vortex/Backgrounds/%s_Door1.ipd", doors[t]);
    art.door = img_ipd(zip, path);
    loaded_theme = t;
}

/* the picture fitted inside the view, the rest black (the help screens keep their whole picture) */
static Img img_contain(const Img *src) {
    Img o = { 0 };
    if (!src || !src->px) return o;
    o.px = calloc(VIEW_W * VIEW_H, 4);
    o.w = VIEW_W, o.h = VIEW_H;
    float s = (float)VIEW_W / src->w < (float)VIEW_H / src->h ? (float)VIEW_W / src->w : (float)VIEW_H / src->h;
    int dw = (int)(src->w * s), dh = (int)(src->h * s), ox = (VIEW_W - dw) / 2, oy = (VIEW_H - dh) / 2;
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x) {
            float fx = clampf((x + 0.5f) / s - 0.5f, 0, src->w - 1.001f), fy = clampf((y + 0.5f) / s - 0.5f, 0, src->h - 1.001f);
            int x0 = (int)fx, y0 = (int)fy, ax = (int)((fx - x0) * 256), ay = (int)((fy - y0) * 256);
            const uint32_t *p = src->px + y0 * src->w + x0;
            uint32_t out = 0xff000000u;
            for (int sh = 0; sh < 24; sh += 8) {
                int a = (int)(p[0] >> sh & 255), b = (int)(p[1] >> sh & 255), c = (int)(p[src->w] >> sh & 255), d = (int)(p[src->w + 1] >> sh & 255);
                out |= (uint32_t)(((a * (256 - ax) + b * ax) * (256 - ay) + (c * (256 - ax) + d * ax) * ay) >> 16) << sh;
            }
            o.px[(oy + y) * VIEW_W + ox + x] = out;
        }
    return o;
}

static void strip(Spr *out, int n, int ti) {
    for (int i = 0; i < n; ++i) out[i] = spr_cell(tex(ti), n, 1, i);
}

static int find_ipg(char *out, int size) {
    DIR *d = opendir(".");
    struct dirent *e;
    int found = 0;
    while (d && (e = readdir(d))) {
        int n = (int)strlen(e->d_name);
        if (n > 4 && !strcasecmp(e->d_name + n - 4, ".ipg")) {
            snprintf(out, (unsigned)size, "./%s", e->d_name);
            found = 1;
            break;
        }
    }
    if (d) closedir(d);
    return found;
}

/* a full-screen note in the game's own font, large enough to read at arm's length: it needs none of
 * the .ipg, so it serves when the .ipg is missing or wrong */
static void notice(const char *title, const char *body) {
    fill(0x020806);
    for (int i = 0; i < 3; ++i) { /* a faint tube behind the words */
        float r = 150 - i * 34;
        for (int k = 0; k < 64; ++k) {
            float a0 = k * TAU / 64, a1 = (k + 1) * TAU / 64;
            line(CX + r * fcos(a0), CY - r * fsin(a0), CX + r * fcos(a1), CY - r * fsin(a1), 2, 0x1f6a2a, 120 - i * 30);
        }
    }
    vtext_outline(title, CX, 44, 24, WHITE, 0x1f9a2a, 256, 0);
    vtext_wrap(body, CX, 100, 300, 15, WHITE, 256);
    vtext_glow("Hold Return to leave", CX, 272, 12, 0xa8d8a0, 256, 0);
    present(fb);
    present(fb); /* both pages */
}

static int load_all(void) {
    char path[300];
    if (!find_ipg(path, sizeof path)) {
        notice("NO VORTEX .IPG", "Copy your Vortex .ipg file into the .vortex folder on the card, then start Vortex again.");
        return 0;
    }
    logf_("Reading %s", path);
    zip = zip_open(path);
    if (zip) {
        Img s = img_lcd5(zip, "Vortex/Vortex.raw.lcd5");
        splash_bg = img_contain(&s);
        img_free(&s);
        if (splash_bg.px) blit(&splash_bg, 0, 0), present(fb);
    }
    if (getenv("VX_HOLDLOAD")) sleep_us(1500000); /* host tests: time to see the picture */
    if (!zip || tex_load(zip, "Vortex/tex") < 40 || levels_load(zip, "Vortex/gamedata/levels/levels") < 1) {
        notice("WRONG .IPG", "This .ipg isn't Vortex, or it is damaged. Put your Vortex .ipg in the .vortex folder. vortex.log there says more.");
        return 0;
    }
    sfx_load(zip, "Vortex/media/sfx");
    strings_load(zip, "Vortex/Localization/en.lproj/text.strings");
    cues_read();

    /* the texture bank's entries, by what they show */
    art.ball = spr_cell(tex(0), 2, 1, 0);
    for (int i = 0; i < 9; ++i) art.fireball[i] = spr_cell(tex(1), 9, 1, i);
    for (int i = 0; i < 2; ++i) {
        art.bat[2][i] = spr_cell(tex(8), 2, 1, i);
        art.bat[1][i] = spr_cell(tex(9), 2, 1, i);
        art.bat[0][i] = spr_cell(tex(10), 2, 1, i);
    }
    for (int i = 0; i < 12; ++i) art.icon[i] = spr_cell_up(tex(12), 4, 3, i);
    art.brick_plain = spr_cell_up(tex(13), 6, 3, 5);
    for (int i = 0; i < 5; ++i) art.brick_bomb[i] = spr_cell_up(tex(13), 6, 3, i);
    for (int i = 0; i < 6; ++i) art.brick_bonus[i] = spr_cell_up(tex(13), 6, 3, 6 + i), art.brick_crack[i] = spr_cell_up(tex(13), 6, 3, 12 + i);
    art.clock = spr_whole(tex(18));
    for (int i = 0; i < 8; ++i) art.boom[i] = spr_cell_up(tex(22), 4, 2, i);
    art.heart = spr_whole(tex(25));
    art.bar = spr_whole(tex(26));
    art.battery = spr_whole(tex(11));
    for (int i = 0; i < 12; ++i) art.gun[i] = spr_cell_up(tex(28), 4, 3, i);
    for (int i = 0; i < 16; ++i) art.debris[i] = spr_cell_up(tex(41), 4, 4, i);
    art.ring = spr_whole(tex(4));
    strip(icons_a, 6, 34), strip(icons_b, 4, 35), strip(icons_c, 6, 39), strip(icons_d, 5, 40);
    for (int i = 0; i < 5; ++i) dial[i] = spr_whole(tex(29 + i));
    {   /* the menus' backdrop: the sewer's tube, dimmed, as the iPod game's own menus */
        Img raw = img_ipd(zip, "Vortex/Backgrounds/sewer.ipd");
        menu_bg = img_cover(&raw);
        img_free(&raw);
        if (!menu_bg.px) menu_bg = img_cover(tex(7));
        for (int i = 0; menu_bg.px && i < VIEW_W * VIEW_H; ++i) {
            uint32_t c = menu_bg.px[i];
            float dx = i % VIEW_W - CX, dy = i / VIEW_W - CY;
            unsigned k = (unsigned)clampi(150 - (int)fsqrt(dx * dx + dy * dy) / 3, 60, 150);
            menu_bg.px[i] = 0xff000000u | ((c & 0xff00ff) * k >> 8 & 0xff00ff) | ((c & 0xff00) * k >> 8 & 0xff00);
        }
    }

    static const char *helps[6] = { "BasicGameplay", "DoubleTap", "FingerPosition", "WheelRotation", "BonusTypes", "BrickTypes" };
    for (int i = 0; i < 6; ++i) {
        snprintf(path, sizeof path, "Vortex/UI3/%s.ipd", helps[i]);
        Img p = img_ipd(zip, path);
        help_pic[i] = img_contain(&p);
        img_free(&p);
    }
    make_shade();
    polar_init();
    return 1;
}

/* ---------------------------------------------------------------------------------------------
 * The level */

float bat_half(void) {
    static const float half[3] = { 0.17f, 0.26f, 0.36f };
    return half[clampi(g.bat_size, 0, 2)];
}

int brick_live(const Brick *b) { return b->type && b->type != B_SOLID; }

static void count_bricks(void) {
    int n = 0;
    for (int k = 0; k < RINGS; ++k)
        for (int j = 0; j < SEGS; ++j) n += brick_live(&g.brick[k][j]);
    g.bricks_left = n;
}

/* past the last level the game's own: random rings, faster, round after round */
static void random_level(Level *l, int n) {
    rnd_seed(0x9e3779b9u * (unsigned)n);
    memset(l, 0, sizeof *l);
    for (int k = 0; k < RINGS; ++k) {
        int period = 1 << (rnd() % 4 + 1), colour = (int)(rnd() % 8), empty = (int)(rnd() % 3);
        uint16_t pat[16];
        for (int j = 0; j < period; ++j) {
            unsigned r = rnd() % 100;
            int type = r < 26 ? 0 : r < 52 ? 1 : r < 66 ? 2 : r < 76 ? 3 : r < 82 ? 4 : r < 87 ? 5 : 6;
            if (j < empty && rnd() % 2) type = 0;
            pat[j] = !type ? 0 : type == 4 ? (uint16_t)(4 << 13 | rnd() % NBONUS) : (uint16_t)(type << 13 | (colour + (j & 1)) % 8 << 2);
        }
        for (int j = 0; j < SEGS; ++j) l->brick[k][j] = pat[j % period];
        l->spin[k] = (short)(((int)(rnd() % 13) - 6) * 8);
    }
    l->time_to_beat = 180;
    l->speed = 180 + clampi(n - 40, 0, 20) * 3;
    l->speed_max = l->speed + 30;
}

static void ball_reset(void);

static void level_start(int n) {
    Level rl;
    const Level *l = level(n - 1);
    if (!l) random_level(&rl, n), l = &rl;
    memset(g.brick, 0, sizeof g.brick);
    for (int k = 0; k < RINGS; ++k) {
        for (int j = 0; j < SEGS; ++j) {
            unsigned w = l->brick[k][j];
            if (!w) continue;
            Brick *b = &g.brick[k][j];
            static const uint8_t types[8] = { B_ONE, B_ONE, B_TWO, B_THREE, B_BONUS, B_SOLID, B_BOMB, B_ONE };
            b->type = types[w >> 13 & 7];
            b->hits = b->max_hits = b->type == B_TWO ? 2 : b->type == B_THREE ? 3 : 1;
            if (b->type == B_BONUS) b->bonus = (uint8_t)((w & 31) % NBONUS), b->colour = 2;
            else b->colour = (uint8_t)(w >> 2 & 7);
            /* a flagged brick sits off its ring's middle, by (offset - 16) / 32 of the ring spacing */
            if (w & 0x800) b->dz = clampf(((int)(w >> 5 & 63) - 16) / 32.0f * RING_STEP, -0.17f, 0.17f);
        }
        g.ring_a[k] = 0;
        g.ring_spin[k] = l->spin[k] * (TAU / 720); /* the file's number, as degrees a second over two */
    }
    g.level = n;
    g.time_to_beat = l->time_to_beat;
    g.speed_base = l->speed / 100.0f;
    g.speed_max = l->speed_max / 100.0f;
    g.speed = g.speed_base;
    g.speed_mul = 1;
    g.clock = 0;
    g.door_frame = 15;
    g.door_t = 0;
    memset(g.cap, 0, sizeof g.cap);
    memset(g.shot, 0, sizeof g.shot);
    memset(g.fx, 0, sizeof g.fx);
    g.sticky_t = g.guns_t = g.laser_t = g.power_t = g.multibat_t = 0;
    g.theme = (n - 1) % NTHEMES;
    load_theme(g.theme);
    count_bricks();
    ball_reset();
}

static void ball_reset(void) {
    memset(g.ball, 0, sizeof g.ball);
    g.ball[0].alive = 1, g.ball[0].stuck = 1, g.ball[0].stuck_off = 0.05f;
    g.ball[0].a = g.bat_a, g.ball[0].z = BALL_R + 0.01f;
    g.bat_size = 1;
    g.lost_t = 0;
}

static void game_new(int start_level) {
    memset(&g, 0, sizeof g);
    g.lives = START_LIVES;
    g.bat_a = g.bat_target = -PI / 2; /* the bottom of the tube */
    g.bat_size = 1;
    level_start(start_level);
    ++save.games;
    save.last_score = save.last_level = save.last_bricks = save.last_bonuses = 0;
}

/* ---------------------------------------------------------------------------------------------
 * Effects */

static Fx *fx_add(int kind, float a, float z, float dur, uint32_t colour) {
    for (int i = 0; i < MAX_FX; ++i)
        if (g.fx[i].dur <= 0) {
            Fx *f = &g.fx[i];
            memset(f, 0, sizeof *f);
            f->kind = kind, f->a = a, f->z = z, f->dur = dur, f->colour = colour;
            return f;
        }
    return 0;
}

static void popup(float a, float z, const char *text, uint32_t colour) {
    Fx *f = fx_add(FX_TEXT, a, z, 0.9f, colour);
    if (f) snprintf(f->text, sizeof f->text, "%s", text);
}

static void banner(const char *s) {
    snprintf(g.banner, sizeof g.banner, "%s", s);
    g.banner_t = 1.6f;
}

/* ---------------------------------------------------------------------------------------------
 * Bricks */

static float brick_mid_a(int k, int j) { return g.ring_a[k] + (j + 0.5f) * SEG_W; }
static float brick_mid_z(int k, int j) { return RING_Z(k) + g.brick[k][j].dz; }

static void cap_drop(int kind, float a, float z) {
    for (int i = 0; i < MAX_CAPS; ++i)
        if (!g.cap[i].alive) {
            g.cap[i] = (Cap){ 1, kind, a, z, 0 };
            play_at(S_DROP, a, z, 180);
            return;
        }
}

static void brick_break(int k, int j, int by_blast);

/* a hit on brick (k, j): power 1 a knock, 99 through anything breakable */
static int brick_hit(int k, int j, int power) {
    Brick *b = &g.brick[k][j];
    if (!b->type) return 0;
    float a = brick_mid_a(k, j), z = brick_mid_z(k, j);
    b->flash = 0.18f;
    if (b->type == B_SOLID) {
        play_at(S_SOLID, a, z, 150);
        fx_add(FX_SPARK, a, z, 0.25f, 0xc0d0ff);
        return 1;
    }
    if (b->hits > power) {
        b->hits = (uint8_t)(b->hits - power);
        g.score += 5;
        play_at(S_CRACK, a, z, 200);
        fx_add(FX_SPARK, a, z, 0.3f, brick_colour(b->colour));
        return 1;
    }
    brick_break(k, j, 0);
    return 1;
}

static void brick_break(int k, int j, int by_blast) {
    Brick *b = &g.brick[k][j];
    if (!b->type || b->type == B_SOLID) return;
    float a = brick_mid_a(k, j), z = brick_mid_z(k, j);
    static const int points[] = { 0, 10, 20, 30, 25, 0, 15 };
    int pts = points[b->type] * (10 + g.level) / 10;
    g.score += pts;
    ++g.bricks_broken;
    char t[16];
    snprintf(t, sizeof t, "%d", pts);
    popup(a, z, t, 0xffffff);
    Fx *f = fx_add(FX_DEBRIS, a, z, 0.55f, brick_colour(b->colour));
    (void)f;
    int type = b->type, bonus = b->bonus;
    b->type = B_NONE;
    if (type == B_BONUS) cap_drop(bonus, a, z);
    if (type == B_BOMB) {
        play_at(S_BOMB, a, z, 230);
        fx_add(FX_BOOM, a, z, 0.6f, 0xffa040);
        g.shake = 0.25f;
        /* the neighbours round the ring, and those it overlaps in the rings either side, go too */
        for (int d = -1; d <= 1; d += 2) {
            Brick *n = &g.brick[k][(j + d + SEGS) % SEGS];
            if (n->type && n->type != B_SOLID && n->fuse <= 0) n->fuse = 0.12f;
        }
        for (int dk = -1; dk <= 1; dk += 2) {
            int kk = k + dk;
            if (kk < 0 || kk >= RINGS) continue;
            for (int jj = 0; jj < SEGS; ++jj) {
                float da = fabs_(wrap_pi(brick_mid_a(kk, jj) - a));
                Brick *n = &g.brick[kk][jj];
                if (da < SEG_W * 0.98f && n->type && n->type != B_SOLID && n->fuse <= 0) n->fuse = 0.16f;
            }
        }
    } else {
        play_at(S_BRICK, a, z, by_blast ? 140 : 220);
    }
    count_bricks();
}

/* ---------------------------------------------------------------------------------------------
 * Bonuses */

static void bonus_take(int kind, float a) {
    char name[40];
    static const char *fallback[NBONUS] = { "Extra Life", "Multi-Ball", "Shrink", "Stretch", "Multi-Bat", "Sticky Bat",
                                            "Guns", "Laser", "Power Ball", "Slower", "Faster" };
    snprintf(name, sizeof name, "%s", str(400 + kind, fallback[kind]));
    banner(name);
    g.score += 50;
    ++g.bonuses_got;
    switch (kind) {
    case BN_LIFE: g.lives = clampi(g.lives + 1, 0, 9), play(S_LIFE, 230); return;
    case BN_MULTIBALL:
        for (int i = 0, n = 0; i < MAX_BALLS && n < 2; ++i) {
            if (!g.ball[i].alive || g.ball[i].stuck) continue;
            Ball src = g.ball[i];
            for (int s = -1; s <= 1 && n < 2; s += 2)
                for (int m = 0; m < MAX_BALLS; ++m)
                    if (!g.ball[m].alive) {
                        Ball nb = src;
                        float c = fcos(0.45f * s), sn = fsin(0.45f * s);
                        nb.u = src.u * c - src.vz * sn, nb.vz = src.u * sn + src.vz * c;
                        g.ball[m] = nb, ++n;
                        break;
                    }
            break;
        }
        break;
    case BN_SHRINK: g.bat_size = clampi(g.bat_size - 1, 0, 2), play(S_SHRINK, 220); return;
    case BN_STRETCH: g.bat_size = clampi(g.bat_size + 1, 0, 2), play(S_STRETCH, 220); return;
    case BN_MULTIBAT: g.multibat_t = 20; break;
    case BN_STICKY: g.sticky_t = 20; break;
    case BN_GUNS: g.guns_t = 20, g.laser_t = 0; break;
    case BN_LASER: g.laser_t = 20, g.guns_t = 0; break;
    case BN_POWER: g.power_t = 12; break;
    case BN_SLOWER: g.speed_mul = clampf(g.speed_mul * 0.8f, 0.6f, 1.5f); break;
    case BN_FASTER: g.speed_mul = clampf(g.speed_mul * 1.25f, 0.6f, 1.5f); break;
    }
    (void)a;
    play(S_BONUS, 220);
}

/* ---------------------------------------------------------------------------------------------
 * The ball */

static float cur_speed(void) { return g.speed * g.speed_mul; }

/* keep the ball's speed, and keep it going along the tube at least a little */
static void ball_norm(Ball *b) {
    float s = cur_speed(), m = fsqrt(b->u * b->u + b->vz * b->vz);
    if (m < 1e-4f) b->vz = s, b->u = 0, m = s;
    b->u *= s / m, b->vz *= s / m;
    float minz = s * MIN_ALONG; /* never more than about 32 degrees off straight down the tube */
    if (fabs_(b->vz) < minz) {
        b->vz = b->vz < 0 ? -minz : minz;
        float uu = fsqrt(s * s - minz * minz);
        b->u = b->u < 0 ? -uu : uu;
    }
}

static void launch(Ball *b) {
    float t = clampf(b->stuck_off / bat_half(), -1, 1) * BAT_STEER * 0.6f + (rndf() - 0.5f) * 0.06f;
    float s = cur_speed();
    b->stuck = 0;
    b->u = s * fsin(t), b->vz = s * fcos(t);
    ball_norm(b);
    play_at(S_LAUNCH, b->a, 0, 180);
}

/* the ball against one brick, in the wall's unrolled plane (arc length at R_BALL by depth);
 * 1 if it hit */
static int ball_brick(Ball *b, int k, int j) {
    Brick *br = &g.brick[k][j];
    if (!br->type) return 0;
    float hw = SEG_W / 2 * R_BALL, hd = BRICK_D / 2;
    float da = wrap_pi(b->a - brick_mid_a(k, j)) * R_BALL, dz = b->z - brick_mid_z(k, j);
    float cx = clampf(da, -hw, hw), cz = clampf(dz, -hd, hd);
    float ex = da - cx, ez = dz - cz, d2 = ex * ex + ez * ez;
    if (d2 >= BALL_R * BALL_R && !(fabs_(da) < hw && fabs_(dz) < hd)) return 0;
    int through = g.power_t > 0 && br->type != B_SOLID;
    if (!through) {
        float ring_u = g.ring_spin[k] * R_BALL; /* the brick's own speed round the tube */
        if (fabs_(da) < hw && fabs_(dz) >= hd * 0.0f && (fabs_(dz) - hd) > (fabs_(da) - hw)) {
            /* the near or far face */
            b->vz = dz < 0 ? -fabs_(b->vz) : fabs_(b->vz);
            b->z = brick_mid_z(k, j) + (dz < 0 ? -(hd + BALL_R) : hd + BALL_R);
        } else if (fabs_(dz) < hd) {
            /* a side: away from the brick, as the brick moves */
            float rel = b->u - ring_u;
            rel = da < 0 ? -fabs_(rel) : fabs_(rel);
            b->u = rel + ring_u;
            b->a = brick_mid_a(k, j) + (da < 0 ? -(hw + BALL_R) : hw + BALL_R) / R_BALL;
        } else {
            /* a corner: off the corner's normal */
            float d = fsqrt(d2) + 1e-6f, nx = ex / d, nz = ez / d;
            float rel = b->u - ring_u, dot = rel * nx + b->vz * nz;
            if (dot < 0) rel -= 2 * dot * nx, b->vz -= 2 * dot * nz;
            b->u = rel + ring_u;
            b->a += nx * (BALL_R - d) / R_BALL, b->z += nz * (BALL_R - d);
        }
        ball_norm(b);
    }
    brick_hit(k, j, through ? 99 : 1);
    return 1;
}

static void ball_step(Ball *b, float dt) {
    if (b->stuck) {
        b->a = g.bat_a + b->stuck_off;
        b->z = BALL_R + 0.01f;
        return;
    }
    b->a = wrap_tau(b->a + b->u / R_BALL * dt);
    b->z += b->vz * dt;
    /* the far end */
    if (b->z > ZFAR - BALL_R && b->vz > 0) {
        b->vz = -b->vz;
        b->u += (rndf() - 0.5f) * 0.05f; /* never quite the same line twice */
        ball_norm(b);
        play_at(S_WALL, b->a, ZFAR, 90);
    }
    /* the bricks: in the rings within reach, those about the ball's angle */
    for (int k = 0; k < RINGS; ++k) {
        if (fabs_(b->z - RING_Z(k)) > BRICK_D / 2 + 0.2f + BALL_R) continue;
        int j0 = (int)(wrap_tau(b->a - g.ring_a[k]) / SEG_W);
        for (int d = -1; d <= 1; ++d)
            if (ball_brick(b, k, (j0 + d + SEGS) % SEGS)) return;
    }
    /* the bat */
    if (b->vz < 0 && b->z < BALL_R + 0.005f && b->z > -0.06f) {
        for (int bat = 0; bat < (g.multibat_t > 0 ? 2 : 1); ++bat) {
            float ba = g.bat_a + bat * PI, off = wrap_pi(b->a - ba), h = bat_half() + BALL_R / R_BALL * 0.6f;
            if (fabs_(off) > h) continue;
            if (g.sticky_t > 0) {
                b->stuck = 1, b->stuck_off = off, b->u = b->vz = 0;
                if (bat) b->stuck_off += PI;
                play_at(S_BAT, b->a, 0, 160);
                return;
            }
            float t = clampf(off / h, -1, 1) * BAT_STEER; /* where it hit: the ends send it round a little */
            float s = cur_speed();
            b->vz = s * fcos(t), b->u = s * fsin(t);
            b->z = BALL_R + 0.005f;
            ball_norm(b);
            /* each return a little quicker, up to the level's top speed */
            g.speed = clampf(g.speed + (g.speed_max - g.speed_base) / 10, g.speed_base, g.speed_max);
            play_at(S_BAT, b->a, 0, 220);
            fx_add(FX_SPARK, b->a, 0.02f, 0.2f, 0xa0c0ff);
            return;
        }
    }
    if (b->z < -0.35f) b->alive = 0;
}

/* ---------------------------------------------------------------------------------------------
 * Weapons */

static void fire(void) {
    if (g.fire_cool > 0) return;
    if (g.guns_t > 0) {
        float h = bat_half() * 0.82f;
        for (int side = -1; side <= 1; side += 2)
            for (int i = 0; i < MAX_SHOTS; ++i)
                if (!g.shot[i].alive) {
                    g.shot[i] = (Shot){ 1, wrap_tau(g.bat_a + side * h), 0.05f };
                    break;
                }
        g.fire_cool = 0.3f;
        play(S_GUN, 170);
    } else if (g.laser_t > 0) {
        /* the beam hits the nearest brick straight down the tube from the bat's middle */
        float best = ZFAR;
        int bk = -1, bj = -1;
        for (int k = 0; k < RINGS; ++k) {
            int j = (int)(wrap_tau(g.bat_a - g.ring_a[k]) / SEG_W) % SEGS;
            const Brick *b = &g.brick[k][j];
            if (!b->type) continue;
            float z = brick_mid_z(k, j) - BRICK_D / 2;
            if (z < best) best = z, bk = k, bj = j;
        }
        g.laser_a = g.bat_a, g.laser_len = best, g.laser_beam = 0.18f;
        if (bk >= 0) brick_hit(bk, bj, 1);
        g.fire_cool = 0.45f;
        play(S_LASER, 190);
    }
}

static void shots_step(float dt) {
    for (int i = 0; i < MAX_SHOTS; ++i) {
        Shot *s = &g.shot[i];
        if (!s->alive) continue;
        s->z += 3.2f * dt;
        if (s->z > ZFAR) {
            s->alive = 0;
            continue;
        }
        for (int k = 0; k < RINGS; ++k) {
            int j = (int)(wrap_tau(s->a - g.ring_a[k]) / SEG_W) % SEGS;
            const Brick *b = &g.brick[k][j];
            if (b->type && fabs_(s->z - brick_mid_z(k, j)) < BRICK_D / 2) {
                brick_hit(k, j, 1);
                s->alive = 0;
                break;
            }
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * Input */

typedef struct {
    int wheel;
    int centre, next, prev, play, back;
    int next_held, prev_held;
    int tap, tap_x, tap_y;
    int touch, touch_x, touch_y;
} Input;

static Input in;
#define REPEAT_GAP 600000
static uint8_t key_held[256];
static uint64_t key_last[256];

/* Q2 Pod passes a held button's repeats on as more key-downs: while a key is held, a down is a
 * repeat, and only the first counts (as Challenger Deep) */
static void read_input(void) {
    int kind, a, b, c;
    static int touch_moved;
    while (next_event(&kind, &a, &b, &c)) {
        if (kind == 'k') {
            int k = b & 255;
            uint64_t t = now_us();
            if (a) {
                int repeat = key_held[k] && t - key_last[k] < REPEAT_GAP;
                key_held[k] = 1, key_last[k] = t;
                if (k == K_NEXT) in.next_held = 1;
                if (k == K_PREV) in.prev_held = 1;
                if (repeat) continue;
            } else {
                key_held[k] = 0;
                if (k == K_NEXT) in.next_held = 0;
                if (k == K_PREV) in.prev_held = 0;
            }
        }
        if (kind == 'w') in.wheel += a;
        else if (kind == 'k' && a) {
            if (b == K_CENTRE) in.centre = 1;
            if (b == K_NEXT) in.next = 1;
            if (b == K_PREV) in.prev = 1;
            if (b == K_PLAY) in.play = 1;
            if (b == K_RETURN) in.back = 1;
        } else if (kind == 't') {
            if (a == 'd') in.touch = 1, in.touch_x = b, in.touch_y = c, touch_moved = 0;
            if (a == 'm' && in.touch) {
                int dx = b - in.touch_x, dy = c - in.touch_y;
                if (dx * dx + dy * dy > 15 * 15) touch_moved = 1;
                in.touch_x = b, in.touch_y = c;
            }
            if (a == 'u' && in.touch) {
                if (!touch_moved) in.tap = 1, in.tap_x = in.touch_x, in.tap_y = in.touch_y;
                in.touch = 0;
            }
        }
    }
}

static void presses_used(void) { in.wheel = in.centre = in.next = in.prev = in.play = in.back = in.tap = 0; }

static int wheel_acc;
static int wheel_steps(void) { /* menus: a step a fifth of a turn, anticlockwise to the left */
    wheel_acc += in.wheel;
    int n = 0;
    while (wheel_acc <= -40) wheel_acc += 40, ++n;
    while (wheel_acc >= 40) wheel_acc -= 40, --n;
    return n;
}

/* ---------------------------------------------------------------------------------------------
 * A step of play */

enum { ST_SPLASH, ST_MENU, ST_HELP, ST_STATS, ST_SOUNDS, ST_INTRO, ST_PLAY, ST_PAUSE, ST_LOST, ST_CLEAR, ST_OVER };
static int state = ST_SPLASH;
static int autopilot;

static void move_bat(float dt) {
    float before = g.bat_a;
    g.bat_a += in.wheel * (TAU / 200);
    if (in.next_held) g.bat_a -= 4.0f * dt;
    if (in.prev_held) g.bat_a += 4.0f * dt;
    if (in.touch && save.control == 1) {
        float dx = in.touch_x - CX, dy = CY - in.touch_y;
        if (dx * dx + dy * dy > 30 * 30) {
            float d = wrap_pi(fatan2(dy, dx) - g.bat_a), mx = 10.0f * dt;
            g.bat_a += clampf(d, -mx, mx);
        }
    }
    if (autopilot) { /* host tests: the bat follows the nearest ball coming back */
        float best = 99, ta = g.bat_a;
        for (int i = 0; i < MAX_BALLS; ++i) {
            const Ball *b = &g.ball[i];
            if (b->alive && !b->stuck && b->vz < 0 && b->z < best) best = b->z, ta = b->a;
        }
        for (int i = 0; i < MAX_CAPS; ++i)
            if (g.cap[i].alive && g.cap[i].z < best - 0.4f) best = g.cap[i].z, ta = g.cap[i].a;
        float d = wrap_pi(ta - g.bat_a), mx = 6.0f * dt;
        g.bat_a += clampf(d, -mx, mx);
    }
    g.bat_a = wrap_tau(g.bat_a);
    (void)before;
}

static void lose_life(void);

static void step_play(float dt) {
    move_bat(dt);
    g.clock += dt;
    for (int k = 0; k < RINGS; ++k) g.ring_a[k] = wrap_tau(g.ring_a[k] + g.ring_spin[k] * dt);

    int stuck = 0;
    for (int i = 0; i < MAX_BALLS; ++i) stuck |= g.ball[i].alive && g.ball[i].stuck;
    if (in.centre || in.tap || (autopilot && stuck)) {
        if (stuck) {
            for (int i = 0; i < MAX_BALLS; ++i)
                if (g.ball[i].alive && g.ball[i].stuck) launch(&g.ball[i]);
        } else {
            fire();
        }
    }
    if (autopilot && (g.guns_t > 0 || g.laser_t > 0)) fire();

    int alive = 0;
    for (int i = 0; i < MAX_BALLS; ++i) {
        if (!g.ball[i].alive) continue;
        ball_step(&g.ball[i], dt);
        alive += g.ball[i].alive;
    }

    for (int i = 0; i < MAX_CAPS; ++i) {
        Cap *c = &g.cap[i];
        if (!c->alive) continue;
        c->t += dt;
        c->z -= 0.75f * dt;
        if (c->z < 0.08f && c->z > -0.1f) {
            for (int bat = 0; bat < (g.multibat_t > 0 ? 2 : 1); ++bat)
                if (fabs_(wrap_pi(c->a - g.bat_a - bat * PI)) < bat_half() + 0.12f) {
                    bonus_take(c->kind, c->a);
                    c->alive = 0;
                    break;
                }
        }
        if (c->z < -0.3f) c->alive = 0;
    }
    shots_step(dt);

    for (int k = 0; k < RINGS; ++k)
        for (int j = 0; j < SEGS; ++j) {
            Brick *b = &g.brick[k][j];
            if (b->flash > 0) b->flash -= dt;
            if (b->fuse > 0 && (b->fuse -= dt) <= 0) b->fuse = 0, brick_break(k, j, 1);
        }
    for (int i = 0; i < MAX_FX; ++i)
        if (g.fx[i].dur > 0 && (g.fx[i].t += dt) >= g.fx[i].dur) g.fx[i].dur = 0;

    float *timers[] = { &g.sticky_t, &g.guns_t, &g.laser_t, &g.power_t, &g.multibat_t, &g.fire_cool, &g.laser_beam, &g.banner_t, &g.shake };
    for (unsigned i = 0; i < sizeof timers / sizeof timers[0]; ++i)
        if (*timers[i] > 0) *timers[i] -= dt;
    if (g.sticky_t <= 0) /* a stuck ball comes free when the glue runs out */
        for (int i = 0; i < MAX_BALLS; ++i)
            if (g.ball[i].alive && g.ball[i].stuck && i > 0) launch(&g.ball[i]);

    if (!g.bricks_left) {
        int fuses = 0;
        for (int k = 0; k < RINGS; ++k)
            for (int j = 0; j < SEGS; ++j) fuses += g.brick[k][j].fuse > 0;
        if (!fuses) {
            /* cleared: the door opens */
            state = ST_CLEAR;
            g.door_t = 0;
            int t = (int)g.clock, bonus = g.time_to_beat > t ? (g.time_to_beat - t) * 10 * (10 + g.level) / 10 : 0;
            g.score += bonus;
            g.banner_t = bonus; /* kept for the screen */
            if (g.level < 64 && (!save.best_time[g.level] || t < save.best_time[g.level])) save.best_time[g.level] = t ? t : 1;
            for (int i = 0; i < MAX_BALLS; ++i) g.ball[i].alive = 0;
            play(S_CLEAR, 230);
            play(S_DOOR, 160);
            return;
        }
    }
    if (!alive) lose_life();
}

static void lose_life(void) {
    play(S_LOST, 230);
    g.lost_t = 1.2f;
    g.sticky_t = g.guns_t = g.laser_t = g.power_t = g.multibat_t = 0;
    memset(g.cap, 0, sizeof g.cap);
    g.speed = g.speed_base, g.speed_mul = 1;
    state = ST_LOST;
}

static void record_game(void) {
    save.last_score = g.score;
    save.last_level = g.level - 1 + (state == ST_CLEAR);
    save.last_bricks = g.bricks_broken;
    save.last_bonuses = g.bonuses_got;
    if (g.score > save.best_score) save.best_score = g.score;
    if (save.last_level > save.best_level) save.best_level = save.last_level;
    save.bricks += g.bricks_broken;
    save.bonuses += g.bonuses_got;
    save_write();
}

/* ---------------------------------------------------------------------------------------------
 * Menus: a carousel of the iPod game's icons, the title above, what the icon does below */

typedef struct {
    const Spr *icon;
    int action;
    char label[48];
} Item;

enum { M_MAIN, M_DIFF, M_OPTIONS, M_PAUSE, M_CONFIRM };
enum { A_NEW, A_INFO, A_OPTIONS, A_HELP, A_EXIT, A_LEVEL, A_CONTROL, A_SFX, A_SOUNDS, A_CLEAR, A_BACK, A_RESUME, A_ABANDON, A_YES, A_NO };

static int menu = M_MAIN, sel, confirm_what, menu_from_pause;
static float car_pos; /* where the carousel is turned to: sel, unwrapped, eased */
static int car_target;
static Item items[8];
static int nitems;
static char menu_title[48];
static int help_page, sound_pick;

static void item(const Spr *icon, int action, const char *label) {
    if (nitems >= 8) return;
    items[nitems].icon = icon, items[nitems].action = action;
    snprintf(items[nitems].label, sizeof items[nitems].label, "%s", label);
    ++nitems;
}

static int menu_gen; /* changes whenever the items or their labels do */

static void menu_build(void) {
    char b[48], pc[8];
    nitems = 0;
    ++menu_gen;
    switch (menu) {
    case M_MAIN:
        snprintf(menu_title, sizeof menu_title, "%s", str(100, "MAIN MENU"));
        item(&icons_a[0], A_NEW, str(200, "new game"));
        item(&icons_a[3], A_INFO, str(201, "personal info"));
        item(&icons_a[4], A_OPTIONS, str(202, "options"));
        item(&icons_c[4], A_HELP, str(232, "help"));
        item(&icons_a[5], A_EXIT, str(203, "exit game"));
        break;
    case M_DIFF:
        snprintf(menu_title, sizeof menu_title, "%s", str(200, "new game"));
        for (int i = 0; i < 5; ++i) {
            static const char *fb_[5] = { "difficulty: newbie", "difficulty: rookie", "difficulty: intermediate", "difficulty: pro", "difficulty: expert" };
            item(&dial[i], A_LEVEL + 100 * i, str(214 + i, fb_[i]));
        }
        item(&icons_d[0], A_BACK, str(208, "back"));
        break;
    case M_OPTIONS:
        snprintf(menu_title, sizeof menu_title, "%s", str(103, "OPTIONS"));
        item(&icons_b[save.control ? 1 : 2], A_CONTROL, save.control ? str(212, "control: finger position") : str(213, "control: wheel rotation"));
        snprintf(pc, sizeof pc, "%d", save.sfx * 10);
        fmt(b, sizeof b, 207, "sfx: %s%%", pc);
        item(&icons_b[0], A_SFX, b);
        item(&icons_c[1], A_SOUNDS, "sound test");
        if (!menu_from_pause) item(&icons_d[2], A_CLEAR, str(205, "clear statistics"));
        item(&icons_d[0], A_BACK, str(208, "back"));
        break;
    case M_PAUSE:
        snprintf(menu_title, sizeof menu_title, "%s", str(106, "PAUSED"));
        item(&icons_c[0], A_RESUME, str(209, "resume"));
        item(&icons_a[4], A_OPTIONS, str(202, "options"));
        item(&icons_c[4], A_HELP, str(232, "help"));
        item(&icons_a[5], A_ABANDON, str(211, "abandon game"));
        break;
    case M_CONFIRM: {
        static const int titles[] = { 107, 109, 110 };
        static const char *fb_[] = { "DELETE STATISTICS?", "ABANDON GAME?", "EXIT?" };
        snprintf(menu_title, sizeof menu_title, "%s", str(titles[confirm_what], fb_[confirm_what]));
        item(&icons_d[0], A_NO, str(220, "no"));
        item(&icons_d[1], A_YES, str(219, "yes"));
        break;
    }
    }
    sel = clampi(sel, 0, nitems - 1);
}

static void menu_open(int m, int s) {
    menu = m, sel = s;
    menu_build();
    car_pos = (float)(car_target = sel);
}

static void menu_turn(int steps) {
    if (!steps || !nitems) return;
    car_target += steps;
    sel = ((car_target % nitems) + nitems) % nitems;
    play(S_MOVE, 160);
}

/* where item i sits on the carousel, as the iPod game's: the items evenly round a ring seen from a
 * little above, the chosen one at the front (bottom), the rest round the sides and the back, smaller
 * and dimmer the farther back they are */
static void item_place(int i, float *x, float *y, float *sc, int *alpha) {
    float d = i - car_pos;
    while (d > nitems / 2.0f) d -= nitems;
    while (d < -nitems / 2.0f) d += nitems;
    float t = d * TAU / (nitems > 2 ? nitems : 2), front = (fcos(t) + 1) / 2; /* 1 front, 0 back */
    *x = CX + 142 * fsin(t);
    *y = 160 + 50 * fcos(t);
    *sc = 0.62f + 0.7f * front;
    *alpha = 150 + (int)(106 * front);
}

static void to_menu(int m) {
    state = ST_MENU;
    menu_open(m, 0);
}

static void level_intro(int n) {
    level_start(n);
    state = ST_INTRO;
    play(S_START, 200);
}

static int menu_act(int action) { /* 1 to quit the game */
    switch (action % 100) {
    case A_NEW: menu_open(M_DIFF, 0); break;
    case A_LEVEL: {
        int d = action / 100;
        game_new(1 + 10 * d);
        state = ST_INTRO;
        play(S_START, 200);
        break;
    }
    case A_INFO: state = ST_STATS; break;
    case A_OPTIONS: menu_from_pause = menu == M_PAUSE, menu_open(M_OPTIONS, 0); break;
    case A_HELP: help_page = 0, state = ST_HELP; break;
    case A_EXIT: confirm_what = 2, menu_open(M_CONFIRM, 0); break;
    case A_CONTROL: save.control ^= 1, save_write(), menu_build(); break;
    case A_SFX: save.sfx = (save.sfx + 1) % 11, snd_master(save.sfx * 256 / 10), save_write(), menu_build(), play(S_PICK, 220); break;
    case A_SOUNDS: state = ST_SOUNDS; break;
    case A_CLEAR: confirm_what = 0, menu_open(M_CONFIRM, 0); break;
    case A_BACK:
        if (menu == M_OPTIONS && menu_from_pause) menu_open(M_PAUSE, 1);
        else menu_open(M_MAIN, menu == M_OPTIONS ? 2 : 0);
        break;
    case A_RESUME: state = ST_PLAY; break;
    case A_ABANDON: confirm_what = 1, menu_open(M_CONFIRM, 0); break;
    case A_NO:
        if (confirm_what == 0) menu_open(M_OPTIONS, 3);
        else if (confirm_what == 1) menu_open(M_PAUSE, 3);
        else menu_open(M_MAIN, 4);
        break;
    case A_YES:
        if (confirm_what == 0) {
            int c = save.control, s = save.sfx;
            memset(&save, 0, sizeof save);
            save.control = c, save.sfx = s;
            save_write();
            menu_open(M_OPTIONS, 3);
        } else if (confirm_what == 1) {
            record_game();
            to_menu(M_MAIN);
        } else {
            return 1;
        }
        break;
    }
    return 0;
}

static int in_pause_menus(void) { return menu == M_PAUSE || menu_from_pause; }

/* the menus' step; 1 to quit */
static int update_menu(void) {
    menu_turn(wheel_steps() + in.next - in.prev);
    if (in.tap && nitems) { /* a tap on an icon turns to it; on the chosen one, does it */
        int best = -1;
        float bd = 45 * 45;
        for (int i = 0; i < nitems; ++i) {
            float x, y, sc;
            int a;
            item_place(i, &x, &y, &sc, &a);
            float dx = in.tap_x - x, dy = in.tap_y - y;
            if (a && dx * dx + dy * dy < bd) bd = dx * dx + dy * dy, best = i;
        }
        if (best == sel) in.centre = 1;
        else if (best >= 0) {
            int d = best - sel;
            if (d > nitems / 2) d -= nitems;
            if (d < -nitems / 2) d += nitems;
            menu_turn(d);
        }
    }
    if (in.centre && nitems) {
        play(S_PICK, 220);
        return menu_act(items[sel].action);
    }
    if (in.back || in.play) {
        play(S_BACK, 200);
        if (menu == M_PAUSE) state = ST_PLAY;
        else if (menu == M_CONFIRM) menu_act(A_NO);
        else if (menu != M_MAIN) menu_act(A_BACK);
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------------
 * Drawing the screens */

static float clock_s;

/* the bar across the top, as the iPod's own: the time on the left, the battery on the right */
static int battery_pct(void) {
    static int pct = -1;
    static uint64_t at;
    uint64_t t = now_us();
    if (at && t - at < 20000000) return pct;
    at = t;
    static const char *paths[] = { "/sys/class/power_supply/battery/capacity", "/sys/class/power_supply/BAT0/capacity",
                                   "/sys/class/power_supply/bq27xxx-0/capacity" };
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        FILE *f = fopen(paths[i], "r");
        int v;
        if (f && fscanf(f, "%d", &v) == 1) {
            fclose(f);
            return pct = clampi(v, 0, 100);
        }
        if (f) fclose(f);
    }
    return pct = -1;
}

static void top_bar(void) {
    hud_bar(166, 0, 25, 0, 0);
    hud_bar(VIEW_W - 166, 0, 25, 1, 0);
    time_t now = time(0);
    struct tm tm;
    if (localtime_r(&now, &tm)) {
        char hm[16], b[24];
        int h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
        snprintf(hm, sizeof hm, "%d:%02d", h, tm.tm_min);
        fmt(b, sizeof b, tm.tm_hour < 12 ? 2 : 3, tm.tm_hour < 12 ? "%sam" : "%spm", hm);
        vtext_glow(b, 62, 7, 10, WHITE, 256, -1);
    }
    /* the battery: the iPod game's outline, filled to the charge */
    int bx = VIEW_W - 84, by = 6, pct = battery_pct();
    if (art.battery.im) spr_box(&art.battery, bx, by, 22, 13, 0, 256);
    else rect(bx, by, 22, 13, GREEN, 200);
    if (pct >= 0) rect(bx + 2, by + 2, 17 * pct / 100, 9, pct < 15 ? 0xff5040 : GREEN, 230);
}

static void draw_carousel(void) {
    car_pos += (car_target - car_pos) * 0.25f;
    if (fabs_(car_target - car_pos) < 0.002f) car_pos = (float)car_target;
    /* the far ones first, the chosen one last, over the rest */
    int order[8], n = 0;
    for (int i = 0; i < nitems; ++i) order[n++] = i;
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0; --j) {
            float xa, ya, sa, xb, yb, sb;
            int aa, ab;
            item_place(order[j - 1], &xa, &ya, &sa, &aa), item_place(order[j], &xb, &yb, &sb, &ab);
            if (sa > sb) {
                int t = order[j];
                order[j] = order[j - 1], order[j - 1] = t;
            }
        }
    for (int k = 0; k < n; ++k) {
        int i = order[k];
        float x, y, sc;
        int alpha;
        item_place(i, &x, &y, &sc, &alpha);
        if (!alpha) continue;
        if (sc > 1.2f) glow(x, y, 64, 0x40ff40, 60);
        const Spr *s = items[i].icon;
        if (s && s->im) spr_rot(s, x, y, 0, sc, alpha, 0xffffff);
        else disc(x, y, 28 * sc, 0x3a8a2a, alpha);
    }
    top_bar();
    vtext_outline(menu_title, CX, 40, 15, WHITE, 0x1f9a2a, 256, 0);
    if (nitems) vtext_outline(items[sel].label, CX, 262, 12, WHITE, 0x1f9a2a, 256, 0);
}

/* the paused game behind the pause menus: drawn and darkened once, when they open (the game is
 * still while they're up), then copied each frame as the main menu's picture is */
static Img paused_bg;
static int paused_ok;

static void draw_menu(void) {
    if (in_pause_menus() && menu != M_CONFIRM ? 1 : menu == M_CONFIRM && confirm_what == 1) {
        if (!paused_bg.px) paused_bg.px = malloc(VIEW_W * VIEW_H * 4), paused_bg.w = VIEW_W, paused_bg.h = VIEW_H;
        if (!paused_ok || !paused_bg.px) {
            tube_draw(1);
            hud_draw();
            darken(150);
            if (paused_bg.px) memcpy(paused_bg.px, fb, VIEW_W * VIEW_H * 4), paused_ok = 1;
        } else {
            blit(&paused_bg, 0, 0);
        }
    } else if (menu_bg.px) {
        blit(&menu_bg, 0, 0);
    } else {
        fill(0);
    }
    draw_carousel();
}

static void draw_splash(void) {
    if (splash_bg.px) blit(&splash_bg, 0, 0);
    else fill(0);
    if ((int)(clock_s * 1.6f) % 2 == 0) vtext_glow(str(900, "Press Select"), CX, 268, 12, WHITE, 256, 0);
}

static void draw_help(void) {
    static const int titles[6] = { 550, 552, 554, 556, 558, 559 }, bodies[4] = { 551, 553, 555, 557 };
    static const char *tfb[6] = { "Gameplay", "Shooting", "Control: Finger Position", "Control: Wheel Rotation", "Bonus Types", "Brick Types" };
    int p = help_page;
    if (help_pic[p].px) blit(&help_pic[p], 0, 0);
    else fill(0);
    if (p < 4) {
        darken(120);
        rect(20, 196, VIEW_W - 40, 92, 0x000000, 150);
        vtext_wrap(str(bodies[p], ""), CX, 208, 290, 10, WHITE, 256);
    } else {
        /* the labels under the iPod game's own pictures of the bonuses and bricks (its English
         * pictures carry none): where each one sits on the 320 x 240 picture */
        float s = (float)VIEW_W / 320, oy = (VIEW_H - 240 * s) / 2;
        if (p == 4) {
            static const struct { short x, y, id; } at[] = { { 40, 42, 409 }, { 120, 42, 410 }, { 40, 115, 408 }, { 120, 115, 405 },
                { 200, 115, 406 }, { 40, 188, 404 }, { 120, 188, 401 }, { 200, 188, 402 }, { 280, 188, 403 } };
            for (unsigned i = 0; i < sizeof at / sizeof at[0]; ++i)
                vtext_glow(str(at[i].id, "?"), at[i].x * s, oy + (at[i].y + 24) * s, 8, WHITE, 256, 0);
        } else {
            static const struct { short x, y, id; } at[] = { { 104, 44, 450 }, { 78, 112, 453 }, { 184, 112, 454 },
                { 52, 186, 451 }, { 158, 186, 452 }, { 264, 186, 455 } };
            for (unsigned i = 0; i < sizeof at / sizeof at[0]; ++i)
                vtext_glow(str(at[i].id, "?"), at[i].x * s, oy + (at[i].y + 16) * s, 9, WHITE, 256, 0);
        }
    }
    vtext_glow(str(titles[p], tfb[p]), CX, p < 4 ? 40 : 8, 14, GREEN, 256, 0);
    char b[16];
    snprintf(b, sizeof b, "%d/6", p + 1);
    vtext_glow(b, 338, 292, 9, 0xa0a0a0, 256, 1);
}

static void stat_row(int y, int id, const char *fb_, int best, int last) {
    char b[32];
    vtext_glow(str(id, fb_), CX, y, 11, GREEN, 256, 0);
    snprintf(b, sizeof b, "%s %d", str(520, "best"), best);
    vtext_glow(b, CX - 12, y + 18, 10, WHITE, 256, 1);
    snprintf(b, sizeof b, "%s %d", str(522, "last"), last);
    vtext_glow(b, CX + 12, y + 18, 10, WHITE, 256, -1);
}

static void draw_stats(void) {
    if (menu_bg.px) blit(&menu_bg, 0, 0);
    darken(120);
    vtext_outline(str(101, "PERSONAL INFO"), CX, 30, 15, WHITE, 0x1f9a2a, 256, 0);
    stat_row(66, 121, "SCORE", save.best_score, save.last_score);
    stat_row(114, 122, "LEVELS CLEARED", save.best_level, save.last_level);
    stat_row(162, 123, "BRICKS DESTROYED", save.bricks, save.last_bricks);
    stat_row(210, 124, "BONUSES RECEIVED", save.bonuses, save.last_bonuses);
    vtext_glow(str(900, "Press Select"), CX, 272, 10, 0xa0a0a0, 256, 0);
}

static void draw_sounds(void) {
    if (menu_bg.px) blit(&menu_bg, 0, 0);
    darken(190);
    vtext_outline("SOUND TEST", CX, 36, 15, WHITE, 0x1f9a2a, 256, 0);
    char b[48];
    snprintf(b, sizeof b, "%d", sound_pick);
    vtext_glow(b, CX, 110, 40, WHITE, 256, 0);
    const Sound *s = sfx(sound_pick);
    snprintf(b, sizeof b, "%.2f S", s ? (double)s->len / s->rate : 0.0);
    vtext_glow(b, CX, 162, 10, 0xa0a0a0, 256, 0);
    int y = 186;
    for (int c = 0; c < NCUES; ++c)
        if (cue[c] == sound_pick) {
            snprintf(b, sizeof b, "plays as: %s", cue_name[c]);
            vtext_glow(b, CX, y, 10, GREEN, 256, 0);
            y += 16;
        }
    vtext_glow("wheel: choose   select: play", CX, 262, 9, 0xa0a0a0, 256, 0);
}

static void draw_banner_text(void) {
    if (g.banner_t > 0 && state == ST_PLAY) {
        int a = g.banner_t > 1.2f ? (int)((1.6f - g.banner_t) / 0.4f * 256) : g.banner_t < 0.4f ? (int)(g.banner_t / 0.4f * 256) : 256;
        vtext_glow(g.banner, CX, 98, 18, 0xfff080, clampi(a, 0, 256), 0);
    }
}

static void draw_intro(void) {
    tube_draw(1);
    hud_draw();
    char b[48];
    snprintf(b, sizeof b, "%s %d", str(112, "LEVEL"), g.level);
    vtext_glow(b, CX, 92, 24, GREEN, 256, 0);
    if (g.level > 1 && g.level <= 41 && g.level % 10 == 1)
        vtext_wrap(str(300 + (g.level - 11) / 10, ""), CX, 132, 280, 9, WHITE, 256);
    if ((int)(clock_s * 1.6f) % 2 == 0) vtext_glow(str(900, "Press Select"), CX, 226, 11, WHITE, 256, 0);
}

static void draw_clear(void) {
    tube_draw(0);
    hud_draw();
    if (g.door_t < 1.2f) return;
    rect(40, 76, VIEW_W - 80, 170, 0x000000, 150);
    char b[48], n[16], t[16];
    snprintf(n, sizeof n, "%d", g.level);
    fmt(b, sizeof b, 113, "LEVEL %s CLEARED", n);
    vtext_glow(b, CX, 90, 17, GREEN, 256, 0);
    mmss(t, sizeof t, (int)g.clock);
    snprintf(b, sizeof b, "%s%s", str(601, "your time: "), t);
    vtext_glow(b, CX, 128, 11, WHITE, 256, 0);
    mmss(t, sizeof t, g.time_to_beat);
    snprintf(b, sizeof b, "%s%s", str(600, "time to beat: "), t);
    vtext_glow(b, CX, 150, 11, WHITE, 256, 0);
    snprintf(b, sizeof b, "%s%d", str(602, "bonus: "), (int)g.banner_t);
    vtext_glow(b, CX, 172, 11, 0xfff080, 256, 0);
    if (g.level < 64 && save.best_time[g.level]) {
        mmss(t, sizeof t, save.best_time[g.level]);
        snprintf(b, sizeof b, "%s%s", str(603, "best time: "), t);
        vtext_glow(b, CX, 194, 11, 0xa0c0ff, 256, 0);
    }
    if ((int)(clock_s * 1.6f) % 2 == 0) vtext_glow(str(900, "Press Select"), CX, 222, 10, WHITE, 256, 0);
}

static void draw_over(void) {
    tube_draw(0);
    darken(140);
    vtext_glow(str(114, "GAME OVER"), CX, 110, 26, 0xff6a50, 256, 0);
    char b[48];
    snprintf(b, sizeof b, "%s%d", str(500, "Final Score: "), g.score);
    vtext_glow(b, CX, 160, 13, WHITE, 256, 0);
    if (g.score >= save.best_score && g.score > 0) vtext_glow(str(520, "best"), CX, 186, 11, GREEN, 256, 0);
    if ((int)(clock_s * 1.6f) % 2 == 0) vtext_glow(str(900, "Press Select"), CX, 236, 10, WHITE, 256, 0);
}

/* ---------------------------------------------------------------------------------------------
 * The loop */

static void write_state(void) {
    FILE *f = fopen("state.tmp", "w");
    if (!f) return;
    int nb = 0, nc = 0;
    float bz = 0;
    for (int i = 0; i < MAX_BALLS; ++i)
        if (g.ball[i].alive) ++nb, bz = g.ball[i].z;
    for (int i = 0; i < MAX_CAPS; ++i) nc += g.cap[i].alive;
    fprintf(f, "state %d\nmenu %d\nsel %d\nlevel %d\nscore %d\nlives %d\nbricks %d\nballs %d\ncaps %d\nbat %d\n"
               "ballz %d\nbroken %d\nbonuses %d\ncontrol %d\nsfx %d\nhelp %d\n",
            state, menu, sel, g.level, g.score, g.lives, g.bricks_left, nb, nc, (int)(g.bat_a * 1000), (int)(bz * 1000),
            g.bricks_broken, g.bonuses_got, save.control, save.sfx, help_page);
    fclose(f);
    rename("state.tmp", "state.txt");
}

int game_main(void) {
    save_read();
    if (!load_all()) {
        while (!quit_asked()) sleep_us(50000);
        return 0;
    }
    snd_master(save.sfx * 256 / 10);
    autopilot = getenv("VX_AUTOPILOT") != 0;
    if (getenv("VX_LEVEL")) { /* host tests: straight into a level */
        game_new(atoi(getenv("VX_LEVEL")));
        state = ST_INTRO;
    }
    if (getenv("VX_GIVE") && state == ST_INTRO) bonus_take(atoi(getenv("VX_GIVE")) % NBONUS, 0); /* host tests: a bonus to try */
    uint64_t last = now_us(), stat_at = last, draw_us = 0;
    float acc = 0;
    int frames = 0, quit = 0;
    while (!quit_asked() && !quit) {
        read_input();
        uint64_t now = now_us();
        float dt = (float)(unsigned)(now - last) / 1e6f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        clock_s += dt;
        switch (state) {
        case ST_SPLASH:
            if (in.centre || in.tap || in.play || clock_s > 30) to_menu(M_MAIN), play(S_PICK, 220);
            break;
        case ST_MENU: quit = update_menu(); break;
        case ST_HELP: {
            int s = wheel_steps() + in.next - in.prev + (in.tap ? 1 : 0);
            if (s) help_page = (help_page + s % 6 + 6) % 6, play(S_MOVE, 160);
            if (in.centre || in.back || in.play) state = ST_MENU, play(S_BACK, 200);
            break;
        }
        case ST_STATS:
            if (in.centre || in.back || in.play || in.tap) state = ST_MENU, play(S_BACK, 200);
            break;
        case ST_SOUNDS: {
            int s = wheel_steps() + in.next - in.prev, n = sfx_count() ? sfx_count() : 1;
            if (s) sound_pick = (sound_pick + s % n + n) % n;
            if (in.centre || in.tap) snd_stop_all(), snd_play(sfx(sound_pick), 230, 0, 0);
            if (in.back || in.play) state = ST_MENU, snd_stop_all();
            break;
        }
        case ST_INTRO:
            move_bat(dt);
            for (int i = 0; i < MAX_BALLS; ++i)
                if (g.ball[i].stuck) g.ball[i].a = g.bat_a + g.ball[i].stuck_off;
            if (in.back || in.play) state = ST_PAUSE, menu_from_pause = 0, menu_open(M_PAUSE, 0), state = ST_MENU;
            else if (in.centre || in.tap || autopilot) {
                state = ST_PLAY;
                launch(&g.ball[0]);
            }
            break;
        case ST_PLAY:
            if (in.play || in.back) {
                menu_from_pause = 0;
                menu_open(M_PAUSE, 0);
                state = ST_MENU;
                break;
            }
            acc += dt;
            while (acc >= STEP && state == ST_PLAY) {
                step_play(STEP);
                presses_used(); /* the first step takes them */
                acc -= STEP;
            }
            if (state != ST_PLAY) acc = 0;
            break;
        case ST_LOST:
            g.lost_t -= dt;
            for (int i = 0; i < MAX_FX; ++i)
                if (g.fx[i].dur > 0 && (g.fx[i].t += dt) >= g.fx[i].dur) g.fx[i].dur = 0;
            if (g.lost_t <= 0) {
                if (--g.lives <= 0) {
                    g.lives = 0;
                    record_game();
                    play(S_OVER, 230);
                    state = ST_OVER;
                } else {
                    ball_reset();
                    state = ST_INTRO;
                }
            }
            break;
        case ST_CLEAR:
            g.door_t += dt;
            g.door_frame = clampi(15 - (int)(g.door_t * 16), 0, 15);
            for (int i = 0; i < MAX_FX; ++i)
                if (g.fx[i].dur > 0 && (g.fx[i].t += dt) >= g.fx[i].dur) g.fx[i].dur = 0;
            if (g.door_t > 1.2f && (in.centre || in.tap || (autopilot && g.door_t > 2.0f))) {
                save.last_level = g.level;
                if (g.level > save.best_level) save.best_level = g.level;
                save_write();
                level_intro(g.level + 1);
            }
            break;
        case ST_OVER:
            if (in.centre || in.tap || in.back || in.play) to_menu(M_MAIN);
            break;
        }
        presses_used();
        if (state != ST_MENU && state != ST_HELP && state != ST_SOUNDS) paused_ok = 0;

        /* the still screens (menus, help, info) are drawn and sent only when something on them changed:
         * most of their frames are the same as the last, and drawing them isn't free on the Q2 */
        char key[96] = "";
        if (state == ST_MENU || state == ST_HELP || state == ST_STATS || state == ST_SPLASH) {
            time_t tnow = time(0);
            snprintf(key, sizeof key, "%d %d %d %d %d %.4f %ld %d %d %d", state, menu, sel, menu_gen, help_page, (double)car_pos,
                     (long)(tnow / 60), battery_pct(), paused_ok, (int)(clock_s * 1.6f) % 2);
        }
        static char last_key[96];
        if (key[0] && !strcmp(key, last_key)) {
            ++frames; /* the screen still shows it: a frame all the same */
            uint64_t spent = now_us() - now;
            if (spent < 16000) sleep_us((unsigned)(16000 - spent));
            if (getenv("VX_STATE")) write_state();
            continue;
        }
        snprintf(last_key, sizeof last_key, "%s", key);

        uint64_t t0 = now_us();
        switch (state) {
        case ST_SPLASH: draw_splash(); break;
        case ST_MENU: draw_menu(); break;
        case ST_HELP: draw_help(); break;
        case ST_STATS: draw_stats(); break;
        case ST_SOUNDS: draw_sounds(); break;
        case ST_INTRO: draw_intro(); break;
        case ST_PLAY:
        case ST_LOST:
            tube_draw(1);
            hud_draw();
            draw_banner_text();
            break;
        case ST_CLEAR: draw_clear(); break;
        case ST_OVER: draw_over(); break;
        }
        present(fb);
        draw_us += now_us() - t0;
        ++frames;
        if (now - stat_at > 10000000) {
            logf_("%.1f fps, %.1f ms a frame drawing; state %d, level %d, score %d", frames * 1e6 / (unsigned)(now - stat_at),
                  (unsigned)draw_us / 1000.0 / frames, state, g.level, g.score);
            stat_at = now, frames = 0, draw_us = 0;
        }
        uint64_t spent = now_us() - now;
        if (spent < 16000) sleep_us((unsigned)(16000 - spent));
        if (getenv("VX_STATE")) write_state();
    }
    save_write();
    return 0;
}
