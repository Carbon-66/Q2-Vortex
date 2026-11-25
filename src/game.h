/* The game's state, shared by game.c (rules, menus, the loop) and tube.c (drawing the tube). */
#ifndef GAME_H
#define GAME_H
#include "vx.h"

/* The tube. The wall is radius 1; depth z runs from the bat's rim (0) to the far end (ZFAR). A point
 * at radius rad and depth z is drawn K * rad / (z + CAM) px from the axis. Angles are radians,
 * anticlockwise from the view's right. */
#define K 140.0f
#define CAM 1.0f
#define ZFAR 2.5f
#define R_BALL 0.90f /* the ball runs round the wall at this radius, among the bricks */
#define R_IN 0.79f   /* the bricks' inner face */
#define R_BAT 0.95f
#define BALL_R 0.065f
#define BRICK_D 0.27f
#define SEG_W (TAU / SEGS)
#define RING_Z(k) (1.75f - (k) * 0.34f) /* ring 0 is the farthest */
#define RING_STEP 0.34f

static inline float px_r(float rad, float z) { return K * rad / (z + CAM); }

enum { B_NONE, B_ONE, B_TWO, B_THREE, B_BONUS, B_SOLID, B_BOMB };
enum { BN_LIFE, BN_MULTIBALL, BN_SHRINK, BN_STRETCH, BN_MULTIBAT, BN_STICKY, BN_GUNS, BN_LASER, BN_POWER, BN_SLOWER, BN_FASTER, NBONUS };

typedef struct {
    uint8_t type, hits, max_hits, colour, bonus;
    float dz;    /* from the ring's depth */
    float flash; /* a hit's light, s */
    float fuse;  /* > 0: blows up when it runs out (a neighbour exploded) */
} Brick;

typedef struct {
    int alive, stuck;
    float a, z, u, vz; /* u: speed round the wall (at R_BALL), vz: along the tube */
    float stuck_off;
} Ball;

typedef struct {
    int alive, kind;
    float a, z, t;
} Cap;

typedef struct {
    int alive;
    float a, z;
} Shot;

enum { FX_DEBRIS, FX_BOOM, FX_TEXT, FX_SPARK };
typedef struct {
    int kind;
    float a, z, t, dur;
    uint32_t colour;
    char text[24];
} Fx;

#define MAX_BALLS 8
#define MAX_CAPS 8
#define MAX_SHOTS 24
#define MAX_FX 96

typedef struct {
    Brick brick[RINGS][SEGS];
    float ring_a[RINGS], ring_spin[RINGS];
    Ball ball[MAX_BALLS];
    Cap cap[MAX_CAPS];
    Shot shot[MAX_SHOTS];
    Fx fx[MAX_FX];
    float bat_a, bat_target;
    int bat_size; /* 0 small, 1 normal, 2 large */
    float sticky_t, guns_t, laser_t, power_t, multibat_t;
    float fire_cool, laser_beam, laser_len, laser_a;
    float speed, speed_base, speed_max, speed_mul;
    int level, lives, score;
    float clock;  /* the level's time, s */
    int time_to_beat;
    int bricks_left, bricks_broken, bonuses_got;
    float lost_t, door_t, banner_t, shake;
    int door_frame;
    char banner[40];
    int theme;
} Game;

extern Game g;
float bat_half(void);
int brick_live(const Brick *b);

/* tube.c */
typedef struct {
    Img bg;              /* the level's background, covering the view, its centre dimmed */
    Img door;            /* the far end's 16 frames */
    Spr brick_plain, brick_bonus[6], brick_bomb[5], brick_crack[6];
    Spr ball, fireball[9], bat[3][2], gun[12], icon[12], debris[16], boom[8], heart, clock, ring, bar, battery;
} Art;
extern Art art;
void tube_draw(int show_balls);
void hud_draw(void);
void hud_bar(int x, int y, int h, int right, int upside);
uint32_t brick_colour(int c);
#endif
