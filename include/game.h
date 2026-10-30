#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#include "sound.h"

/*
 * The game world is a virtual 320x240 square-pixel playfield (the Lovable
 * 800x600 canvas scaled by 0.4).  game_render() maps it onto the playing field
 * rectangle of the real screen, so the world keeps its proportions on a 4:3 display.
 *
 * Positions and velocities are 16.16 fixed point in world units per 50 Hz
 * frame.  Angles are 16-bit: 65536 is one full turn, 0 points right, and
 * increasing angles turn clockwise on screen (y grows downwards).
 */
#define GAME_MAX_ASTEROIDS 27   /* as many as the arcade original allowed */
#define GAME_MAX_BULLETS 12
#define GAME_MAX_ASTEROID_POINTS 11
/* Rocks share a small set of outlines per size (as in the arcade original), so a drawer can prepare each one
   ahead of time. The rock turns through 1 << GAME_ROCK_ORIENT_BITS steps of the 64 the game knows; 0 means it
   keeps one orientation. */
#define GAME_ROCK_SHAPES 8
#define GAME_ROCK_SHAPE_BITS 3
#define GAME_ROCK_ORIENT_BITS 0
#define GAME_ROCK_ORIENT_MASK (0x3f & ~((1 << (6 - GAME_ROCK_ORIENT_BITS)) - 1))
#define GAME_DRAW_CACHE_WORDS (3 + 6 * GAME_MAX_ASTEROID_POINTS)   /* room the platform may use per rock */
#define GAME_MAX_POWERUPS 4
#define GAME_MAX_ENEMIES 12
#define GAME_MAX_ENEMY_BULLETS 24
#define GAME_STAR_COUNT 18
#define GAME_FIX_SHIFT 16
#define GAME_FIX_ONE (1L << GAME_FIX_SHIFT)
#define GAME_WORLD_WIDTH 320
#define GAME_WORLD_HEIGHT 240

#define GAME_HIGH_SCORE_COUNT 5
#define GAME_MIN_SCORE_FOR_INITIALS 1000
#define GAME_EXTRA_LIFE_INTERVAL 10000
#define GAME_START_LIVES 3
#define GAME_HYPERSPACE_RECHARGE_FRAMES 500   /* 10 s at 50 Hz */

/* High score file: "AST1" + 5 entries of (3 initials, 0, score as 4 bytes big endian). */
#define GAME_SCORE_FILE_BYTES 44

/* Palette indices. Line colours (ship, rocks) each sit on a single bitplane
   (1, 2, 4, 8), which the assembly line drawer can plot quickly. */
#define GAME_COLOR_BLACK 0
#define GAME_COLOR_SHIP 1
#define GAME_COLOR_ASTEROID_LARGE 2
#define GAME_COLOR_FLAME 3
#define GAME_COLOR_ASTEROID_MEDIUM 4
#define GAME_COLOR_FRAME 5
#define GAME_COLOR_YELLOW 6
#define GAME_COLOR_RED 7
#define GAME_COLOR_ASTEROID_SMALL 8
#define GAME_COLOR_GREY 9
#define GAME_COLOR_MAGENTA 10
#define GAME_COLOR_STAR_DIM 11
#define GAME_COLOR_CYAN 12
#define GAME_COLOR_BROWN 13
#define GAME_COLOR_PURPLE 14
#define GAME_COLOR_STAR_BRIGHT 15
#define GAME_COLOR_WHITE GAME_COLOR_ASTEROID_LARGE

#define GAME_ASTEROID_SMALL 1
#define GAME_ASTEROID_MEDIUM 2
#define GAME_ASTEROID_LARGE 3

/* Screens. */
enum {
    GAME_MODE_TITLE = 0,
    GAME_MODE_PLAYING,
    GAME_MODE_GAME_OVER,
    GAME_MODE_ENTER_INITIALS
};

typedef struct GameInput {
    uint8_t left;
    uint8_t right;
    uint8_t thrust;
    uint8_t fire;
    uint8_t hyperspace;
    uint8_t start;          /* Space or Return: start a game, confirm */
    uint8_t pause;
    uint8_t escape;         /* Esc: pauses; on a pause screen it goes back to the menu, on the menu it quits */
    uint8_t down;           /* menu: down (thrust is up) */
    uint8_t space;          /* the space bar itself, which can be fire or hyperspace (see hyper_on_space) */
    uint8_t fire_alt;       /* fire that is not the space bar: joystick button, Ctrl, Alt */
    uint8_t typed;          /* a letter typed on the keyboard since the last step ('A' - 'Z'), else 0; one step only */
    uint8_t backspace;      /* Backspace pressed since the last step; one step only */
} __attribute__((aligned(2))) GameInput;   /* all-byte struct: without this it may sit at an odd address, and gcc copies it with move.l (address error on a 68000) */

typedef struct GameShip {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint16_t angle;
    uint16_t hyperspace_cooldown;
    uint8_t cooldown;
    uint8_t invulnerability;
    uint8_t thrusting;
} GameShip;

typedef struct GameBullet {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint8_t active;
    uint8_t life;
    uint8_t color;      /* enemy bullets only */
} GameBullet;

typedef struct GameAsteroid {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint16_t angle;
    int16_t spin;
    uint8_t active;
    uint8_t size;
    uint8_t point_count;
    uint8_t radius[GAME_MAX_ASTEROID_POINTS];
    /* screen-space vertex offsets for the cached orientation (render cache) */
    int8_t off_x[GAME_MAX_ASTEROID_POINTS];
    int8_t off_y[GAME_MAX_ASTEROID_POINTS];
    int8_t bound_x0;    /* tight bounding box of the cached outline, relative to the centre */
    int8_t bound_y0;
    int8_t bound_x1;
    int8_t bound_y1;
    uint8_t cache_index;
    uint8_t cache_valid;
    uint8_t draw_count;  /* vertices of the cached outline (small rocks are drawn with fewer) */
    uint8_t shape;       /* 1 + the index of the shared outline it has (see game_rock_shape), 0 = its own outline */
    uint8_t render_pending;                         /* set by a rocks drawer for rocks it left to game_render */
    uint8_t draw_cache_valid;                       /* the platform's own prepared form of the outline, below */
    uint16_t draw_cache[GAME_DRAW_CACHE_WORDS];
} GameAsteroid;

enum {
    GAME_POWERUP_SHIELD = 0,
    GAME_POWERUP_RAPID_FIRE,
    GAME_POWERUP_EXTRA_LIFE,
    GAME_POWERUP_MULTIPLIER,
    GAME_POWERUP_TYPES
};

typedef struct GamePowerUp {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint16_t life;
    uint8_t active;
    uint8_t type;
} GamePowerUp;

/* A background star: drifts slowly left. Its screen pixel lives in GameState.star_points and is only
   recomputed when it changes; stale_ remembers the pixel it just left, which still has to be erased
   from both screen buffers. Stars 0-5 are the far layer, 6-11 the middle, 12-17 the near one. */
typedef struct GameStar {
    int32_t x;             /* world x, 16.16 (y never changes) */
    int16_t key;           /* x in 1/8 pixel steps: the screen position is only worked out when this changes */
    int16_t stale_x;
    int16_t stale_y;
    uint8_t layer;
    uint8_t stale_frames;
    uint8_t fresh_frames;  /* frames the new pixel still has to be drawn every frame (once per screen buffer) */
} GameStar;

enum {
    GAME_ENEMY_NONE = 0,
    GAME_ENEMY_UFO_LARGE,
    GAME_ENEMY_UFO_SMALL,
    GAME_ENEMY_BROWN,
    GAME_ENEMY_GREEN,
    GAME_ENEMY_BLUE,
    GAME_ENEMY_PURPLE
};

enum {
    GAME_BOSS_NONE = 0,
    GAME_BOSS_AMIGA_BALL,
    GAME_BOSS_FLYING_SAUCER,
    GAME_BOSS_TENTACLE,
    GAME_BOSS_BORG_CUBE
};

/* UFOs and the four kinds of alien. timer / timer2 / burst / flags are behaviour state that means
   something different for each kind (flags: 1 = invisible, 2 = charging a teleport, 4 = leaving for good). */
typedef struct GameEnemy {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint16_t phase;
    uint16_t age;
    int16_t timer;
    int16_t timer2;
    int16_t warn;       /* frames of entry warning left: the enemy waits at the edge, harmless, until it reaches 0 */
    int16_t charge;     /* blue sentinel: frames of teleport charge-up left (0 = not charging) */
    int16_t tp_x;       /* blue sentinel: where it will reappear (world pixels) */
    int16_t tp_y;
    int16_t invis;      /* blue sentinel: frames of invisibility left */
    uint8_t active;
    uint8_t kind;
    uint8_t hp;
    uint8_t burst;
    uint8_t flock;
    uint8_t flags;
} GameEnemy;

typedef struct GameBoss {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    int32_t target_y;
    int16_t hp;
    int16_t max_hp;
    uint16_t phase;
    uint16_t angle;
    int16_t timer;
    int16_t timer2;
    int16_t dash_timer;  /* Boing ball: frames until its next dash (last stage only) */
    int16_t dash_ticks;  /* Boing ball: frames of hovering left before it drops */
    uint8_t active;
    uint8_t kind;
    uint8_t entered;
    uint8_t flash;
    uint8_t stage;       /* Boing ball: 0, 1 or 2 as it loses hit points */
    uint8_t dash_state;  /* Boing ball: 0 bouncing, 1 hovering over the ship, 2 dropping */
    uint8_t parity;      /* Boing ball: alternates the offset of its ring of bullets */
    uint8_t stage_flash; /* Boing ball: frames it shows it has changed stage */
} GameBoss;

typedef struct GameHighScore {
    uint32_t score;
    char initials[4];
} GameHighScore;

typedef struct GameState {
    /* the playing field rectangle on the screen, and the world -> screen scales (8.8 fixed point) */
    uint16_t field_x;
    uint16_t field_y;
    uint16_t field_width;
    uint16_t field_height;
    uint16_t x_scale;
    uint16_t y_scale;

    uint8_t mode;
    uint16_t mode_timer;
    uint8_t paused;
    uint8_t quit_requested;     /* set when Esc is pressed on the main menu: the program should exit */
    uint8_t hyper_on_space;     /* setting: hyperspace on the space bar instead of H (fire is then the joystick button) */
    uint8_t settings_changed;   /* set when a setting was changed and should be saved */
    uint8_t menu_item;          /* main menu selection: 0 start, 1 the hyperspace key setting */
    uint8_t ignore_keys;        /* fire and hyperspace stay off until the keys that started/resumed the game are let go */
    uint16_t banner_timer;      /* frames left of the "WAVE n" banner */
    uint32_t score;
    uint32_t next_extra_life;
    uint16_t frame;
    uint8_t wave;
    uint8_t lives;
    uint8_t extra_life_count;   /* extra lives earned by score so far (the thresholds get further apart) */
    uint8_t life_lost_this_wave;
    uint8_t life_drops_this_wave;
    uint8_t alien_budget;       /* aliens still to arrive this wave */
    uint8_t wave_clear_timer;   /* frames of the "wave clear" pause left */
    uint16_t clear_bonus;       /* bonus shown during that pause */
    uint16_t drop_cooldown;     /* frames before the next power-up may drop */
    uint16_t pity_timer;        /* frames since the last drop: long enough guarantees the next one */
    uint16_t life_gap_timer;    /* frames since the last extra-life drop */

    GameShip ship;
    GameBullet bullets[GAME_MAX_BULLETS];
    GameAsteroid asteroids[GAME_MAX_ASTEROIDS];
    GamePowerUp powerups[GAME_MAX_POWERUPS];
    GameEnemy enemies[GAME_MAX_ENEMIES];
    GameBullet enemy_bullets[GAME_MAX_ENEMY_BULLETS];
    GameBoss boss;
    int16_t ufo_timer;          /* frames until the next UFO / alien is due */
    int16_t alien_timer;
    uint8_t ufo_present;        /* a UFO or the flying saucer is on screen (for its warble) */
    GameStar stars[GAME_STAR_COUNT];
    int16_t star_points[GAME_STAR_COUNT * 2];   /* screen x,y of every star, grouped by layer */
    uint16_t shield_timer;      /* frames left of each active power-up */
    uint16_t rapid_timer;
    uint16_t multiplier_timer;
    uint32_t rng_state;

    GameHighScore high_scores[GAME_HIGH_SCORE_COUNT];
    char entry[4];              /* initials being entered */
    uint8_t entry_position;
    uint8_t repeat_timer;
    uint8_t scores_changed;     /* set when the high score table changed and should be saved */
    uint16_t sound_events;      /* bit per SFX_* id: effects to play (taken by game_take_sound_events) */
    GameInput previous;         /* last step's input, for detecting key presses */

    /* what is currently drawn on the (double-buffered) screen, so it is only redrawn on change */
    uint8_t screen_refresh;     /* frames of the full static screen (title, game over ...) still to draw */
    uint8_t prompt_refresh;
    uint8_t prompt_visible;
    uint8_t hud_refresh;        /* frames of the whole HUD still to draw */
    uint8_t hud_field[8];       /* frames each HUD field still has to be drawn after a change */
    uint8_t hud_hyper_full;     /* frames the whole hyperspace field (label included) still has to be drawn */
    uint32_t hud_score;
    uint32_t hud_high;
    uint8_t hud_lives;
    uint8_t hud_wave;
    uint8_t hud_hyperspace;     /* percent charged */
    uint8_t hud_powers[3];      /* seconds left of each power-up */
} GameState;

typedef void (*GameLineDrawer)(void *context, int x0, int y0, int x1, int y1, uint8_t color);

/* Optional: draws a closed outline in one call. points holds x,y pairs (screen space, may lie
   off-screen). When absent, game_render() draws the outline with the line drawer. */
typedef void (*GamePolygonDrawer)(void *context, const int16_t *points, int count, uint8_t color);

/* Optional, a faster route for the rocks: a closed outline given as a centre and signed byte offsets, which
   the game only uses when the whole outline lies inside the playing field. cache is scratch space owned by
   the rock, for whatever the drawer can prepare once per outline; the game clears *cache_valid whenever the
   outline changes and the drawer sets it after filling the cache. */
typedef void (*GameOffsetPolygonDrawer)(void *context, int center_x, int center_y, const int8_t *off_x,
                                        const int8_t *off_y, int count, uint8_t color, void *cache,
                                        uint8_t *cache_valid);

/* Optional, the fast route for all the rocks at once (the Atari build does this in assembly): draws every rock
   whose outline lies inside the playing field, reports its dirty rectangle, and leaves the others alone with
   render_pending set, so game_render draws those the ordinary way. It may call game_prepare_rock() to bring a
   rock's cached outline up to date. */
typedef void (*GameRocksDrawer)(void *context, GameState *state);

/* Optional: called with the screen-space bounding box of everything game_render() draws
   on the playing field, so the platform can erase just those areas next time. */
typedef void (*GameDirtyMarker)(void *context, int x0, int y0, int x1, int y1);

/* Optional: opaque bitmap text. x is a multiple of 8 (16 at scale 2), scale is 1 or 2. */
typedef void (*GameTextDrawer)(void *context, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t scale);

/* Optional: single pixels of one colour (the stars). */
typedef void (*GamePointDrawer)(void *context, const int16_t *points, int count, uint8_t color);

/* Optional: clear the whole playing field (for the static screens). */
typedef void (*GameFieldClearer)(void *context);

typedef struct GameRenderer {
    void *context;
    GameLineDrawer line;
    GamePolygonDrawer polygon;
    GameDirtyMarker dirty;
    GameTextDrawer text;
    GameFieldClearer clear_field;
    GamePointDrawer points;
    GameOffsetPolygonDrawer polygon_offsets;
    GameRocksDrawer rocks;
    uint8_t low_detail;   /* nonzero when the machine is struggling: leave out what can be left out (the stars) */
} GameRenderer;

/* The field rectangle is where the world is drawn on the screen. The game starts on the title screen. */
void game_init(GameState *state, uint16_t field_x, uint16_t field_y, uint16_t field_width, uint16_t field_height);
void game_start(GameState *state);   /* begin a new game right away (what pressing start does) */
void game_step(GameState *state, const GameInput *input);
void game_render(GameState *state, const GameRenderer *renderer);

/* Reference C versions of loops that the Atari build runs in assembly (st_step.S); the tests compare the two. */
void game_update_rocks_ref(GameState *state);
void game_update_bullets_ref(GameState *state);
int game_find_bullet_hit_ref(const GameState *state, int first_bullet, int *rock);
void game_nudge_rock(const GameState *state, GameAsteroid *asteroid);   /* called back from st_step.S */
#ifdef ATARI_ST_TARGET
void st_update_rocks(GameState *state);
void st_update_bullets(GameState *state);
/* reach: per rock size the largest 12.4 distance for a hit; returns the bullet index or -1, and the rock in *rock */
long st_find_bullet_hit(const GameState *state, long first_bullet, const uint16_t *reach, long *rock);
#endif

/* For tests and tools: bring an enemy (GAME_ENEMY_*) in at once, without the entry warning. */
void game_spawn_enemy_now(GameState *state, int kind);

/* The shared outline `shape` (0 .. GAME_ROCK_SHAPES - 1) of a rock size: fills size, shape, point_count and radius. */
void game_rock_shape(int size, int shape, GameAsteroid *out);

/* For renderers that draw rocks themselves: rebuild the rock's cached outline if its orientation changed. */
void game_prepare_rock(const GameState *state, GameAsteroid *asteroid);

/* Sound effects triggered since the last call, as a bit mask of 1 << SFX_*. */
uint16_t game_take_sound_events(GameState *state);

/* High score table <-> file image. unpack returns 1 if the data was valid. */
void game_scores_pack(const GameState *state, uint8_t *out);
int game_scores_unpack(GameState *state, const uint8_t *data);

#endif
