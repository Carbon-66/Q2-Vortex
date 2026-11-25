/* Vortex for the Shanling Q2 under Q2 Pod: Breakout down a tube.
 *
 * The game is its own code. Its art, level layouts, words and sound are read at run time from
 * the player's own copy of the 2006 click-wheel iPod game (the .ipg file put in the card folder):
 * nothing from it is in this source or in the card folder as shipped.
 *
 * Shared declarations for every file in src/. */
#ifndef VX_H
#define VX_H
#include <stdint.h>

#define VIEW_W 375 /* the UI's landscape view: the portrait panel turned */
#define VIEW_H 320
#define CX 187 /* the tube's axis on the view */
#define CY 160

/* Q2 Pod's AWTK key codes */
#define K_RETURN 170
#define K_PLAY 171
#define K_CENTRE 218
#define K_PREV 222
#define K_NEXT 223

#define PI 3.14159265f
#define TAU 6.28318531f

/* ---------------------------------------------------------------------------------------------
 * q2.c: the platform */
uint64_t now_us(void);
void sleep_us(unsigned us);
void present(const uint32_t *frame); /* a VIEW_W x VIEW_H XRGB frame onto the panel */
int quit_asked(void);
int next_event(int *kind, int *a, int *b, int *c); /* 'k' (a 1 down/0 up, b code), 't' (a 'd'/'m'/'u', b x, c y), 'w' (a travel) */
void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void message(const char *title, const char *body); /* a full-screen note in the platform's 5 x 7 font */

/* ---------------------------------------------------------------------------------------------
 * sound.c: 16-bit mono sounds mixed onto Q2 Pod's output */
typedef struct {
    const int16_t *data;
    unsigned len, rate;
} Sound;
void snd_start(const char *device, const char *volume);
void snd_shutdown(void);
int snd_play(const Sound *s, int vol, int pan, int loop); /* vol 0-256, pan -256..256; a channel or -1 */
void snd_stop(int ch);
void snd_stop_all(void);
void snd_master(int vol); /* 0-256, the game's own sfx level */

/* ---------------------------------------------------------------------------------------------
 * zip.c: the .ipg (a zip) */
typedef struct Zip Zip;
Zip *zip_open(const char *path);
void zip_close(Zip *z);
uint8_t *zip_read(Zip *z, const char *name, unsigned *size); /* malloc'd, 0 if missing or bad */
int zip_has(Zip *z, const char *name);
int inflate_raw(const uint8_t *in, unsigned inlen, uint8_t *out, unsigned outlen); /* 0 ok */

/* ---------------------------------------------------------------------------------------------
 * assets.c: the iPod game's files */
typedef struct {
    int w, h;
    uint32_t *px; /* ARGB, alpha in the top byte */
} Img;

typedef struct {
    const Img *im;
    short x, y, w, h; /* the frame in the atlas */
} Spr;

void img_free(Img *i);
Img img_ipd(Zip *z, const char *member);   /* a whole picture: 16-byte header (w, h, format), then pixels */
Img img_lcd5(Zip *z, const char *member);  /* the launch picture: a 16-byte header, then RGB565 */
Img img_cover(const Img *src);             /* scaled to cover the view, centred (4:3 -> 375 x 320) */
int tex_load(Zip *z, const char *member);  /* the texture bank, "tex": its entries by number */
const Img *tex(int i);
int tex_count(void);
Spr spr_cell(const Img *im, int cols, int rows, int i);
Spr spr_whole(const Img *im);
Spr spr_cell_up(const Img *im, int cols, int rows, int i); /* a cell numbered as the bank stores the sheet */

int sfx_load(Zip *z, const char *member); /* the sound bank: unnamed WAVs, by number */
const Sound *sfx(int i);
int sfx_count(void);

int strings_load(Zip *z, const char *member); /* text.strings: "id"="text" */
const char *str(int id, const char *fallback);

#define RINGS 5
#define SEGS 16
typedef struct {
    uint16_t brick[RINGS][SEGS]; /* 0 none; else type << 13 | offset flag 0x800 | offset 6 bits << 5 | colour or bonus */
    short spin[RINGS];           /* each ring's turning speed */
    int time_to_beat, speed, speed_max; /* seconds; the ball's speed range */
} Level;
int levels_load(Zip *z, const char *member);
int level_count(void);
const Level *level(int i); /* 0-based */

/* ---------------------------------------------------------------------------------------------
 * gfx.c: drawing into the frame */
extern uint32_t *fb; /* VIEW_W x VIEW_H */
void fill(uint32_t c);
void blit(const Img *i, int x, int y);                     /* opaque, whole */
void rect(int x, int y, int w, int h, uint32_t c, int alpha); /* alpha 0-256 */
void spr(const Spr *s, int x, int y, int alpha);           /* top left at x, y */
void spr_c(const Spr *s, int cx, int cy, int scale, int alpha); /* centred, scale 256 = 1:1 */
void spr_box(const Spr *s, int x, int y, int w, int h, int flip, int alpha); /* stretched into a box; flip 1 mirrored, 2 upside down */
void spr_rot(const Spr *s, float cx, float cy, float ang, float scale, int alpha, uint32_t tint); /* centred, turned ang (anticlockwise) */
void spr_add(const Spr *s, int cx, int cy, int scale, int alpha); /* added (light), centred */
void disc(float cx, float cy, float r, uint32_t c, int alpha);
void glow(float cx, float cy, float r, uint32_t c, int alpha); /* a soft added spot */
void line(float x0, float y0, float x1, float y1, float w, uint32_t c, int alpha);
void darken(int alpha);
/* the vector font: h px tall capitals; align -1 left, 0 centre, 1 right; returns the width */
int vtext(const char *s, float x, float y, float h, uint32_t c, int alpha, int align);
int vtext_w(const char *s, float h);
void vtext_glow(const char *s, float x, float y, float h, uint32_t c, int alpha, int align);
void vtext_outline(const char *s, float x, float y, float h, uint32_t fill, uint32_t edge, int alpha, int align);
int vtext_wrap(const char *s, float x, float y, float w, float h, uint32_t c, int alpha); /* centred lines; returns the height */

/* the view in polar terms, from the tube's axis: each pixel's distance (1/16 px) and angle (65536 a turn,
 * anticlockwise from east) */
extern uint16_t *polar_r, *polar_a;
void polar_init(void);

/* ---------------------------------------------------------------------------------------------
 * maths */
float fsin(float a);
float fcos(float a);
float fatan2(float y, float x);
float wrap_pi(float a); /* into -PI..PI */
float wrap_tau(float a); /* into 0..TAU */
static inline float fsqrt(float x) { return __builtin_sqrtf(x); }
static inline float fabs_(float x) { return x < 0 ? -x : x; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
unsigned rnd(void);
float rndf(void); /* 0..1 */
void rnd_seed(unsigned s);

int game_main(void);
#endif
