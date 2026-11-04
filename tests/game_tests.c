#include <stdlib.h>
#include "game.h"

#include <stdio.h>
#include <string.h>

/* The real playing-field layout (PLATFORM_FIELD_* in platform.h). */
#define FIELD_X 16
#define FIELD_Y 16
#define FIELD_W 288
#define FIELD_H 176

static int failures;
static int checks;

#define CHECK(condition) \
    do { \
        ++checks; \
        if (!(condition)) { \
            ++failures; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        } \
    } while (0)

static const GameInput no_input = {0, 0, 0, 0, 0, 0, 0, 0};


/* A cleared wave now pauses for a moment ("WAVE n CLEAR") before the next one starts: step until the wave changes. */
static void step_to_next_wave(GameState *state) {
    const uint8_t wave = state->wave;
    int guard;

    for (guard = 0; guard < 200 && state->wave == wave; ++guard) {
        game_step(state, &no_input);
    }
}

/* Skip the entry warning of every enemy on the field. */
static void end_warnings(GameState *state) {
    int index;

    for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
        state->enemies[index].warn = 0;
    }
}

/* ---- capture renderer ---- */

#define MAX_LINES 600
#define MAX_RECTS 100
#define MAX_TEXTS 40

typedef struct TextCall {
    int x;
    int y;
    int fg;
    int bg;
    int scale;
    char text[40];
} TextCall;

static int lines[MAX_LINES][4];
static int line_count;
static int color_counts[16];
static int rects[MAX_RECTS][4];
static int rect_count;
static TextCall texts[MAX_TEXTS];
static int text_count;
static int clear_calls;
static int point_count;
static int point_calls;

static void reset_capture(void) {
    line_count = 0;
    rect_count = 0;
    text_count = 0;
    clear_calls = 0;
    point_count = 0;
    point_calls = 0;
    memset(color_counts, 0, sizeof(color_counts));
}

static void capture_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    (void) context;
    if (line_count < MAX_LINES) {
        lines[line_count][0] = x0;
        lines[line_count][1] = y0;
        lines[line_count][2] = x1;
        lines[line_count][3] = y1;
    }
    ++line_count;
    ++color_counts[color & 15];
}

static void capture_rect(void *context, int x0, int y0, int x1, int y1) {
    (void) context;
    if (rect_count < MAX_RECTS) {
        rects[rect_count][0] = x0;
        rects[rect_count][1] = y0;
        rects[rect_count][2] = x1;
        rects[rect_count][3] = y1;
    }
    ++rect_count;
}

static void capture_text(void *context, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t scale) {
    (void) context;
    if (text_count < MAX_TEXTS) {
        texts[text_count].x = x;
        texts[text_count].y = y;
        texts[text_count].fg = fg;
        texts[text_count].bg = bg;
        texts[text_count].scale = scale;
        strncpy(texts[text_count].text, text, sizeof(texts[text_count].text) - 1);
        texts[text_count].text[sizeof(texts[text_count].text) - 1] = 0;
    }
    ++text_count;
}

static void capture_clear(void *context) {
    (void) context;
    ++clear_calls;
}

static int point_layers[16];
static int points_xy[8][GAME_STAR_COUNT * 2];

static void capture_points(void *context, const int16_t *points, int count, uint8_t color) {
    int index;

    (void) context;
    if (point_calls < 8) {
        for (index = 0; index < count && index < GAME_STAR_COUNT; ++index) {
            points_xy[point_calls][index * 2] = points[index * 2];
            points_xy[point_calls][index * 2 + 1] = points[index * 2 + 1];
        }
        point_layers[point_calls] = color;
    }
    ++point_calls;
    point_count += count;
}

static GameRenderer make_renderer(void) {
    GameRenderer renderer;

    renderer.context = NULL;
    renderer.line = capture_line;
    renderer.polygon = NULL;
    renderer.dirty = capture_rect;
    renderer.text = capture_text;
    renderer.clear_field = capture_clear;
    renderer.points = capture_points;
    renderer.polygon_offsets = NULL;
    renderer.sprite = NULL;
    renderer.rocks = NULL;
    renderer.low_detail = 0;
    return renderer;
}

static void render(GameState *state) {
    const GameRenderer renderer = make_renderer();

    reset_capture();
    game_render(state, &renderer);
}

static int has_text(const char *wanted) {
    int index;

    for (index = 0; index < text_count && index < MAX_TEXTS; ++index) {
        if (strcmp(texts[index].text, wanted) == 0) {
            return 1;
        }
    }
    return 0;
}

static const TextCall *find_text(const char *wanted) {
    int index;

    for (index = 0; index < text_count && index < MAX_TEXTS; ++index) {
        if (strcmp(texts[index].text, wanted) == 0) {
            return &texts[index];
        }
    }
    return NULL;
}

/* ---- helpers ---- */

static void init_playing(GameState *state) {
    game_init(state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    game_start(state);
}

static int count_asteroids(const GameState *state, int size) {
    int active = 0;
    int index;

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        if (state->asteroids[index].active && (size == 0 || state->asteroids[index].size == size)) {
            ++active;
        }
    }
    return active;
}

/* Remove every rock except one parked in a far corner so the wave does not end. */
static void clear_field(GameState *state) {
    memset(state->asteroids, 0, sizeof(state->asteroids));
    memset(state->bullets, 0, sizeof(state->bullets));
    state->asteroids[0].active = 1;
    state->asteroids[0].size = GAME_ASTEROID_SMALL;
    state->asteroids[0].point_count = 8;
    state->asteroids[0].x = 5L << GAME_FIX_SHIFT;
    state->asteroids[0].y = 5L << GAME_FIX_SHIFT;
}

static void put_rock_on_ship(GameState *state, int size) {
    state->asteroids[1] = state->asteroids[0];
    state->asteroids[1].size = (uint8_t) size;
    state->asteroids[1].x = state->ship.x;
    state->asteroids[1].y = state->ship.y;
}

/* ---- tests: state and flow ---- */

static void test_initial_state(void) {
    GameState state;
    int index;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);

    CHECK(state.mode == GAME_MODE_TITLE);
    CHECK(state.field_x == FIELD_X && state.field_y == FIELD_Y);
    CHECK(state.field_width == FIELD_W && state.field_height == FIELD_H);
    CHECK(state.x_scale == 230);   /* 288 / 320 */
    CHECK(state.y_scale == 187);   /* 176 / 240 */
    CHECK(state.lives == 3);
    CHECK(count_asteroids(&state, 0) == 0);
    for (index = 0; index < GAME_HIGH_SCORE_COUNT; ++index) {
        CHECK(state.high_scores[index].score == 0);
        CHECK(strcmp(state.high_scores[index].initials, "---") == 0);
    }
}

static void test_start_from_title(void) {
    GameState state;
    GameInput space = {0, 0, 0, 1, 0, 1, 0, 0};
    int frame;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    for (frame = 0; frame < 10; ++frame) {
        game_step(&state, &no_input);
    }
    CHECK(state.mode == GAME_MODE_TITLE);

    game_step(&state, &space);
    CHECK(state.mode == GAME_MODE_PLAYING);
    CHECK(state.wave == 1);
    CHECK(state.lives == 3);
    CHECK(state.score == 0);
    CHECK(count_asteroids(&state, GAME_ASTEROID_LARGE) == 6);
    CHECK(state.banner_timer > 0);
    CHECK(state.screen_refresh == 2);

    /* holding the key does not restart anything */
    state.score = 40;
    game_step(&state, &space);
    CHECK(state.score >= 40);
    CHECK(state.mode == GAME_MODE_PLAYING);
}

static void test_no_autopilot(void) {
    GameState state;
    int frame;
    int bullets_seen = 0;
    int index;
    uint16_t angle;

    init_playing(&state);
    angle = state.ship.angle;
    for (frame = 0; frame < 300; ++frame) {
        state.ship.invulnerability = 255;
        game_step(&state, &no_input);
        for (index = 0; index < GAME_MAX_BULLETS; ++index) {
            bullets_seen += state.bullets[index].active;
        }
    }
    CHECK(state.mode == GAME_MODE_PLAYING);
    CHECK(state.ship.angle == angle);
    CHECK(state.ship.vx == 0 && state.ship.vy == 0);
    CHECK(!state.ship.thrusting);
    CHECK(bullets_seen == 0);
}

static void test_wave_sizes(void) {
    GameState state;
    int wave;
    const int expected[] = {0, 6, 8, 10, 12, 14, 16, 18, 20, 20, 20};

    init_playing(&state);
    for (wave = 1; wave <= 10; ++wave) {
        memset(state.asteroids, 0, sizeof(state.asteroids));
        memset(&state.boss, 0, sizeof(state.boss));
        memset(state.enemies, 0, sizeof(state.enemies));
        state.wave = (uint8_t) (wave - 1);
        state.ufo_timer = 30000;
        state.alien_timer = 30000;
        step_to_next_wave(&state);
        CHECK(state.wave == wave);
        if (wave % 5 == 0) {
            CHECK(state.boss.active);                    /* every fifth wave is a boss fight, without rocks */
            CHECK(count_asteroids(&state, 0) == 0);
        } else {
            CHECK(!state.boss.active);
            CHECK(count_asteroids(&state, GAME_ASTEROID_LARGE) == expected[wave]);
        }
    }
}

static void test_asteroid_shapes(void) {
    GameState state;
    int index;
    int point;

    init_playing(&state);
    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        const GameAsteroid *asteroid = &state.asteroids[index];
        if (!asteroid->active) {
            continue;
        }
        CHECK(asteroid->point_count >= 8 && asteroid->point_count <= 11);
        for (point = 0; point < asteroid->point_count; ++point) {
            CHECK(asteroid->radius[point] >= 12 && asteroid->radius[point] <= 19);
        }
    }
}

static void test_bullet_breaks_asteroids_and_scores(void) {
    GameState state;
    const int expected_score[] = {0, 100, 50, 20};
    int size;

    init_playing(&state);

    for (size = GAME_ASTEROID_LARGE; size >= GAME_ASTEROID_SMALL; --size) {
        clear_field(&state);
        state.score = 0;
        state.asteroids[1].active = 1;
        state.asteroids[1].size = (uint8_t) size;
        state.asteroids[1].point_count = 8;
        state.asteroids[1].x = 100L << GAME_FIX_SHIFT;
        state.asteroids[1].y = 50L << GAME_FIX_SHIFT;
        state.bullets[0].active = 1;
        state.bullets[0].life = 4;
        state.bullets[0].x = state.asteroids[1].x;
        state.bullets[0].y = state.asteroids[1].y;

        game_step(&state, &no_input);

        CHECK(state.score == (uint32_t) expected_score[size]);
        CHECK(!state.bullets[0].active);
        if (size > GAME_ASTEROID_SMALL) {
            CHECK(count_asteroids(&state, size - 1) == 2 + (size - 1 == GAME_ASTEROID_SMALL ? 1 : 0));
        }
    }
}

static void test_ship_wraps_across_screen(void) {
    GameState state;

    init_playing(&state);
    state.ship.x = -1;
    state.ship.y = -1;

    game_step(&state, &no_input);

    CHECK(state.ship.x >= 0);
    CHECK(state.ship.y >= 0);
    CHECK(state.ship.x < ((long) GAME_WORLD_WIDTH << GAME_FIX_SHIFT));
    CHECK(state.ship.y < ((long) GAME_WORLD_HEIGHT << GAME_FIX_SHIFT));
}

static void test_turning_speed(void) {
    GameState state;
    GameInput right = {0, 1, 0, 0, 0, 0, 0, 0};
    GameInput left = {1, 0, 0, 0, 0, 0, 0, 0};
    int frame;

    init_playing(&state);
    clear_field(&state);
    for (frame = 0; frame < 100; ++frame) {
        game_step(&state, &right);
    }
    /* 100 frames x 626 = 62600 of 65536: 0.955 turn clockwise from "up" */
    CHECK(state.ship.angle == (uint16_t) (49152u + 62600u));

    for (frame = 0; frame < 100; ++frame) {
        game_step(&state, &left);
    }
    CHECK(state.ship.angle == 49152u);
}

static void test_thrust_and_drag(void) {
    GameState state;
    GameInput thrust = {0, 0, 1, 0, 0, 0, 0, 0};
    GameInput turn = {1, 0, 0, 0, 0, 0, 0, 0};
    int32_t speed_after_ten = 0;
    int32_t coasting;
    int frame;

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;

    for (frame = 0; frame < 300; ++frame) {
        game_step(&state, &thrust);
        if (frame == 9) {
            speed_after_ten = -state.ship.vy;
        }
        CHECK(state.ship.vy <= 0);
    }

    /* pointing up: velocity goes up the screen, almost none sideways */
    CHECK(state.ship.vy < 0);
    CHECK(state.ship.vx > -2000 && state.ship.vx < 2000);
    CHECK(speed_after_ten > 25000 && speed_after_ten < 40000);
    /* top speed is 8 px/frame at 60 Hz, 3.84 world px/frame here */
    CHECK(-state.ship.vy <= 251658L + 300);
    CHECK(-state.ship.vy > 200000L);

    /* coasting slows the ship by about 1.2% per frame (turning does not add speed) */
    coasting = -state.ship.vy;
    for (frame = 0; frame < 50; ++frame) {
        game_step(&state, &turn);
    }
    CHECK(-state.ship.vy < coasting * 6 / 10);
    CHECK(-state.ship.vy > coasting * 4 / 10);
}

static void test_bullets(void) {
    GameState state;
    GameInput fire = {0, 0, 0, 1, 0, 0, 0, 0};
    int frame;
    int fired = 0;

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;

    game_step(&state, &fire);
    CHECK(state.bullets[0].active);
    /* bullet flies up at 7 px/frame @60 Hz = 3.36 world px/frame */
    CHECK(state.bullets[0].vy < -219000L && state.bullets[0].vy > -221500L);
    CHECK(state.bullets[0].vx > -300L && state.bullets[0].vx < 300L);

    for (frame = 0; frame < 120; ++frame) {
        int index;
        int active = 0;
        game_step(&state, &fire);
        for (index = 0; index < GAME_MAX_BULLETS; ++index) {
            active += state.bullets[index].active;
        }
        if (active > fired) {
            fired = active;
        }
    }
    /* a shot lives 50 frames and the gun fires every 12: about four in the air at once */
    CHECK(fired >= 4 && fired <= GAME_MAX_BULLETS);

    /* a single shot flies for 50 frames: 49 left after the frame it was fired */
    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    game_step(&state, &fire);
    CHECK(state.bullets[0].active);
    CHECK(state.bullets[0].life == 49);
    for (frame = 0; frame < 48; ++frame) {
        game_step(&state, &no_input);
    }
    CHECK(state.bullets[0].active);
    CHECK(state.bullets[0].life == 1);
    game_step(&state, &no_input);
    CHECK(!state.bullets[0].active);
}

static long distance_squared(long ax, long ay, long bx, long by);
static GameEnemy *first_enemy(GameState *state, int kind);
static void shoot_enemy(GameState *state, GameEnemy *enemy);

static void test_ship_collision(void) {
    GameState state;

    init_playing(&state);
    clear_field(&state);
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);

    /* invulnerable after spawning: no damage */
    state.ship.invulnerability = 30;
    game_step(&state, &no_input);
    CHECK(state.lives == 3);

    state.ship.invulnerability = 0;
    game_step(&state, &no_input);
    CHECK(state.lives == 2);
    CHECK(state.mode == GAME_MODE_PLAYING);
    CHECK(state.ship.invulnerability == 100);
    CHECK(state.life_lost_this_wave);
    /* it comes back at a quiet spot, not on top of the rock that hit it, and inside the field */
    CHECK(state.ship.x >= (40L << GAME_FIX_SHIFT) && state.ship.x <= (280L << GAME_FIX_SHIFT));
    CHECK(state.ship.y >= (32L << GAME_FIX_SHIFT) && state.ship.y <= (208L << GAME_FIX_SHIFT));
    CHECK(distance_squared(state.ship.x, state.ship.y, state.asteroids[1].x, state.asteroids[1].y) > 40L * 40L);
}

static void test_wave_clear_advances(void) {
    GameState state;

    init_playing(&state);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    game_step(&state, &no_input);

    /* the wave is over: a pause with a bonus (wave 1 x 100, and 500 for not losing a life) comes first */
    CHECK(state.wave == 1);
    CHECK(state.wave_clear_timer > 0);
    CHECK(state.clear_bonus == 600);
    CHECK(state.score == 600);
    step_to_next_wave(&state);

    CHECK(state.wave == 2);
    CHECK(count_asteroids(&state, GAME_ASTEROID_LARGE) == 8);
    CHECK(state.ship.invulnerability >= 49);   /* a short grace, not a full respawn's worth */
    CHECK(state.banner_timer > 0);
}

static void test_asteroid_speeds_scale_with_wave(void) {
    GameState state;
    int32_t total_wave1 = 0;
    int32_t total_wave9 = 0;
    int index;
    int wave;

    for (wave = 1; wave <= 9; wave += 8) {
        int32_t total = 0;
        int rocks = 0;
        int round;

        init_playing(&state);
        for (round = 0; round < 6; ++round) {
            memset(state.asteroids, 0, sizeof(state.asteroids));
            state.wave = (uint8_t) (wave - 1);
            state.rng_state += (uint32_t) round * 7919u;
            step_to_next_wave(&state);
            for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
                const GameAsteroid *asteroid = &state.asteroids[index];
                if (asteroid->active) {
                    const long vx = asteroid->vx >> 8;
                    const long vy = asteroid->vy >> 8;
                    /* large rocks: at most 1.5 x 0.24 px/frame, times up to 1.4 for wave 9 = 0.5 px/frame = 129 in 8.8 */
                    CHECK(vx * vx + vy * vy <= 140L * 140L);
                    total += (vx < 0 ? -vx : vx) + (vy < 0 ? -vy : vy);
                    ++rocks;
                }
            }
        }
        if (wave == 1) {
            total_wave1 = total / rocks;
        } else {
            total_wave9 = total / rocks;
        }
    }
    /* +5% per wave: 8 waves later rocks are around 1.4x faster */
    CHECK(total_wave9 > total_wave1 * 12 / 10);
    CHECK(total_wave9 < total_wave1 * 17 / 10);
}

/* ---- tests: extra lives, hyperspace, pause ---- */

static void test_extra_lives(void) {
    GameState state;

    init_playing(&state);
    clear_field(&state);
    CHECK(state.next_extra_life == 10000);

    state.score = 9990;
    state.asteroids[1].active = 1;
    state.asteroids[1].size = GAME_ASTEROID_LARGE;
    state.asteroids[1].point_count = 8;
    state.asteroids[1].x = 100L << GAME_FIX_SHIFT;
    state.asteroids[1].y = 50L << GAME_FIX_SHIFT;
    state.bullets[0].active = 1;
    state.bullets[0].life = 4;
    state.bullets[0].x = state.asteroids[1].x;
    state.bullets[0].y = state.asteroids[1].y;

    game_step(&state, &no_input);
    CHECK(state.score == 10010);
    CHECK(state.lives == 4);
    CHECK(state.next_extra_life == 30000);   /* then 30000, 60000, 100000 ... */

    /* lives are capped at 5 */
    state.lives = 5;
    state.score = 29990;
    state.asteroids[1].active = 1;
    state.asteroids[1].size = GAME_ASTEROID_LARGE;
    state.asteroids[1].x = 100L << GAME_FIX_SHIFT;
    state.asteroids[1].y = 50L << GAME_FIX_SHIFT;
    state.bullets[0].active = 1;
    state.bullets[0].life = 4;
    state.bullets[0].x = state.asteroids[1].x;
    state.bullets[0].y = state.asteroids[1].y;
    game_step(&state, &no_input);
    CHECK(state.lives == 5);
    CHECK(state.next_extra_life == 60000);

    /* the later thresholds */
    state.score = 59990;
    state.lives = 3;
    state.asteroids[1].active = 1;
    state.asteroids[1].size = GAME_ASTEROID_LARGE;
    state.asteroids[1].x = 100L << GAME_FIX_SHIFT;
    state.asteroids[1].y = 50L << GAME_FIX_SHIFT;
    state.bullets[0].active = 1;
    state.bullets[0].life = 4;
    state.bullets[0].x = state.asteroids[1].x;
    state.bullets[0].y = state.asteroids[1].y;
    game_step(&state, &no_input);
    CHECK(state.lives == 4);
    CHECK(state.next_extra_life == 100000);
}

static long distance_squared(long ax, long ay, long bx, long by) {
    const long dx = (ax - bx) >> GAME_FIX_SHIFT;
    const long dy = (ay - by) >> GAME_FIX_SHIFT;

    return dx * dx + dy * dy;
}

static void test_hyperspace(void) {
    GameState state;
    GameInput hyper = {0, 0, 0, 0, 1, 0, 0, 0};
    int index;
    int frame;
    long old_x;
    long old_y;

    init_playing(&state);
    CHECK(state.ship.hyperspace_cooldown == 0);
    state.ship.vx = 100000;
    state.ship.vy = -50000;
    state.ship.invulnerability = 0;
    old_x = state.ship.x;
    old_y = state.ship.y;

    game_step(&state, &hyper);

    /* jumped, stopped, invulnerable, recharging */
    CHECK(state.ship.x != old_x || state.ship.y != old_y);
    CHECK(state.ship.vx == 0 && state.ship.vy == 0);
    CHECK(state.ship.invulnerability >= 99);
    CHECK(state.ship.hyperspace_cooldown == GAME_HYPERSPACE_RECHARGE_FRAMES);

    /* it lands clear of every rock: rock radius + 24 world pixels (allowing one frame of rock movement) */
    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        const GameAsteroid *asteroid = &state.asteroids[index];
        if (asteroid->active) {
            const long radius = (asteroid->size == 3 ? 16 : asteroid->size == 2 ? 10 : 5) + 24 - 2;
            CHECK(distance_squared(state.ship.x, state.ship.y, asteroid->x, asteroid->y) >= radius * radius);
        }
    }

    /* no second jump until it has recharged */
    {
        const long jumped_x = state.ship.x;
        const long jumped_y = state.ship.y;

        for (frame = 0; frame < GAME_HYPERSPACE_RECHARGE_FRAMES - 5; ++frame) {
            state.ship.invulnerability = 255;
            game_step(&state, &hyper);
            CHECK(state.ship.x == jumped_x && state.ship.y == jumped_y);
        }
        CHECK(state.ship.hyperspace_cooldown > 0);
        for (frame = 0; frame < 10; ++frame) {
            state.ship.invulnerability = 255;
            game_step(&state, &no_input);
        }
        CHECK(state.ship.hyperspace_cooldown == 0);
    }

    /* a lost life recharges it */
    state.ship.hyperspace_cooldown = 300;
    clear_field(&state);
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);
    state.ship.invulnerability = 0;
    game_step(&state, &no_input);
    CHECK(state.ship.hyperspace_cooldown == 0);
}

static void test_pause(void) {
    GameState state;
    GameInput pause = {0, 0, 0, 0, 0, 0, 1, 0};
    int32_t x_before;
    uint16_t frame_before;
    int frame;

    init_playing(&state);
    x_before = state.asteroids[0].x;

    game_step(&state, &pause);
    CHECK(state.paused);
    /* holding pause does not toggle again */
    game_step(&state, &pause);
    CHECK(state.paused);

    game_step(&state, &no_input);
    frame_before = state.frame;
    x_before = state.asteroids[0].x;
    for (frame = 0; frame < 20; ++frame) {
        game_step(&state, &no_input);
    }
    CHECK(state.asteroids[0].x == x_before);
    CHECK(state.paused);
    (void) frame_before;

    render(&state);
    CHECK(has_text("PAUSED"));

    game_step(&state, &pause);
    CHECK(!state.paused);
    game_step(&state, &no_input);
    CHECK(state.asteroids[0].x != x_before);
}

/* ---- tests: Esc, the pause screen, the menu, the setting, typing initials ---- */

static void run_game_over_with_score(GameState *state, uint32_t score);

static void release(GameState *state) {
    game_step(state, &no_input);
}

static void test_escape_pauses_and_leaves(void) {
    GameState state;
    GameInput escape = {0};
    GameInput space = {0};
    int32_t x_before;
    int frame;

    escape.escape = 1;
    space.start = 1;
    space.space = 1;
    space.fire = 1;

    init_playing(&state);

    /* Esc once: paused, nothing moves, the pause screen shows */
    game_step(&state, &escape);
    CHECK(state.paused);
    CHECK(state.mode == GAME_MODE_PLAYING);
    release(&state);
    x_before = state.asteroids[0].x;
    for (frame = 0; frame < 10; ++frame) {
        release(&state);
    }
    CHECK(state.asteroids[0].x == x_before);
    render(&state);
    CHECK(has_text("PAUSED"));
    {
        const TextCall *big = find_text("PAUSED");

        CHECK(big != NULL && big->scale == 4 && big->x % 16 == 0);
    }
    CHECK(has_text("ESC  EXIT TO MENU"));
    CHECK(has_text("SPACE  CONTINUE"));

    /* Space continues, and the space bar that did it does not also fire */
    game_step(&state, &space);
    CHECK(!state.paused);
    CHECK(state.mode == GAME_MODE_PLAYING);
    for (frame = 0; frame < 5; ++frame) {
        game_step(&state, &space);
    }
    CHECK(state.bullets[0].active == 0);
    release(&state);
    CHECK(state.asteroids[0].x != x_before);

    /* Esc, Esc: back to the main menu, with the high scores on it */
    game_step(&state, &escape);
    CHECK(state.paused);
    release(&state);
    game_step(&state, &escape);
    CHECK(state.mode == GAME_MODE_TITLE);
    CHECK(!state.quit_requested);
    release(&state);
    render(&state);
    CHECK(has_text("- HALL OF FAME -"));

    /* Esc on the menu quits the program */
    game_step(&state, &escape);
    CHECK(state.quit_requested);
}

static void test_p_pauses_too(void) {
    GameState state;
    GameInput pause = {0};
    GameInput space = {0};

    pause.pause = 1;
    space.start = 1;
    space.space = 1;
    space.fire = 1;
    init_playing(&state);
    game_step(&state, &pause);
    CHECK(state.paused);
    release(&state);
    game_step(&state, &space);   /* Space also continues after P */
    CHECK(!state.paused);
}

static void test_menu_setting(void) {
    GameState state;
    GameInput down = {0};
    GameInput up = {0};
    GameInput fire = {0};
    GameInput right = {0};

    down.down = 1;
    up.thrust = 1;
    fire.start = 1;
    fire.fire = 1;
    right.right = 1;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    CHECK(state.mode == GAME_MODE_TITLE);
    CHECK(state.menu_item == 0);
    CHECK(!state.hyper_on_space);
    render(&state);
    CHECK(has_text("PRESS FIRE TO START"));
    CHECK(has_text("  HYPERSPACE KEY: H      "));

    /* down selects the setting; fire changes it instead of starting the game */
    game_step(&state, &down);
    CHECK(state.menu_item == 1);
    release(&state);
    game_step(&state, &fire);
    CHECK(state.mode == GAME_MODE_TITLE);
    CHECK(state.hyper_on_space);
    CHECK(state.settings_changed);
    release(&state);
    render(&state);
    CHECK(has_text("> HYPERSPACE KEY: SPACE <"));

    /* left / right change it as well, and it is saved each time */
    state.settings_changed = 0;
    game_step(&state, &right);
    CHECK(!state.hyper_on_space);
    CHECK(state.settings_changed);
    release(&state);

    /* up goes back to the start line, and fire starts the game */
    game_step(&state, &up);
    CHECK(state.menu_item == 0);
    release(&state);
    game_step(&state, &fire);
    CHECK(state.mode == GAME_MODE_PLAYING);
}

static void test_hyperspace_on_space(void) {
    GameState state;
    GameInput space = {0};
    GameInput button = {0};
    GameInput hyper_key = {0};
    int frame;

    space.start = 1;
    space.space = 1;
    space.fire = 1;      /* the platform sets fire for the space bar too */
    button.fire = 1;
    button.fire_alt = 1;
    hyper_key.hyperspace = 1;

    /* default: Space fires, H is hyperspace */
    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    game_step(&state, &space);
    CHECK(state.bullets[0].active || state.bullets[1].active);
    release(&state);
    state.ship.hyperspace_cooldown = 0;
    game_step(&state, &hyper_key);
    CHECK(state.ship.hyperspace_cooldown > 0);

    /* hyperspace on Space: Space jumps and no longer fires; the joystick button and Ctrl/Alt fire */
    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    state.hyper_on_space = 1;
    game_step(&state, &space);
    CHECK(state.ship.hyperspace_cooldown > 0);
    for (frame = 0; frame < GAME_MAX_BULLETS; ++frame) {
        CHECK(!state.bullets[frame].active);
    }
    release(&state);
    game_step(&state, &button);
    CHECK(state.bullets[0].active || state.bullets[1].active);
    release(&state);
    /* H still works as well */
    state.ship.hyperspace_cooldown = 0;
    game_step(&state, &hyper_key);
    CHECK(state.ship.hyperspace_cooldown > 0);
}

static void test_start_key_does_not_jump(void) {
    GameState state;
    GameInput space = {0};
    int frame;

    space.start = 1;
    space.space = 1;
    space.fire = 1;

    /* starting the game with the space bar while hyperspace is on it: still held for a few frames, no jump */
    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    state.hyper_on_space = 1;
    game_step(&state, &space);
    CHECK(state.mode == GAME_MODE_PLAYING);
    for (frame = 0; frame < 10; ++frame) {
        game_step(&state, &space);
    }
    CHECK(state.ship.hyperspace_cooldown == 0);
    release(&state);
    game_step(&state, &space);
    CHECK(state.ship.hyperspace_cooldown > 0);   /* pressed again on purpose */
}

static void test_typing_initials(void) {
    GameState state;
    GameInput typed = {0};
    GameInput back = {0};
    GameInput ret = {0};

    ret.start = 1;
    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    run_game_over_with_score(&state, 1234);
    CHECK(state.mode == GAME_MODE_ENTER_INITIALS);

    typed.typed = 'G';
    game_step(&state, &typed);
    CHECK(state.entry[0] == 'G');
    CHECK(state.entry_position == 1);
    typed.typed = 'O';
    game_step(&state, &typed);
    CHECK(state.entry[1] == 'O');
    CHECK(state.entry_position == 2);

    /* a typo: backspace goes back, the next letter replaces it */
    typed.typed = 'X';
    game_step(&state, &typed);
    CHECK(state.entry[2] == 'X');
    CHECK(state.entry_position == 2);   /* the last letter waits for Return */
    back.backspace = 1;
    game_step(&state, &back);
    CHECK(state.entry_position == 1);
    typed.typed = 'A';
    game_step(&state, &typed);
    CHECK(state.entry[1] == 'A');
    typed.typed = 'L';
    game_step(&state, &typed);
    CHECK(state.entry[2] == 'L');
    CHECK(state.mode == GAME_MODE_ENTER_INITIALS);

    /* lower-case or other keys do nothing */
    typed.typed = '5';
    game_step(&state, &typed);
    CHECK(state.entry[2] == 'L');

    game_step(&state, &ret);
    CHECK(state.mode == GAME_MODE_TITLE);
    CHECK(strcmp(state.high_scores[0].initials, "GAL") == 0);
    CHECK(state.high_scores[0].score == 1234);
}

/* ---- tests: game over, high scores ---- */

static void test_game_over_without_high_score(void) {
    GameState state;
    int frame;

    init_playing(&state);
    clear_field(&state);
    state.score = 500;
    state.lives = 1;
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);
    state.ship.invulnerability = 0;

    game_step(&state, &no_input);
    CHECK(state.mode == GAME_MODE_GAME_OVER);
    CHECK(state.lives == 0);

    for (frame = 0; frame < 149; ++frame) {
        game_step(&state, &no_input);
        CHECK(state.mode == GAME_MODE_GAME_OVER);
    }
    game_step(&state, &no_input);   /* the game over screen lasts 150 frames */
    CHECK(state.mode == GAME_MODE_TITLE);   /* 500 is below the 1000 needed for the table */
    CHECK(!state.scores_changed);
}

static void test_game_over_skip_with_key(void) {
    GameState state;
    GameInput space = {0, 0, 0, 1, 0, 1, 0, 0};
    int frame;

    init_playing(&state);
    clear_field(&state);
    state.score = 500;
    state.lives = 1;
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);
    state.ship.invulnerability = 0;
    game_step(&state, &no_input);
    CHECK(state.mode == GAME_MODE_GAME_OVER);

    /* too early to skip */
    game_step(&state, &space);
    CHECK(state.mode == GAME_MODE_GAME_OVER);
    game_step(&state, &no_input);
    for (frame = 0; frame < 40; ++frame) {
        game_step(&state, &no_input);
    }
    game_step(&state, &space);
    CHECK(state.mode == GAME_MODE_TITLE);
}

static void run_game_over_with_score(GameState *state, uint32_t score) {
    GameHighScore table[GAME_HIGH_SCORE_COUNT];
    int frame;

    memcpy(table, state->high_scores, sizeof(table));   /* a new game keeps the high scores */
    init_playing(state);
    memcpy(state->high_scores, table, sizeof(table));
    clear_field(state);
    state->score = score;
    state->lives = 1;
    put_rock_on_ship(state, GAME_ASTEROID_LARGE);
    state->ship.invulnerability = 0;
    game_step(state, &no_input);
    for (frame = 0; frame < 150 && state->mode == GAME_MODE_GAME_OVER; ++frame) {
        game_step(state, &no_input);
    }
}

static void test_initials_entry(void) {
    GameState state;
    GameInput right = {0, 1, 0, 0, 0, 0, 0, 0};
    GameInput left = {1, 0, 0, 0, 0, 0, 0, 0};
    GameInput fire = {0, 0, 0, 1, 0, 1, 0, 0};
    int frame;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    run_game_over_with_score(&state, 1234);
    CHECK(state.mode == GAME_MODE_ENTER_INITIALS);
    CHECK(strcmp(state.entry, "AAA") == 0);
    CHECK(state.entry_position == 0);

    /* right: A -> B */
    game_step(&state, &right);
    CHECK(state.entry[0] == 'B');
    game_step(&state, &no_input);

    /* left twice: B -> A -> Z (wraps) */
    game_step(&state, &left);
    game_step(&state, &no_input);
    game_step(&state, &left);
    game_step(&state, &no_input);
    CHECK(state.entry[0] == 'Z');

    /* holding right repeats after a delay */
    for (frame = 0; frame < 60; ++frame) {
        game_step(&state, &right);
    }
    CHECK(state.entry[0] != 'Z');
    game_step(&state, &no_input);

    /* set the first letter to 'M', accept, then 'B' and 'C' */
    state.entry[0] = 'M';
    game_step(&state, &fire);
    game_step(&state, &no_input);
    CHECK(state.entry_position == 1);
    CHECK(state.entry[1] == 'M');   /* the next letter starts at the same one */
    state.entry[1] = 'B';
    game_step(&state, &fire);
    game_step(&state, &no_input);
    CHECK(state.entry_position == 2);
    state.entry[2] = 'C';
    game_step(&state, &fire);
    game_step(&state, &no_input);

    CHECK(state.mode == GAME_MODE_TITLE);
    CHECK(state.high_scores[0].score == 1234);
    CHECK(strcmp(state.high_scores[0].initials, "MBC") == 0);
    CHECK(state.scores_changed);
}

static void test_high_score_ranking(void) {
    GameState state;
    int index;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    for (index = 0; index < GAME_HIGH_SCORE_COUNT; ++index) {
        state.high_scores[index].score = (uint32_t) (5000 - index * 1000);
        memcpy(state.high_scores[index].initials, "OLD", 4);
    }

    /* 3500 slots in at rank 3 (after 5000, 4000), pushing the lowest entry off */
    run_game_over_with_score(&state, 3500);
    CHECK(state.mode == GAME_MODE_ENTER_INITIALS);
    memcpy(state.entry, "NEW", 4);
    state.entry_position = 2;
    {
        const GameInput fire = {0, 0, 0, 1, 0, 1, 0, 0};
        game_step(&state, &fire);
    }
    CHECK(state.mode == GAME_MODE_TITLE);
    CHECK(state.high_scores[0].score == 5000);
    CHECK(state.high_scores[1].score == 4000);
    CHECK(state.high_scores[2].score == 3500);
    CHECK(strcmp(state.high_scores[2].initials, "NEW") == 0);
    CHECK(state.high_scores[3].score == 3000);
    CHECK(state.high_scores[4].score == 2000);

    /* a score below the lowest entry does not qualify, however high */
    run_game_over_with_score(&state, 1999);
    CHECK(state.mode == GAME_MODE_TITLE);
}

static void test_score_file_round_trip(void) {
    GameState state;
    GameState loaded;
    uint8_t data[GAME_SCORE_FILE_BYTES];
    uint8_t bad[GAME_SCORE_FILE_BYTES];
    int index;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    for (index = 0; index < GAME_HIGH_SCORE_COUNT; ++index) {
        state.high_scores[index].score = 123456u * (uint32_t) (index + 1);
        state.high_scores[index].initials[0] = (char) ('A' + index);
        state.high_scores[index].initials[1] = 'Z';
        state.high_scores[index].initials[2] = (index & 1) ? '-' : 'Q';
    }
    game_scores_pack(&state, data);
    CHECK(data[0] == 'A' && data[1] == 'S' && data[2] == 'T' && data[3] == '1');
    CHECK(GAME_SCORE_FILE_BYTES == 4 + GAME_HIGH_SCORE_COUNT * 8);

    game_init(&loaded, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    CHECK(game_scores_unpack(&loaded, data));
    for (index = 0; index < GAME_HIGH_SCORE_COUNT; ++index) {
        CHECK(loaded.high_scores[index].score == state.high_scores[index].score);
        CHECK(strcmp(loaded.high_scores[index].initials, state.high_scores[index].initials) == 0);
    }

    /* damaged files are rejected and leave the table alone */
    memcpy(bad, data, sizeof(bad));
    bad[0] = 'X';
    game_init(&loaded, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    CHECK(!game_scores_unpack(&loaded, bad));
    CHECK(loaded.high_scores[0].score == 0);
    memcpy(bad, data, sizeof(bad));
    bad[4] = '!';
    CHECK(!game_scores_unpack(&loaded, bad));
    CHECK(loaded.high_scores[0].score == 0);
}

/* ---- tests: rendering ---- */

static void test_render_playing_geometry(void) {
    GameState state;

    init_playing(&state);
    state.ship.invulnerability = 0;
    render(&state);

    /* 4 ship edges, and 8-11 edges for each of the 6 large rocks */
    CHECK(color_counts[GAME_COLOR_SHIP] == 4);
    CHECK(color_counts[GAME_COLOR_ASTEROID_LARGE] >= 6 * 8 && color_counts[GAME_COLOR_ASTEROID_LARGE] <= 6 * 11);
    CHECK(color_counts[GAME_COLOR_FLAME] == 0);
    CHECK(rect_count >= 7);

    {
        GameInput thrust = {0, 0, 1, 0, 0, 0, 0, 0};
        game_step(&state, &thrust);
        render(&state);
        CHECK(color_counts[GAME_COLOR_FLAME] == 2);
    }
}

static int ship_min_x;
static int ship_max_x;
static int ship_min_y;
static int ship_max_y;

static void measure_ship(GameState *state) {
    int index;

    render(state);
    ship_min_x = ship_min_y = 10000;
    ship_max_x = ship_max_y = -10000;
    /* the ship's four edges come first in the capture */
    for (index = 0; index < 4 && index < line_count; ++index) {
        if (lines[index][0] < ship_min_x) ship_min_x = lines[index][0];
        if (lines[index][2] < ship_min_x) ship_min_x = lines[index][2];
        if (lines[index][0] > ship_max_x) ship_max_x = lines[index][0];
        if (lines[index][2] > ship_max_x) ship_max_x = lines[index][2];
        if (lines[index][1] < ship_min_y) ship_min_y = lines[index][1];
        if (lines[index][3] < ship_min_y) ship_min_y = lines[index][3];
        if (lines[index][1] > ship_max_y) ship_max_y = lines[index][1];
        if (lines[index][3] > ship_max_y) ship_max_y = lines[index][3];
    }
}

static void test_ship_is_long_and_narrow(void) {
    GameState state;

    /* a 320x240 field has square pixels, so screen extents equal world extents */
    game_init(&state, 0, 0, 320, 240);
    game_start(&state);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.ship.invulnerability = 0;

    state.ship.angle = 49152u;   /* up */
    measure_ship(&state);
    CHECK(ship_max_y - ship_min_y >= 15 && ship_max_y - ship_min_y <= 17);
    CHECK(ship_max_x - ship_min_x >= 7 && ship_max_x - ship_min_x <= 9);

    state.ship.angle = 0;        /* right */
    measure_ship(&state);
    CHECK(ship_max_x - ship_min_x >= 15 && ship_max_x - ship_min_x <= 17);
    CHECK(ship_max_y - ship_min_y >= 7 && ship_max_y - ship_min_y <= 9);

    /* the real field is 288x176: the ship keeps its proportions on the 4:3 screen (x 0.9, y 0.73) */
    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    game_start(&state);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.ship.invulnerability = 0;
    state.ship.angle = 49152u;
    measure_ship(&state);
    CHECK(ship_max_y - ship_min_y >= 10 && ship_max_y - ship_min_y <= 13);
    CHECK(ship_max_x - ship_min_x >= 6 && ship_max_x - ship_min_x <= 8);
    /* centred in the field */
    CHECK(ship_min_x > FIELD_X + FIELD_W / 2 - 10 && ship_max_x < FIELD_X + FIELD_W / 2 + 10);
    CHECK(ship_min_y > FIELD_Y + FIELD_H / 2 - 14 && ship_max_y < FIELD_Y + FIELD_H / 2 + 14);
}

static int point_is_covered(int x, int y) {
    int index;

    for (index = 0; index < rect_count && index < MAX_RECTS; ++index) {
        if (x >= rects[index][0] && x <= rects[index][2] && y >= rects[index][1] && y <= rects[index][3]) {
            return 1;
        }
    }
    return 0;
}

/* Everything drawn on the field must sit inside a rectangle reported as dirty, or erasing would leave ghosts. */
static void check_dirty_coverage(GameState *state) {
    int index;

    render(state);
    CHECK(line_count > 0 && line_count <= MAX_LINES);
    CHECK(rect_count <= MAX_RECTS);
    for (index = 0; index < line_count && index < MAX_LINES; ++index) {
        CHECK(point_is_covered(lines[index][0], lines[index][1]));
        CHECK(point_is_covered(lines[index][2], lines[index][3]));
    }
}

static void test_dirty_rects_cover_everything_drawn(void) {
    GameState state;
    int frame;

    init_playing(&state);
    for (frame = 0; frame < 700; ++frame) {
        GameInput input = {0, 0, 0, 0, 0, 0, 0, 0};
        input.thrust = (uint8_t) ((frame / 40) & 1);
        input.left = (uint8_t) ((frame / 25) & 1);
        input.fire = (uint8_t) ((frame / 7) & 1);
        input.hyperspace = (uint8_t) (frame % 173 == 0);
        if (frame % 100 == 99) {
            state.wave = 6;   /* bigger fields */
            memset(state.asteroids, 0, sizeof(state.asteroids));
        }
        if (state.mode != GAME_MODE_PLAYING) {
            game_start(&state);
        }
        state.ship.invulnerability = (uint8_t) (frame % 3 == 0 ? 255 : state.ship.invulnerability);
        game_step(&state, &input);
        if (frame % 3 == 0) {
            check_dirty_coverage(&state);
        }
    }
}

static void test_render_cache_is_reused(void) {
    GameState state;
    const GameAsteroid *asteroid;
    int8_t first_x[GAME_MAX_ASTEROID_POINTS];
    int frame;

    game_init(&state, 0, 0, 320, 240);
    game_start(&state);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.asteroids[0].active = 1;
    state.asteroids[0].size = GAME_ASTEROID_LARGE;
    state.asteroids[0].point_count = 8;
    for (frame = 0; frame < 8; ++frame) {
        state.asteroids[0].radius[frame] = 16;
    }
    state.asteroids[0].x = 100L << GAME_FIX_SHIFT;
    state.asteroids[0].y = 100L << GAME_FIX_SHIFT;
    state.asteroids[0].angle = 0;
    asteroid = &state.asteroids[0];

    render(&state);
    CHECK(asteroid->cache_valid);
    /* vertex 0 points right, at radius 16 */
    CHECK(asteroid->off_x[0] == 16 && asteroid->off_y[0] == 0);
    memcpy(first_x, asteroid->off_x, sizeof(first_x));

    /* a small turn stays in the same 64-step orientation and reuses the cache */
    state.asteroids[0].angle = 500;
    render(&state);
    CHECK(memcmp(first_x, asteroid->off_x, sizeof(first_x)) == 0);

    /* a bigger turn (>5.6 degrees) rebuilds it */
    state.asteroids[0].angle = 3000;
    render(&state);
    CHECK(asteroid->cache_index == 2);
    CHECK(memcmp(first_x, asteroid->off_x, sizeof(first_x)) != 0);
}

static void test_text_layout_is_aligned(void) {
    GameState state;
    int index;
    int mode;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    for (mode = 0; mode < 4; ++mode) {
        state.mode = (uint8_t) mode;
        state.paused = 0;
        state.banner_timer = 10;
        state.score = 1234;
        state.entry[0] = 'A';
        state.entry[1] = 'B';
        state.entry[2] = 'C';
        state.entry[3] = 0;
        state.screen_refresh = 2;
        state.hud_refresh = 2;
        render(&state);
        CHECK(text_count > 0 && text_count <= MAX_TEXTS);
        for (index = 0; index < text_count && index < MAX_TEXTS; ++index) {
            const int cell = (texts[index].scale == 2) ? 16 : 8;
            const int width = (int) strlen(texts[index].text) * cell;

            CHECK(texts[index].scale == 1 || texts[index].scale == 2);
            CHECK(texts[index].x % cell == 0);
            CHECK(texts[index].x >= 0 && texts[index].x + width <= 320);
            CHECK(texts[index].y >= 0 && texts[index].y + cell <= 200);
        }
    }
}

static void test_hud_content_and_redraw(void) {
    GameState state;
    int frame;
    int text_before;

    init_playing(&state);
    state.score = 1234;
    state.lives = 2;
    state.wave = 7;
    render(&state);

    CHECK(has_text("SCORE 001234"));
    CHECK(has_text("WAVE 07"));
    CHECK(has_text("HI 001234"));   /* the running score is the hi score until beaten */
    CHECK(has_text("HYPER READY "));
    {
        const TextCall *score = find_text("SCORE 001234");
        CHECK(score != NULL && score->y == 0 && score->bg == GAME_COLOR_FRAME);
    }
    {
        /* two ship icons (code 127) then blanks */
        char icons[10];
        memset(icons, ' ', 8);
        icons[0] = 127;
        icons[1] = 127;
        icons[8] = 0;
        CHECK(has_text(icons));
    }

    /* unchanged values are drawn only for the two frames it takes to fill both screen buffers */
    render(&state);
    CHECK(has_text("SCORE 001234"));
    render(&state);
    CHECK(!has_text("SCORE 001234"));
    text_before = text_count;
    for (frame = 0; frame < 5; ++frame) {
        render(&state);
        CHECK(text_count <= 2);   /* only the wave banner, if any */
    }
    (void) text_before;

    /* a change redraws it */
    state.score = 1300;
    render(&state);
    CHECK(has_text("SCORE 001300"));

    /* many lives are shown as an icon and a count */
    state.lives = 8;
    render(&state);
    {
        char icons[10];
        memset(icons, ' ', 8);
        icons[0] = 127;
        icons[1] = 'X';
        icons[2] = '8';
        icons[8] = 0;
        CHECK(has_text(icons));
    }

    /* the hyperspace charge is shown as a percentage while recharging */
    state.ship.hyperspace_cooldown = 250;
    render(&state);
    CHECK(has_text("HYPER 50%  "));
}

static void test_static_screens_draw_once_per_buffer(void) {
    GameState state;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);

    render(&state);
    CHECK(clear_calls == 1);
    CHECK(has_text("A"));   /* the logo is animated letter by letter */
    CHECK(find_text("A")->scale == 2);
    CHECK(has_text("PRESS FIRE TO START"));
    CHECK(has_text("- HALL OF FAME -"));
    CHECK(has_text("1. ---  000000"));
    render(&state);
    CHECK(clear_calls == 1);

    /* both buffers are done: only the animated logo is redrawn */
    render(&state);
    CHECK(clear_calls == 0);
    CHECK(has_text("A"));
    CHECK(!has_text("PRESS FIRE TO START"));
    CHECK(!has_text("HALL OF FAME"));
}

static void test_title_prompt_blinks(void) {
    GameState state;
    int frame;
    int toggles = 0;
    uint8_t last;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    render(&state);
    render(&state);
    last = state.prompt_visible;
    CHECK(last == 1);

    for (frame = 0; frame < 100; ++frame) {
        game_step(&state, &no_input);
        if (state.prompt_visible != last) {
            const TextCall *prompt;

            ++toggles;
            last = state.prompt_visible;
            /* the new state is drawn on two consecutive frames (one per screen buffer) */
            render(&state);
            prompt = find_text("PRESS FIRE TO START");
            CHECK(prompt != NULL);
            if (prompt != NULL) {
                CHECK(prompt->fg == (last ? GAME_COLOR_YELLOW : GAME_COLOR_BLACK));
            }
            render(&state);
            CHECK(has_text("PRESS FIRE TO START"));
            render(&state);
            CHECK(!has_text("PRESS FIRE TO START"));
        }
    }
    CHECK(toggles == 4);   /* every 25 frames */
}

static void test_game_over_and_initials_screens(void) {
    GameState state;

    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    run_game_over_with_score(&state, 4321);
    CHECK(state.mode == GAME_MODE_ENTER_INITIALS);
    render(&state);
    CHECK(clear_calls == 1);
    CHECK(has_text("NEW HIGH SCORE!"));
    CHECK(has_text("SCORE 004321"));
    CHECK(has_text("ENTER YOUR INITIALS"));
    CHECK(has_text("A"));

    /* the game over screen itself */
    init_playing(&state);
    clear_field(&state);
    state.score = 4321;
    state.lives = 1;
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);
    state.ship.invulnerability = 0;
    game_step(&state, &no_input);
    CHECK(state.mode == GAME_MODE_GAME_OVER);
    render(&state);
    CHECK(has_text("GAME OVER"));
    CHECK(find_text("GAME OVER")->scale == 2);
    CHECK(has_text("YOUR SCORE 004321"));
    CHECK(has_text("NEW HIGH SCORE!"));
}

static void test_banner_and_playing_screen(void) {
    GameState state;

    init_playing(&state);
    render(&state);
    CHECK(clear_calls == 1);   /* first frame of the game clears the title screen away */
    CHECK(has_text("WAVE 01"));
    /* the banner is over the moving game, so its rectangle is reported for erasing */
    CHECK(rect_count > 0);

    state.banner_timer = 0;
    render(&state);
    render(&state);
    CHECK(!has_text("WAVE 01") || find_text("WAVE 01")->y == 0);
}

/* ---- tests: sound ---- */

#define MAX_PSG_WRITES 400

static struct PsgWrite {
    int tick;
    int reg;
    int value;
} psg_writes[MAX_PSG_WRITES];
static int psg_count;
static int psg_tick;
static int psg_state[16];   /* current value of each register as the chip would hold it */
static int mixer_always_has_ports;

static void psg_capture(uint8_t reg, uint8_t value) {
    if (psg_count < MAX_PSG_WRITES) {
        psg_writes[psg_count].tick = psg_tick;
        psg_writes[psg_count].reg = reg;
        psg_writes[psg_count].value = value;
    }
    ++psg_count;
    psg_state[reg & 15] = value;
    if (reg == 7 && (value & 0xc0) != 0xc0) {
        mixer_always_has_ports = 0;   /* bits 6 and 7 must stay set on the ST */
    }
}

static void psg_start(void) {
    psg_count = 0;
    psg_tick = 0;
    mixer_always_has_ports = 1;
    memset(psg_state, 0, sizeof(psg_state));
    sound_init(psg_capture);
}

static void psg_ticks(int count) {
    int index;

    for (index = 0; index < count; ++index) {
        sound_tick();
        ++psg_tick;
    }
}

static void test_sound_engine(void) {
    int tick;

    /* init leaves the chip silent with the port bits set */
    psg_start();
    CHECK(psg_state[8] == 0 && psg_state[9] == 0 && psg_state[10] == 0);
    CHECK(psg_state[7] == 0xff);

    /* nothing playing: ticks write nothing new */
    {
        const int before = psg_count;
        psg_ticks(5);
        CHECK(psg_count - before <= 1);   /* at most the mixer settling */
        CHECK((psg_state[7] & 0x3f) == 0x3f);
    }

    /* shot: a soft pew, voice A tone sweeping down in pitch (period rises 12 per frame), volume 9 falling to 0 */
    psg_start();
    sound_play(SFX_SHOOT);
    psg_ticks(1);
    CHECK(psg_state[0] == 150 && psg_state[1] == 0);
    CHECK(psg_state[8] == 9);
    CHECK((psg_state[7] & 0x01) == 0);   /* tone A on */
    CHECK((psg_state[7] & 0x08) != 0);   /* noise A off */
    psg_ticks(5);
    CHECK(psg_state[0] == 150 + 12 * 5);
    CHECK(psg_state[8] == 9 - 5);
    psg_ticks(20);
    CHECK(psg_state[8] == 0);
    CHECK((psg_state[7] & 0x3f) == 0x3f);   /* everything switched off again */

    /* explosions: noise on voice B, no tone; a bigger one is deeper and longer */
    psg_start();
    sound_play(SFX_EXPLODE_LARGE);
    psg_ticks(1);
    CHECK(psg_state[9] == 15);
    CHECK(psg_state[6] == 28);
    CHECK((psg_state[7] & 0x02) != 0);   /* tone B off */
    CHECK((psg_state[7] & 0x10) == 0);   /* noise B on */
    psg_ticks(40);
    CHECK(psg_state[9] == 0);
    psg_start();
    sound_play(SFX_EXPLODE_SMALL);
    psg_ticks(1);
    CHECK(psg_state[6] == 12);
    psg_ticks(20);
    CHECK(psg_state[9] == 0);

    /* an arpeggio steps through its notes */
    psg_start();
    sound_play(SFX_POWERUP);
    psg_ticks(1);
    CHECK(psg_state[0] == 239);
    psg_ticks(3);
    CHECK(psg_state[0] == 190);
    psg_ticks(3);
    CHECK(psg_state[0] == 159);
    psg_ticks(3);
    CHECK(psg_state[0] == 119);
    psg_ticks(4);
    CHECK(psg_state[8] == 0);

    /* ship death plays a tone and noise together */
    psg_start();
    sound_play(SFX_SHIP_DEATH);
    psg_ticks(1);
    CHECK(psg_state[8] > 0 && psg_state[9] > 0);

    /* a new effect replaces one already playing on the same voice */
    psg_start();
    sound_play(SFX_SHOOT);
    psg_ticks(4);
    sound_play(SFX_HYPERSPACE);
    psg_ticks(1);
    CHECK(psg_state[0] == (400 & 0xff) && psg_state[1] == (400 >> 8));

    /* looping sounds: thrust rumble on voice C, UFO warble when there is no thrust */
    psg_start();
    sound_set_thrust(1);
    psg_ticks(3);
    CHECK(psg_state[10] == 6);
    CHECK((psg_state[7] & 0x20) == 0);   /* noise C on */
    CHECK(psg_state[6] == 26);
    sound_set_thrust(0);
    psg_ticks(1);
    CHECK(psg_state[10] == 0);

    psg_start();
    sound_set_ufo(1);
    psg_ticks(1);
    CHECK((psg_state[7] & 0x04) == 0);   /* tone C on */
    {
        const int first = psg_state[4];
        int changed = 0;
        for (tick = 0; tick < 12; ++tick) {
            psg_ticks(1);
            if (psg_state[4] != first) {
                changed = 1;
            }
        }
        CHECK(changed);   /* it warbles */
    }
    sound_set_thrust(1);
    psg_ticks(1);
    CHECK((psg_state[7] & 0x04) != 0);   /* thrust takes over voice C */
    sound_set_thrust(0);
    sound_set_ufo(0);
    psg_ticks(1);
    CHECK(psg_state[10] == 0);

    /* silence stops everything, and the port bits are never touched */
    psg_start();
    sound_play(SFX_GAME_OVER);
    sound_set_thrust(1);
    psg_ticks(3);
    sound_silence();
    CHECK(psg_state[8] == 0 && psg_state[9] == 0 && psg_state[10] == 0);
    CHECK(psg_state[7] == 0xff);
    psg_ticks(5);
    CHECK(psg_state[8] == 0 && psg_state[10] == 0);
    CHECK(mixer_always_has_ports);

    /* unknown ids are ignored */
    sound_play(-3);
    sound_play(SFX_COUNT + 4);
    psg_ticks(2);
    CHECK(mixer_always_has_ports);

    sound_init(NULL);   /* the tests must not leave a callback behind */
}

static int events_contain(uint16_t events, int sfx) {
    return (events & (1u << sfx)) != 0;
}

static void test_game_sound_events(void) {
    GameState state;
    GameInput fire = {0, 0, 0, 1, 0, 0, 0, 0};
    GameInput hyper = {0, 0, 0, 0, 1, 0, 0, 0};
    uint16_t events;
    int size;

    /* starting a game announces the wave */
    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    game_start(&state);
    events = game_take_sound_events(&state);
    CHECK(events_contain(events, SFX_WAVE_START));
    CHECK(game_take_sound_events(&state) == 0);   /* taking clears them */

    /* firing */
    init_playing(&state);
    clear_field(&state);
    (void) game_take_sound_events(&state);
    state.ship.invulnerability = 255;
    game_step(&state, &fire);
    events = game_take_sound_events(&state);
    CHECK(events_contain(events, SFX_SHOOT));

    /* each rock size has its own explosion */
    for (size = GAME_ASTEROID_LARGE; size >= GAME_ASTEROID_SMALL; --size) {
        const int expected = (size == 3) ? SFX_EXPLODE_LARGE : (size == 2) ? SFX_EXPLODE_MEDIUM : SFX_EXPLODE_SMALL;

        clear_field(&state);
        state.asteroids[1].active = 1;
        state.asteroids[1].size = (uint8_t) size;
        state.asteroids[1].point_count = 8;
        state.asteroids[1].x = 100L << GAME_FIX_SHIFT;
        state.asteroids[1].y = 50L << GAME_FIX_SHIFT;
        state.bullets[0].active = 1;
        state.bullets[0].life = 4;
        state.bullets[0].x = state.asteroids[1].x;
        state.bullets[0].y = state.asteroids[1].y;
        (void) game_take_sound_events(&state);
        game_step(&state, &no_input);
        events = game_take_sound_events(&state);
        CHECK(events_contain(events, expected));
        CHECK(!events_contain(events, SFX_SHIP_DEATH));
    }

    /* hyperspace */
    init_playing(&state);
    (void) game_take_sound_events(&state);
    game_step(&state, &hyper);
    CHECK(events_contain(game_take_sound_events(&state), SFX_HYPERSPACE));

    /* extra life */
    init_playing(&state);
    clear_field(&state);
    state.score = 9990;
    state.asteroids[1].active = 1;
    state.asteroids[1].size = GAME_ASTEROID_LARGE;
    state.asteroids[1].point_count = 8;
    state.asteroids[1].x = 100L << GAME_FIX_SHIFT;
    state.asteroids[1].y = 50L << GAME_FIX_SHIFT;
    state.bullets[0].active = 1;
    state.bullets[0].life = 4;
    state.bullets[0].x = state.asteroids[1].x;
    state.bullets[0].y = state.asteroids[1].y;
    (void) game_take_sound_events(&state);
    game_step(&state, &no_input);
    CHECK(events_contain(game_take_sound_events(&state), SFX_EXTRA_LIFE));

    /* losing a life, then the last life */
    init_playing(&state);
    clear_field(&state);
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);
    state.ship.invulnerability = 0;
    (void) game_take_sound_events(&state);
    game_step(&state, &no_input);
    events = game_take_sound_events(&state);
    CHECK(events_contain(events, SFX_SHIP_DEATH));
    CHECK(!events_contain(events, SFX_GAME_OVER));

    clear_field(&state);
    state.lives = 1;
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);
    state.ship.invulnerability = 0;
    game_step(&state, &no_input);
    events = game_take_sound_events(&state);
    CHECK(events_contain(events, SFX_SHIP_DEATH));
    CHECK(events_contain(events, SFX_GAME_OVER));
}

/* Every effect the game triggers must be a real effect id, and the chip's port bits must survive. */
static void test_sound_ids_are_valid(void) {
    GameState state;
    int frame;
    uint16_t seen = 0;

    init_playing(&state);
    psg_start();
    for (frame = 0; frame < 3000; ++frame) {
        GameInput input = {0, 0, 0, 0, 0, 0, 0, 0};
        input.thrust = (uint8_t) ((frame / 30) & 1);
        input.left = (uint8_t) ((frame / 17) & 1);
        input.fire = (uint8_t) ((frame / 5) & 1);
        input.hyperspace = (uint8_t) (frame % 211 == 0);
        if (state.mode != GAME_MODE_PLAYING) {
            game_start(&state);
        }
        game_step(&state, &input);
        {
            const uint16_t events = game_take_sound_events(&state);
            int sfx;

            seen = (uint16_t) (seen | events);
            for (sfx = 1; sfx < SFX_COUNT; ++sfx) {
                if (events & (1u << sfx)) {
                    sound_play(sfx);
                }
            }
            CHECK((events & 1u) == 0);   /* bit 0 (SFX_NONE) is never used */
            CHECK((events >> SFX_COUNT) == 0);
        }
        sound_set_thrust(state.ship.thrusting);
        sound_tick();
    }
    CHECK(events_contain(seen, SFX_SHOOT));
    CHECK(events_contain(seen, SFX_EXPLODE_LARGE));
    CHECK(mixer_always_has_ports);
    sound_init(NULL);
}

static int events_contain_bit(uint16_t events, int sfx) {
    return (events & (1u << sfx)) != 0;
}

/* ---- tests: power-ups and stars ---- */

static void place_powerup_on_ship(GameState *state, int type) {
    memset(state->powerups, 0, sizeof(state->powerups));
    state->powerups[0].active = 1;
    state->powerups[0].type = (uint8_t) type;
    state->powerups[0].x = state->ship.x;
    state->powerups[0].y = state->ship.y;
    state->powerups[0].life = 100;
}

static void put_bullet_on_new_large_rock(GameState *state) {
    clear_field(state);
    state->asteroids[1].active = 1;
    state->asteroids[1].size = GAME_ASTEROID_LARGE;
    state->asteroids[1].point_count = 8;
    state->asteroids[1].x = 100L << GAME_FIX_SHIFT;
    state->asteroids[1].y = 50L << GAME_FIX_SHIFT;
    state->bullets[0].active = 1;
    state->bullets[0].life = 4;
    state->bullets[0].x = state->asteroids[1].x;
    state->bullets[0].y = state->asteroids[1].y;
}

static int active_powerups(const GameState *state) {
    int index;
    int count = 0;

    for (index = 0; index < GAME_MAX_POWERUPS; ++index) {
        count += state->powerups[index].active;
    }
    return count;
}

static void test_powerup_drops(void) {
    GameState state;
    int trial;
    int drops = 0;
    int types[GAME_POWERUP_TYPES] = {0, 0, 0, 0};

    init_playing(&state);
    for (trial = 0; trial < 800; ++trial) {
        int index;

        memset(state.powerups, 0, sizeof(state.powerups));
        state.drop_cooldown = 0;
        state.pity_timer = 0;
        state.life_drops_this_wave = 0;
        put_bullet_on_new_large_rock(&state);
        state.ship.invulnerability = 255;
        game_step(&state, &no_input);
        for (index = 0; index < GAME_MAX_POWERUPS; ++index) {
            if (state.powerups[index].active) {
                ++drops;
                CHECK(state.powerups[index].type < GAME_POWERUP_TYPES);
                ++types[state.powerups[index].type];
                /* it appears where the rock was, drifting slowly */
                CHECK(state.powerups[index].life > 240);
                CHECK(state.powerups[index].vx > -8000 && state.powerups[index].vx < 8000);
            }
        }
        state.score = 0;
    }
    /* Lovable: 8% of destroyed large rocks drop one (800 kills: expect about 64) */
    CHECK(drops > 40 && drops < 95);
    CHECK(types[GAME_POWERUP_SHIELD] > 10);
    CHECK(types[GAME_POWERUP_RAPID_FIRE] > 8);
    CHECK(types[GAME_POWERUP_MULTIPLIER] > 8);
    CHECK(types[GAME_POWERUP_EXTRA_LIFE] <= 800 / 100);   /* rare: only with three lives or fewer, at most one a wave */
}

/* Drops by source and the rules around them: a cooldown, a limit on screen, a guarantee after a dry spell,
   and extra lives only when they are needed. */
static void test_powerup_economy(void) {
    GameState state;
    int trial;
    int drops;
    int counts[6];
    static const int sources[6] = {0, 1, 2, 3, 4, 5};   /* large, medium, small rock; large UFO, small UFO, alien */
    static const int enemy_kind[6] = {0, 0, 0, GAME_ENEMY_UFO_LARGE, GAME_ENEMY_UFO_SMALL, GAME_ENEMY_BROWN};
    int source;

    for (source = 0; source < 6; ++source) {
        drops = 0;
        for (trial = 0; trial < 1000; ++trial) {
            init_playing(&state);
            clear_field(&state);
            state.ship.invulnerability = 255;
            memset(state.powerups, 0, sizeof(state.powerups));
            if (sources[source] < 3) {
                const uint8_t size = (uint8_t) (GAME_ASTEROID_LARGE - sources[source]);
                state.asteroids[1].active = 1;
                state.asteroids[1].size = size;
                state.asteroids[1].point_count = 8;
                state.asteroids[1].x = 100L << GAME_FIX_SHIFT;
                state.asteroids[1].y = 50L << GAME_FIX_SHIFT;
                state.bullets[0].active = 1;
                state.bullets[0].life = 4;
                state.bullets[0].x = state.asteroids[1].x;
                state.bullets[0].y = state.asteroids[1].y;
            } else {
                game_spawn_enemy_now(&state, enemy_kind[source]);
                shoot_enemy(&state, first_enemy(&state, enemy_kind[source]));
            }
            state.rng_state = (uint32_t) trial * 2654435761u + (uint32_t) source * 40503u + 1u;
            game_step(&state, &no_input);
            drops += active_powerups(&state) > 0;
        }
        counts[source] = drops;
    }
    CHECK(counts[0] > 40 && counts[0] < 130);       /* 8% */
    CHECK(counts[1] > 15 && counts[1] < 75);        /* 4% */
    CHECK(counts[2] == 0);                          /* small rocks never */
    CHECK(counts[3] > 230 && counts[3] < 380);      /* 30% */
    CHECK(counts[4] > 430 && counts[4] < 570);      /* 50% */
    CHECK(counts[5] > 190 && counts[5] < 310);      /* 25% */

    /* after a drop there is a cooldown (5 s) before the next */
    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    state.drop_cooldown = 100;
    state.pity_timer = 5000;
    put_bullet_on_new_large_rock(&state);
    game_step(&state, &no_input);
    CHECK(active_powerups(&state) == 0);

    /* never more than two on the screen */
    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    state.powerups[0].active = 1;
    state.powerups[0].life = 300;
    state.powerups[0].x = 10L << GAME_FIX_SHIFT;
    state.powerups[0].y = 10L << GAME_FIX_SHIFT;
    state.powerups[1] = state.powerups[0];
    state.pity_timer = 5000;
    put_bullet_on_new_large_rock(&state);
    game_step(&state, &no_input);
    CHECK(active_powerups(&state) == 2);

    /* 30 s without a drop guarantees one from any rock but a small one */
    for (trial = 0; trial < 50; ++trial) {
        init_playing(&state);
        clear_field(&state);
        state.ship.invulnerability = 255;
        state.pity_timer = 1500;
        state.rng_state += (uint32_t) trial * 7919u;
        put_bullet_on_new_large_rock(&state);
        game_step(&state, &no_input);
        CHECK(active_powerups(&state) == 1);
        CHECK(state.drop_cooldown > 0 && state.pity_timer < 5);
    }

    /* extra lives drop only with three lives or fewer */
    {
        int with_few = 0;
        int with_many = 0;

        for (trial = 0; trial < 300; ++trial) {
            int pass;

            for (pass = 0; pass < 2; ++pass) {
                init_playing(&state);
                clear_field(&state);
                state.ship.invulnerability = 255;
                state.lives = (uint8_t) (pass ? 4 : 1);
                state.pity_timer = 1500;
                state.rng_state += (uint32_t) trial * 104729u + (uint32_t) pass;
                put_bullet_on_new_large_rock(&state);
                game_step(&state, &no_input);
                if (state.powerups[0].active && state.powerups[0].type == GAME_POWERUP_EXTRA_LIFE) {
                    if (pass) {
                        ++with_many;
                    } else {
                        ++with_few;
                    }
                }
            }
        }
        CHECK(with_many == 0);
        CHECK(with_few > 25 && with_few < 70);   /* weight 12 of 112 with one life left: about 11% of 300 */
    }

    /* and not twice in a wave */
    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    state.lives = 1;
    state.life_drops_this_wave = 1;
    for (trial = 0; trial < 100; ++trial) {
        memset(state.powerups, 0, sizeof(state.powerups));
        state.drop_cooldown = 0;
        state.pity_timer = 1500;
        put_bullet_on_new_large_rock(&state);
        game_step(&state, &no_input);
        CHECK(!(state.powerups[0].active && state.powerups[0].type == GAME_POWERUP_EXTRA_LIFE));
    }
}

static void test_powerup_expires_and_wraps(void) {
    GameState state;
    int frame;

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    memset(state.powerups, 0, sizeof(state.powerups));
    state.powerups[0].active = 1;
    state.powerups[0].type = GAME_POWERUP_SHIELD;
    state.powerups[0].x = 319L << GAME_FIX_SHIFT;
    state.powerups[0].y = 5L << GAME_FIX_SHIFT;
    state.powerups[0].vx = 20000;
    state.powerups[0].vy = -20000;
    state.powerups[0].life = 250;

    for (frame = 0; frame < 249; ++frame) {
        game_step(&state, &no_input);
        CHECK(state.powerups[0].x >= 0 && state.powerups[0].x < (320L << GAME_FIX_SHIFT));
        CHECK(state.powerups[0].y >= 0 && state.powerups[0].y < (240L << GAME_FIX_SHIFT));
    }
    CHECK(state.powerups[0].active);
    game_step(&state, &no_input);
    CHECK(!state.powerups[0].active);   /* gone after 250 frames (5 s) */
    CHECK(state.shield_timer == 0);     /* never collected */
}

static void test_powerup_shield(void) {
    GameState state;
    int frame;

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 0;
    place_powerup_on_ship(&state, GAME_POWERUP_SHIELD);
    (void) game_take_sound_events(&state);
    game_step(&state, &no_input);
    CHECK(!state.powerups[0].active);
    CHECK(state.shield_timer == 349);   /* 7 seconds */
    CHECK(state.score == 150);          /* and every pick-up pays a bonus */
    CHECK(events_contain_bit(game_take_sound_events(&state), SFX_POWERUP));

    /* rocks pass through the shielded ship... */
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);
    for (frame = 0; frame < 100; ++frame) {
        game_step(&state, &no_input);
    }
    CHECK(state.lives == 3);
    CHECK(state.shield_timer == 249);

    /* ...until it runs out (350 frames, 7 s) */
    for (frame = 0; frame < 249; ++frame) {
        state.asteroids[1].x = state.ship.x;
        state.asteroids[1].y = state.ship.y;
        state.asteroids[1].active = 1;
        game_step(&state, &no_input);
    }
    CHECK(state.shield_timer == 0);
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);
    game_step(&state, &no_input);
    CHECK(state.lives == 2);
}

static void test_powerup_rapid_fire(void) {
    GameState state;
    GameInput fire = {0, 0, 0, 1, 0, 0, 0, 0};
    int normal_shots = 0;
    int rapid_shots = 0;
    int frame;

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    for (frame = 0; frame < 120; ++frame) {
        const int before = state.ship.cooldown;
        game_step(&state, &fire);
        if (state.ship.cooldown > before || (before == 0 && state.ship.cooldown > 0)) {
            ++normal_shots;
        }
    }

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    place_powerup_on_ship(&state, GAME_POWERUP_RAPID_FIRE);
    game_step(&state, &no_input);
    CHECK(state.rapid_timer == 499);
    for (frame = 0; frame < 120; ++frame) {
        const int before = state.ship.cooldown;
        game_step(&state, &fire);
        if (state.ship.cooldown > before || (before == 0 && state.ship.cooldown > 0)) {
            ++rapid_shots;
        }
    }
    /* every 12 frames normally (10), every 5 with rapid fire (24) */
    CHECK(normal_shots >= 9 && normal_shots <= 11);
    CHECK(rapid_shots >= 22 && rapid_shots <= 25);
    CHECK(state.rapid_timer < 400);
}

static void test_powerup_multiplier_and_life(void) {
    GameState state;

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    place_powerup_on_ship(&state, GAME_POWERUP_MULTIPLIER);
    game_step(&state, &no_input);
    CHECK(state.multiplier_timer == 749);
    put_bullet_on_new_large_rock(&state);
    state.score = 0;
    game_step(&state, &no_input);
    CHECK(state.score == 40);   /* 20 x 2 */

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    place_powerup_on_ship(&state, GAME_POWERUP_EXTRA_LIFE);
    (void) game_take_sound_events(&state);
    game_step(&state, &no_input);
    CHECK(state.lives == 4);
    CHECK(events_contain_bit(game_take_sound_events(&state), SFX_EXTRA_LIFE));

    state.lives = 9;
    place_powerup_on_ship(&state, GAME_POWERUP_EXTRA_LIFE);
    game_step(&state, &no_input);
    CHECK(state.lives == 9);   /* capped */

    /* a new game clears every power-up */
    state.shield_timer = 100;
    state.rapid_timer = 100;
    state.multiplier_timer = 100;
    place_powerup_on_ship(&state, GAME_POWERUP_SHIELD);
    game_start(&state);
    CHECK(state.shield_timer == 0 && state.rapid_timer == 0 && state.multiplier_timer == 0);
    CHECK(active_powerups(&state) == 0);
}

static void test_powerup_render_and_hud(void) {
    GameState state;
    int type;

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 0;
    state.ship.x = 20L << GAME_FIX_SHIFT;
    state.ship.y = 20L << GAME_FIX_SHIFT;
    for (type = 0; type < GAME_POWERUP_TYPES; ++type) {
        const int expected_color = (type == 0) ? GAME_COLOR_SHIP : (type == 1) ? GAME_COLOR_RED
                                   : (type == 2) ? GAME_COLOR_MAGENTA : GAME_COLOR_YELLOW;
        int base;

        memset(state.powerups, 0, sizeof(state.powerups));
        state.powerups[0].active = 1;
        state.powerups[0].type = (uint8_t) type;
        state.powerups[0].x = 200L << GAME_FIX_SHIFT;
        state.powerups[0].y = 150L << GAME_FIX_SHIFT;
        state.powerups[0].life = 200;
        render(&state);
        /* octagon (8) plus a symbol: a square (4), two bars, a plus or a cross (2 each) */
        base = (type == GAME_POWERUP_SHIELD) ? 12 : 10;
        CHECK(color_counts[expected_color] >= base);
        CHECK(rect_count >= 3);
    }

    /* it blinks when about to vanish */
    memset(state.powerups, 0, sizeof(state.powerups));
    state.powerups[0].active = 1;
    state.powerups[0].type = GAME_POWERUP_MULTIPLIER;
    state.powerups[0].x = 200L << GAME_FIX_SHIFT;
    state.powerups[0].y = 150L << GAME_FIX_SHIFT;
    state.powerups[0].life = 40;
    render(&state);
    {
        const int visible = color_counts[GAME_COLOR_YELLOW];
        state.powerups[0].life = 44;
        render(&state);
        CHECK((visible == 0) != (color_counts[GAME_COLOR_YELLOW] == 0));
    }

    /* the HUD counts the effects down in seconds */
    memset(state.powerups, 0, sizeof(state.powerups));
    state.shield_timer = 250;
    state.rapid_timer = 51;
    state.multiplier_timer = 0;
    state.hud_refresh = 2;
    render(&state);
    CHECK(has_text("S05"));
    CHECK(has_text("R02"));
    {
        const TextCall *shield = find_text("S05");
        CHECK(shield != NULL && shield->y == 8 && shield->x == 224 && shield->bg == GAME_COLOR_FRAME);
    }
    render(&state);
    render(&state);
    state.shield_timer = 200;
    render(&state);
    CHECK(has_text("S04"));

    /* the shield is drawn as a ring around the ship */
    state.shield_timer = 200;
    state.ship.invulnerability = 0;
    render(&state);
    CHECK(color_counts[GAME_COLOR_SHIP] >= 4 + 8);
}

static int expected_star_x(const GameState *state, int index) {
    /* the same mapping the game uses: field origin + world x * x_scale */
    return FIELD_X + (int) (((long) (state->stars[index].x >> 11) * state->x_scale) >> 13);
}

static void test_stars(void) {
    GameState state;
    int index;
    int layer_counts[3] = {0, 0, 0};
    int32_t before[GAME_STAR_COUNT];
    int frame;

    init_playing(&state);
    for (index = 0; index < GAME_STAR_COUNT; ++index) {
        CHECK(state.stars[index].x >= 0 && state.stars[index].x < (320L << GAME_FIX_SHIFT));
        CHECK(state.stars[index].layer < 3);
        CHECK(state.stars[index].layer == index / 6);   /* grouped by layer, 6 each */
        ++layer_counts[state.stars[index].layer];
        before[index] = state.stars[index].x;
        CHECK(state.star_points[index * 2] >= FIELD_X && state.star_points[index * 2] < FIELD_X + FIELD_W);
        CHECK(state.star_points[index * 2 + 1] >= FIELD_Y && state.star_points[index * 2 + 1] < FIELD_Y + FIELD_H);
    }
    CHECK(layer_counts[0] == 6 && layer_counts[1] == 6 && layer_counts[2] == 6);

    /* they drift left, the near ones faster, and wrap round */
    clear_field(&state);
    state.ship.invulnerability = 255;
    for (frame = 0; frame < 100; ++frame) {
        game_step(&state, &no_input);
    }
    for (index = 0; index < GAME_STAR_COUNT; ++index) {
        const int32_t moved = before[index] - state.stars[index].x;
        const int layer = state.stars[index].layer;
        const int32_t speed = (layer == 0) ? 524 : (layer == 1) ? 1573 : 3146;

        if (moved < 0) {
            continue;   /* wrapped */
        }
        CHECK(moved == 100 * speed || moved == 96 * speed || moved == 104 * speed);   /* every fourth frame, four steps at once */
        /* the stored screen pixel follows the star (to within the eighth-of-a-pixel it updates by) */
        {
            const int difference = expected_star_x(&state, index) - state.star_points[index * 2];
            CHECK(difference >= -1 && difference <= 1);
        }
    }
    state.stars[0].x = 100;   /* less than one step's drift from the left edge */
    game_step(&state, &no_input);
    game_step(&state, &no_input);
    game_step(&state, &no_input);
    game_step(&state, &no_input);
    CHECK(state.stars[0].x > (300L << GAME_FIX_SHIFT));   /* wrapped to the right edge */
    CHECK(state.star_points[0] > FIELD_X + FIELD_W - 30);

    /* paused: they stand still */
    {
        GameInput pause = {0, 0, 0, 0, 0, 0, 1, 0};
        int32_t x = state.stars[3].x;
        game_step(&state, &pause);
        game_step(&state, &no_input);
        CHECK(state.stars[3].x == x);
    }
}

static void test_stars_render_and_erase(void) {
    GameState state;
    int layer;
    int index;
    int frame;

    init_playing(&state);
    clear_field(&state);
    state.ship.invulnerability = 255;
    /* a freshly cleared screen gets every star (the first two frames, one per screen buffer) */
    render(&state);
    CHECK(point_calls == 3);
    CHECK(point_count == GAME_STAR_COUNT);
    for (layer = 0; layer < 3; ++layer) {
        CHECK(point_layers[layer] == (layer == 0 ? GAME_COLOR_STAR_DIM : layer == 1 ? GAME_COLOR_GREY : GAME_COLOR_STAR_BRIGHT));
        for (index = 0; index < GAME_STAR_COUNT / 3; ++index) {
            const int star = layer * 6 + index;
            CHECK(points_xy[layer][index * 2] == state.star_points[star * 2]);
            CHECK(points_xy[layer][index * 2 + 1] == state.star_points[star * 2 + 1]);
        }
    }
    render(&state);
    CHECK(point_count == GAME_STAR_COUNT);

    /* after that a quarter of them per frame: over eight frames every star is drawn twice in a row,
       plus two extra frames for any star that moved onto a new pixel */
    {
        int total = 0;
        int frame_index;

        for (frame_index = 0; frame_index < 8; ++frame_index) {
            game_step(&state, &no_input);
            render(&state);
            CHECK(point_count <= 8);
            total += point_count;
        }
        CHECK(total >= 2 * GAME_STAR_COUNT && total <= 2 * GAME_STAR_COUNT + 2 * GAME_STAR_COUNT);
    }

    /* run until a near star moves onto a new pixel */
    {
        const int star = 12;
        const int old_x = state.star_points[star * 2];
        const int old_y = state.star_points[star * 2 + 1];
        int covered_frames = 0;

        for (frame = 0; frame < 200 && state.star_points[star * 2] == old_x; ++frame) {
            game_step(&state, &no_input);
        }
        CHECK(state.star_points[star * 2] != old_x);
        CHECK(state.star_points[star * 2 + 1] == old_y);   /* stars only move sideways */

        /* the pixel it left is erased on the next two frames (one per screen buffer), not more */
        for (frame = 0; frame < 4; ++frame) {
            int found = 0;
            int r;

            render(&state);
            for (r = 0; r < rect_count && r < MAX_RECTS; ++r) {
                if (rects[r][0] <= old_x && rects[r][2] >= old_x && rects[r][1] <= old_y && rects[r][3] >= old_y &&
                    rects[r][2] - rects[r][0] <= 3 && rects[r][3] - rects[r][1] <= 3) {
                    found = 1;
                }
            }
            covered_frames += found;
        }
        CHECK(covered_frames == 2);
    }

    /* stars are only drawn while playing */
    game_init(&state, FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    render(&state);
    CHECK(point_calls == 0);
}

/* ---- tests: UFOs, aliens, bosses ---- */

static GameEnemy *first_enemy(GameState *state, int kind) {
    int index;

    for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
        if (state->enemies[index].active && (kind == 0 || state->enemies[index].kind == kind)) {
            return &state->enemies[index];
        }
    }
    return NULL;
}

static int enemy_count(const GameState *state, int kind) {
    int index;
    int count = 0;

    for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
        count += (state->enemies[index].active && (kind == 0 || state->enemies[index].kind == kind));
    }
    return count;
}

static int enemy_bullet_count(const GameState *state) {
    int index;
    int count = 0;

    for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
        count += state->enemy_bullets[index].active;
    }
    return count;
}

/* A quiet field with a safe ship: one far-away rock and no spawns due for a long while. */
static void quiet_playing(GameState *state, int wave) {
    init_playing(state);
    clear_field(state);
    state->wave = (uint8_t) wave;
    state->ship.invulnerability = 255;
    state->ufo_timer = 30000;
    state->alien_timer = 30000;
    state->alien_budget = 0;
    state->banner_timer = 0;
}

static void keep_ship_safe(GameState *state) {
    state->ship.invulnerability = 255;
}

static void shoot_enemy(GameState *state, GameEnemy *enemy) {
    int index;

    /* bullets wrap round the field, so bring an enemy that is still off the edge inside first */
    if (enemy->x < (5L << 16) || enemy->x > (315L << 16)) {
        enemy->x = 100L << 16;
    }

    for (index = 0; index < GAME_MAX_BULLETS; ++index) {
        if (!state->bullets[index].active) {
            state->bullets[index].active = 1;
            state->bullets[index].life = 4;
            state->bullets[index].x = enemy->x;
            state->bullets[index].y = enemy->y;
            state->bullets[index].vx = 0;
            state->bullets[index].vy = 0;
            return;
        }
    }
}

static void test_ufo_spawn_and_flight(void) {
    GameState state;
    GameEnemy *ufo;
    int frame;
    int shots = 0;
    int crossed = 0;

    quiet_playing(&state, 1);
    state.ufo_timer = 1;
    game_step(&state, &no_input);
    ufo = first_enemy(&state, 0);
    CHECK(ufo != NULL);
    CHECK(ufo->kind == GAME_ENEMY_UFO_LARGE);   /* small UFOs only after wave 9 */
    CHECK(ufo->warn > 90 && !state.ufo_present);   /* it is announced at the edge first */
    end_warnings(&state);
    game_step(&state, &no_input);
    CHECK(state.ufo_present);
    /* enters from a side edge at 1.0-1.5 px/frame (x0.48) */
    CHECK(ufo->x < (1L << 16) || ufo->x > ((320L - 2) << 16));
    CHECK((ufo->vx > 31000 && ufo->vx < 47500) || (ufo->vx < -31000 && ufo->vx > -47500));
    CHECK(state.ufo_timer > 400);   /* the next one is a while away */

    for (frame = 0; frame < 1000; ++frame) {
        keep_ship_safe(&state);
        game_step(&state, &no_input);
        shots += (enemy_bullet_count(&state) > 0);
        if (first_enemy(&state, 0) == NULL) {
            crossed = 1;
            break;
        }
        CHECK(ufo->y > 0 && ufo->y < (240L << 16));   /* wobbles up and down, stays in the field */
    }
    CHECK(crossed);            /* it flies across and leaves */
    CHECK(shots > 10);         /* firing on the way */
    CHECK(!state.ufo_present);
}

static void test_ufo_shots_and_points(void) {
    GameState state;
    GameEnemy *ufo;
    int frame;
    int aimed = 0;

    /* large UFO: 200 points; its shots go anywhere */
    quiet_playing(&state, 1);
    state.ufo_timer = 1;
    game_step(&state, &no_input);
    ufo = first_enemy(&state, 0);
    end_warnings(&state);
    state.score = 0;
    shoot_enemy(&state, ufo);
    game_step(&state, &no_input);
    CHECK(state.score == 200);
    CHECK(enemy_count(&state, 0) == 0);

    /* small UFO (wave 10+): 1000 points, and it aims at the ship */
    quiet_playing(&state, 12);
    for (frame = 0; frame < 80 && !first_enemy(&state, GAME_ENEMY_UFO_SMALL); ++frame) {
        state.ufo_timer = 1;
        game_step(&state, &no_input);
        if (first_enemy(&state, GAME_ENEMY_UFO_LARGE) != NULL) {
            memset(state.enemies, 0, sizeof(state.enemies));   /* keep trying until a small one turns up */
        }
    }
    ufo = first_enemy(&state, GAME_ENEMY_UFO_SMALL);
    CHECK(ufo != NULL);
    end_warnings(&state);
    if (ufo != NULL) {
        ufo->timer = 1;
        ufo->x = 40L << 16;
        ufo->y = 40L << 16;
        state.ship.x = 200L << 16;
        state.ship.y = 180L << 16;
        state.ship.invulnerability = 255;
        game_step(&state, &no_input);
        {
            int index;
            for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
                const GameBullet *bullet = &state.enemy_bullets[index];
                if (bullet->active) {
                    /* aimed down and to the right, towards the ship */
                    aimed = bullet->vx > 0 && bullet->vy > 0;
                    CHECK(bullet->color == GAME_COLOR_RED);
                }
            }
        }
        CHECK(aimed);
        state.score = 0;
        shoot_enemy(&state, first_enemy(&state, GAME_ENEMY_UFO_SMALL));
        game_step(&state, &no_input);
        CHECK(state.score == 1000);
    }
}

/* Run the alien director n times (one event each, with the timer forced due) and report which kinds came. */
static void run_director(GameState *state, int events) {
    int event;

    for (event = 0; event < events; ++event) {
        state->alien_timer = 1;
        state->alien_budget = 9;
        keep_ship_safe(state);
        game_step(state, &no_input);
    }
}

static void test_alien_waves_and_gates(void) {
    GameState state;
    int frame;

    /* nothing before wave 3 */
    quiet_playing(&state, 2);
    run_director(&state, 3);
    CHECK(enemy_count(&state, 0) == 0);

    /* wave 3: brown aliens only, and the first wave's cap is a single unit at a time */
    quiet_playing(&state, 3);
    run_director(&state, 1);
    CHECK(enemy_count(&state, GAME_ENEMY_BROWN) == 1);
    CHECK(enemy_count(&state, GAME_ENEMY_GREEN) == 0);
    run_director(&state, 4);
    CHECK(enemy_count(&state, GAME_ENEMY_BROWN) == 1);   /* one unit out at once before wave 7 */
    CHECK(state.alien_timer == 100);                     /* nothing may come: it looks again soon */

    /* the new alien arrives with a warning: harmless at the edge until it flies in */
    quiet_playing(&state, 3);
    run_director(&state, 1);
    {
        const GameEnemy *brown = first_enemy(&state, GAME_ENEMY_BROWN);
        const long x_before = brown->x;

        CHECK(brown->warn > 90 && brown->warn <= 100);
        for (frame = 0; frame < 50; ++frame) {
            keep_ship_safe(&state);
            game_step(&state, &no_input);
        }
        CHECK(brown->warn > 0 && brown->warn < 60);
        CHECK(brown->x == x_before);                      /* it has not moved */
        shoot_enemy(&state, (GameEnemy *) brown);
        game_step(&state, &no_input);
        CHECK(brown->active);                             /* and cannot be shot */
        for (frame = 0; frame < 60; ++frame) {
            keep_ship_safe(&state);
            game_step(&state, &no_input);
        }
        CHECK(brown->warn == 0);
        CHECK(brown->x != x_before);                      /* now it flies */
    }

    /* two units from wave 7 on: greens appear then, at most one (two from wave 16), blues from wave 11 */
    quiet_playing(&state, 7);
    for (frame = 0; frame < 40; ++frame) {
        run_director(&state, 1);
    }
    CHECK(enemy_count(&state, GAME_ENEMY_GREEN) <= 1);
    CHECK(enemy_count(&state, GAME_ENEMY_BROWN) + enemy_count(&state, GAME_ENEMY_GREEN) <= 2);
    CHECK(enemy_count(&state, GAME_ENEMY_BLUE) == 0);
    CHECK(enemy_count(&state, GAME_ENEMY_PURPLE) == 0);

    quiet_playing(&state, 11);
    {
        int saw_blue = 0;

        for (frame = 0; frame < 200; ++frame) {
            memset(state.enemies, 0, sizeof(state.enemies));
            run_director(&state, 1);
            saw_blue |= enemy_count(&state, GAME_ENEMY_BLUE) > 0;
            CHECK(enemy_count(&state, GAME_ENEMY_PURPLE) == 0);
            CHECK(enemy_count(&state, 0) <= 2);   /* the global cap before wave 12 */
        }
        CHECK(saw_blue);
    }

    quiet_playing(&state, 14);
    {
        int saw_purple = 0;

        for (frame = 0; frame < 200; ++frame) {
            memset(state.enemies, 0, sizeof(state.enemies));
            run_director(&state, 1);
            saw_purple |= enemy_count(&state, GAME_ENEMY_PURPLE) == 3;
        }
        CHECK(saw_purple);
    }

    /* a wave has a budget of events: 2 + wave / 3, at most 7 */
    init_playing(&state);
    CHECK(state.alien_budget == 2);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.wave = 8;
    step_to_next_wave(&state);
    CHECK(state.wave == 9 && state.alien_budget == 5);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.wave = 28;
    step_to_next_wave(&state);
    CHECK(state.wave == 29 && state.alien_budget == 7);
    CHECK(state.alien_timer == 500);   /* the first event is 10 s into the wave */
}

static long distance_between(const GameEnemy *enemy, const GameShip *ship) {
    const long dx = (enemy->x - ship->x) >> 16;
    const long dy = (enemy->y - ship->y) >> 16;

    return dx * dx + dy * dy;
}

static void test_brown_alien_chases(void) {
    GameState state;
    GameEnemy *alien;
    int frame;
    long start_distance;
    int hit = 0;

    quiet_playing(&state, 3);
    game_spawn_enemy_now(&state, GAME_ENEMY_BROWN);
    alien = first_enemy(&state, GAME_ENEMY_BROWN);
    CHECK(alien != NULL);
    start_distance = distance_between(alien, &state.ship);
    for (frame = 0; frame < 200; ++frame) {
        long speed8;
        keep_ship_safe(&state);
        game_step(&state, &no_input);
        speed8 = ((alien->vx >> 8) * (alien->vx >> 8)) + ((alien->vy >> 8) * (alien->vy >> 8));
        CHECK(speed8 <= 410L * 410L);   /* top speed 3 px/frame at 60 Hz, +5% per wave: 405 in 8.8 at wave 3 */
        if (distance_between(alien, &state.ship) < 100) {
            hit = 1;
        }
    }
    CHECK(hit);                                        /* it homes in on the ship */
    CHECK(start_distance > 100);

    /* touching it costs a life; killing it scores 300 */
    quiet_playing(&state, 3);
    game_spawn_enemy_now(&state, GAME_ENEMY_BROWN);
    alien = first_enemy(&state, GAME_ENEMY_BROWN);
    state.ship.invulnerability = 0;
    alien->x = state.ship.x;
    alien->y = state.ship.y;
    game_step(&state, &no_input);
    CHECK(state.lives == 2);

    quiet_playing(&state, 3);
    game_spawn_enemy_now(&state, GAME_ENEMY_BROWN);
    state.score = 0;
    shoot_enemy(&state, first_enemy(&state, GAME_ENEMY_BROWN));
    game_step(&state, &no_input);
    CHECK(state.score == 300);
}

static void test_green_alien_shoots_and_takes_two_hits(void) {
    GameState state;
    GameEnemy *alien;
    int index;
    int aimed_bullets = 0;

    quiet_playing(&state, 6);
    game_spawn_enemy_now(&state, GAME_ENEMY_GREEN);
    alien = first_enemy(&state, GAME_ENEMY_GREEN);
    CHECK(alien != NULL);
    CHECK(alien->hp == 2);
    alien->x = 40L << 16;
    alien->y = 40L << 16;
    alien->vx = 0;
    alien->timer = 1;
    state.ship.x = 200L << 16;
    state.ship.y = 180L << 16;
    game_step(&state, &no_input);
    for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
        const GameBullet *bullet = &state.enemy_bullets[index];
        if (bullet->active && bullet->color == GAME_COLOR_SHIP && bullet->vx > 0 && bullet->vy > 0) {
            ++aimed_bullets;
        }
    }
    CHECK(aimed_bullets == 1);

    state.score = 0;
    shoot_enemy(&state, alien);
    game_step(&state, &no_input);
    CHECK(alien->active && alien->hp == 1);   /* the first hit only damages it */
    CHECK(state.score == 0);
    shoot_enemy(&state, alien);
    game_step(&state, &no_input);
    CHECK(!alien->active);
    CHECK(state.score == 500);
}

static void test_blue_sentinel(void) {
    GameState state;
    GameEnemy *alien;
    int frame;
    int saw_invisible = 0;
    int saw_teleport = 0;
    int max_bullets = 0;

    quiet_playing(&state, 8);
    game_spawn_enemy_now(&state, GAME_ENEMY_BLUE);
    alien = first_enemy(&state, GAME_ENEMY_BLUE);
    CHECK(alien != NULL);
    CHECK(alien->hp == 2);
    {
        const long start_x = alien->x >> 16;
        long last_x = start_x;
        long last_y = alien->y >> 16;

        for (frame = 0; frame < 900; ++frame) {
            const int before = enemy_bullet_count(&state);
            keep_ship_safe(&state);
            game_step(&state, &no_input);
            if (!alien->active) {
                break;
            }
            if (enemy_bullet_count(&state) > before) {
                CHECK(state.enemy_bullets[0].color == GAME_COLOR_ASTEROID_SMALL || enemy_bullet_count(&state) > 0);
            }
            if (enemy_bullet_count(&state) > max_bullets) {
                max_bullets = enemy_bullet_count(&state);
            }
            if (alien->flags & 1) {
                saw_invisible = 1;
                /* cannot be hit while invisible */
                {
                    const int hp = alien->hp;
                    shoot_enemy(&state, alien);
                    keep_ship_safe(&state);
                    game_step(&state, &no_input);
                    if (alien->flags & 1) {   /* (on the frame it turns visible again it can be hit) */
                        CHECK(alien->hp == hp);
                    }
                    memset(state.bullets, 0, sizeof(state.bullets));
                }
            }
            {
                const long x = alien->x >> 16;
                const long y = alien->y >> 16;
                if ((x - last_x > 25 || last_x - x > 25) || (y - last_y > 25 || last_y - y > 25)) {
                    saw_teleport = 1;
                }
                last_x = x;
                last_y = y;
            }
        }
    }
    CHECK(saw_invisible);      /* vanishes after each burst */
    CHECK(saw_teleport);       /* and jumps elsewhere */
    CHECK(max_bullets >= 2);   /* a burst of three shots */
}

static void test_purple_swarm(void) {
    GameState state;
    int frame;
    int index;
    long closest = 1000000;
    long ship_distance_at_end = 0;
    int members = 0;

    quiet_playing(&state, 14);
    game_spawn_enemy_now(&state, GAME_ENEMY_PURPLE);
    CHECK(enemy_count(&state, GAME_ENEMY_PURPLE) == 3);
    for (frame = 0; frame < 300; ++frame) {
        keep_ship_safe(&state);
        game_step(&state, &no_input);
        for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
            int other;
            for (other = index + 1; other < GAME_MAX_ENEMIES; ++other) {
                if (state.enemies[index].active && state.enemies[other].active) {
                    const long dx = (state.enemies[index].x - state.enemies[other].x) >> 16;
                    const long dy = (state.enemies[index].y - state.enemies[other].y) >> 16;
                    if (frame > 60 && dx * dx + dy * dy < closest) {
                        closest = dx * dx + dy * dy;
                    }
                }
            }
        }
    }
    CHECK(closest > 3 * 3);   /* they keep apart from each other */
    for (index = 0; index < GAME_MAX_ENEMIES; ++index) {
        if (state.enemies[index].active) {
            ship_distance_at_end += distance_between(&state.enemies[index], &state.ship);
            ++members;
        }
    }
    CHECK(members >= 2);   /* a straggler that wanders off the edge is allowed to leave, as in Lovable */
    CHECK(ship_distance_at_end / members < 130L * 130L);   /* and gather round the ship */

    /* 100 points each */
    state.score = 0;
    shoot_enemy(&state, first_enemy(&state, GAME_ENEMY_PURPLE));
    game_step(&state, &no_input);
    CHECK(state.score == 100);
}

static void test_enemy_bullets_hurt(void) {
    GameState state;

    quiet_playing(&state, 1);
    state.ship.invulnerability = 0;
    state.enemy_bullets[0].active = 1;
    state.enemy_bullets[0].life = 20;
    state.enemy_bullets[0].color = GAME_COLOR_RED;
    state.enemy_bullets[0].x = state.ship.x;
    state.enemy_bullets[0].y = state.ship.y;
    game_step(&state, &no_input);
    CHECK(state.lives == 2);
    CHECK(enemy_bullet_count(&state) == 0);   /* a lost life clears the shots */

    /* a shield stops them */
    quiet_playing(&state, 1);
    state.ship.invulnerability = 0;
    state.shield_timer = 100;
    state.enemy_bullets[0].active = 1;
    state.enemy_bullets[0].life = 20;
    state.enemy_bullets[0].x = state.ship.x;
    state.enemy_bullets[0].y = state.ship.y;
    game_step(&state, &no_input);
    CHECK(state.lives == 3);

    /* shots fly and expire */
    quiet_playing(&state, 1);
    state.enemy_bullets[0].active = 1;
    state.enemy_bullets[0].life = 50;
    state.enemy_bullets[0].x = 10L << 16;
    state.enemy_bullets[0].y = 10L << 16;
    state.enemy_bullets[0].vx = 100000;
    state.enemy_bullets[0].vy = 0;
    game_step(&state, &no_input);
    CHECK(state.enemy_bullets[0].x > (11L << 16));
    {
        int frame;
        for (frame = 0; frame < 49; ++frame) {
            keep_ship_safe(&state);
            game_step(&state, &no_input);
        }
        CHECK(!state.enemy_bullets[0].active);
    }
}

static void test_wave_waits_for_enemies(void) {
    GameState state;

    quiet_playing(&state, 1);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.ufo_timer = 1;
    game_step(&state, &no_input);
    /* the rocks are gone but a UFO is about: the wave is not over */
    CHECK(state.wave == 1);
    CHECK(enemy_count(&state, 0) == 1);
    memset(state.enemies, 0, sizeof(state.enemies));
    step_to_next_wave(&state);
    CHECK(state.wave == 2);
}

static void test_boss_waves(void) {
    static const int expected_hp[] = {0, 0, 0, 0, 0, 14, 0, 0, 0, 0, 19, 0, 0, 0, 0, 18, 0, 0, 0, 0, 41};
    GameState state;
    int wave;
    int frame;

    for (wave = 5; wave <= 20; wave += 5) {
        quiet_playing(&state, wave - 1);
        memset(state.asteroids, 0, sizeof(state.asteroids));
        step_to_next_wave(&state);
        CHECK(state.wave == wave);
        CHECK(state.boss.active);
        CHECK(state.boss.kind == 1 + (wave / 5 - 1) % 4);
        CHECK(state.boss.hp == expected_hp[wave]);
        CHECK(state.boss.max_hp == expected_hp[wave]);
        CHECK(count_asteroids(&state, 0) == 0);   /* boss waves have no rocks */
        CHECK(state.banner_timer > 0);

        /* it flies in from the top, then fights */
        CHECK(state.boss.y < 0);
        state.ufo_timer = 30000;
        state.alien_timer = 30000;
        for (frame = 0; frame < 500; ++frame) {
            keep_ship_safe(&state);
            game_step(&state, &no_input);
            state.ufo_timer = 30000;
            state.alien_timer = 30000;
        }
        CHECK(state.boss.entered);
        CHECK(state.boss.x > 0 && state.boss.x < (320L << 16));
        CHECK(state.boss.y > 0 && state.boss.y < (240L << 16));

        /* it shoots */
        {
            int shots = 0;
            for (frame = 0; frame < 400; ++frame) {
                keep_ship_safe(&state);
                game_step(&state, &no_input);
                state.ufo_timer = 30000;
                state.alien_timer = 30000;
                shots += (enemy_bullet_count(&state) > 0);
            }
            CHECK(shots > 20);
        }

        /* hits wear it down; the last one wins the wave */
        state.score = 0;
        memset(state.bullets, 0, sizeof(state.bullets));
        {
            int hits = 0;
            while (state.boss.active && hits < 100) {
                memset(state.enemies, 0, sizeof(state.enemies));   /* a minion on top of the boss would take the shot */
                state.bullets[0].active = 1;
                state.bullets[0].life = 4;
                state.bullets[0].x = state.boss.x;
                state.bullets[0].y = state.boss.y;
                state.bullets[0].vx = 0;
                state.bullets[0].vy = 0;
                keep_ship_safe(&state);
                game_step(&state, &no_input);
                state.ufo_timer = 30000;
                ++hits;
                if (state.boss.active && state.boss.kind != GAME_BOSS_BORG_CUBE) {
                    CHECK(state.boss.hp == expected_hp[wave] - hits);
                }
            }
            CHECK(!state.boss.active);
            CHECK(state.score >= 2000u + 200u * (uint32_t) wave);
            CHECK(state.score <= 2000u + 200u * (uint32_t) wave + 200u);   /* nothing else scored */
            CHECK(active_powerups(&state) >= 1);       /* it drops a reward */
            CHECK(enemy_count(&state, 0) == 0);
            CHECK(enemy_bullet_count(&state) == 0);
        }
        game_step(&state, &no_input);
        CHECK(state.wave == wave + 1);
        CHECK(count_asteroids(&state, GAME_ASTEROID_LARGE) > 0);
    }
}

static void test_boss_hurts_the_ship(void) {
    GameState state;

    quiet_playing(&state, 4);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    step_to_next_wave(&state);
    CHECK(state.boss.active);
    state.boss.entered = 1;
    state.boss.x = state.ship.x;
    state.boss.y = state.ship.y;
    state.ship.invulnerability = 0;
    game_step(&state, &no_input);
    CHECK(state.lives == 2);
}

static void test_boss_hit_flash_and_sound(void) {
    GameState state;

    quiet_playing(&state, 4);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    step_to_next_wave(&state);
    state.boss.entered = 1;
    state.boss.x = 100L << 16;
    state.boss.y = 60L << 16;
    state.bullets[0].active = 1;
    state.bullets[0].life = 4;
    state.bullets[0].x = state.boss.x;
    state.bullets[0].y = state.boss.y;
    (void) game_take_sound_events(&state);
    game_step(&state, &no_input);
    CHECK(state.boss.flash > 0);
    CHECK(events_contain_bit(game_take_sound_events(&state), SFX_BOSS_HIT));
}


/* ---- tests: the Lovable balance pass ---- */

static void test_wave_clear_bonus_and_position(void) {
    GameState state;
    int index;

    /* clearing a wave pays wave x 100, and 500 more if no life was lost; the multiplier does not double it */
    quiet_playing(&state, 7);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.multiplier_timer = 500;
    state.score = 0;
    game_step(&state, &no_input);
    CHECK(state.clear_bonus == 1200 && state.score == 1200 && state.wave_clear_timer == 75);

    quiet_playing(&state, 7);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.life_lost_this_wave = 1;
    state.score = 0;
    game_step(&state, &no_input);
    CHECK(state.clear_bonus == 700 && state.score == 700);

    quiet_playing(&state, 26);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.score = 0;
    game_step(&state, &no_input);
    CHECK(state.clear_bonus == 2500);   /* capped at wave 20 */

    /* during the pause nothing new turns up, and the wave only starts when it is over */
    quiet_playing(&state, 7);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    game_step(&state, &no_input);
    state.ufo_timer = 1;
    state.alien_timer = 1;
    state.alien_budget = 5;
    for (index = 0; index < 60; ++index) {
        game_step(&state, &no_input);
    }
    CHECK(state.wave == 7 && enemy_count(&state, 0) == 0);

    /* the ship keeps its place and speed into the next wave, the new rocks keep away from it */
    quiet_playing(&state, 6);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.ship.x = 40L << 16;
    state.ship.y = 30L << 16;
    state.ship.vx = 20000;
    state.ship.vy = -10000;
    for (index = 0; index < 120 && state.wave == 6; ++index) {
        state.ship.x = 40L << 16;
        state.ship.y = 30L << 16;
        game_step(&state, &no_input);
    }
    CHECK(state.wave == 7);
    CHECK(state.ship.x == (40L << 16) + 20000 * 0 || state.ship.x > (39L << 16));
    CHECK(state.ship.y < (60L << 16));
    CHECK(count_asteroids(&state, GAME_ASTEROID_LARGE) == 18);
    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        if (state.asteroids[index].active) {
            long dx = (state.asteroids[index].x - state.ship.x) >> 16;
            long dy = (state.asteroids[index].y - state.ship.y) >> 16;
            if (dx > 160) dx -= 320;
            if (dx < -160) dx += 320;
            if (dy > 120) dy -= 240;
            if (dy < -120) dy += 240;
            CHECK(dx * dx + dy * dy >= 80L * 80L);   /* 88 px, less what the rock has drifted in a frame */
        }
    }
}

static void test_respawn_at_a_quiet_spot(void) {
    GameState state;
    int trial;
    int index;

    for (trial = 0; trial < 20; ++trial) {
        long closest = 100000;

        quiet_playing(&state, 4);
        memset(state.asteroids, 0, sizeof(state.asteroids));
        state.rng_state += (uint32_t) trial * 7919u;
        /* a ring of big rocks round the middle and a few more: the quietest place is somewhere out at the edges */
        for (index = 0; index < 8; ++index) {
            state.asteroids[index].active = 1;
            state.asteroids[index].size = GAME_ASTEROID_LARGE;
            state.asteroids[index].point_count = 8;
            state.asteroids[index].x = (long) (160 + (index % 2 ? 50 : -50) + (index / 2) * 6) << 16;
            state.asteroids[index].y = (long) (60 + (index / 2) * 40) << 16;
            state.asteroids[index].vx = 8000;
        }
        state.ship.invulnerability = 0;
        state.ship.x = state.asteroids[0].x;
        state.ship.y = state.asteroids[0].y;
        game_step(&state, &no_input);
        CHECK(state.lives == 2);
        CHECK(state.ship.invulnerability == 100);
        CHECK(state.ship.vx == 0 && state.ship.vy == 0);
        for (index = 0; index < 8; ++index) {
            const long distance = distance_squared(state.ship.x, state.ship.y, state.asteroids[index].x, state.asteroids[index].y);
            closest = distance < closest ? distance : closest;
        }
        CHECK(closest > 40L * 40L);   /* clear of every rock by a good margin */
    }
}

static void test_green_alien_balance(void) {
    GameState state;
    GameEnemy *alien;
    int frame;
    int index;
    int max_alive = 0;
    int shots = 0;

    quiet_playing(&state, 7);
    game_spawn_enemy_now(&state, GAME_ENEMY_GREEN);
    alien = first_enemy(&state, GAME_ENEMY_GREEN);
    CHECK(alien->timer >= 100 && alien->timer < 150);   /* a slower start */
    alien->x = 100L << 16;
    alien->y = 40L << 16;
    alien->vx = 0;
    alien->timer = 1;
    state.ship.x = 200L << 16;
    state.ship.y = 160L << 16;
    game_step(&state, &no_input);
    for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
        const GameBullet *bullet = &state.enemy_bullets[index];
        if (bullet->active && bullet->color == GAME_COLOR_SHIP) {
            const long vx = bullet->vx >> 8;
            const long vy = bullet->vy >> 8;
            CHECK(vx * vx + vy * vy > 540L * 540L && vx * vx + vy * vy < 565L * 565L);   /* 4.5 px/tick: 553 in 8.8 */
            CHECK(bullet->life == 46);                                                  /* a shorter range than before */
            ++shots;
        }
    }
    CHECK(shots == 1);
    CHECK(alien->timer >= 75 + 0 && alien->timer < 200);

    /* at most three of its shots at a time, and none while it is at the very edge of the field */
    for (frame = 0; frame < 600; ++frame) {
        int alive = 0;
        alien->timer = alien->timer > 2 ? 2 : alien->timer;
        keep_ship_safe(&state);
        game_step(&state, &no_input);
        for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
            alive += (state.enemy_bullets[index].active && state.enemy_bullets[index].color == GAME_COLOR_SHIP);
        }
        max_alive = alive > max_alive ? alive : max_alive;
        if (!alien->active) {
            break;
        }
    }
    CHECK(max_alive <= 3);

    quiet_playing(&state, 7);
    game_spawn_enemy_now(&state, GAME_ENEMY_GREEN);
    alien = first_enemy(&state, GAME_ENEMY_GREEN);
    alien->x = 3L << 16;
    alien->vx = 0;
    alien->timer = 1;
    game_step(&state, &no_input);
    CHECK(enemy_bullet_count(&state) == 0);   /* off to the side it holds its fire (and the timer waits at 1) */
    CHECK(alien->timer == 1);
}

static void test_blue_sentinel_balance(void) {
    GameState state;
    GameEnemy *alien;
    int frame;
    int teleports = 0;
    int far_enough = 1;

    quiet_playing(&state, 12);
    game_spawn_enemy_now(&state, GAME_ENEMY_BLUE);
    alien = first_enemy(&state, GAME_ENEMY_BLUE);
    CHECK(alien->timer2 >= 350 && alien->timer2 < 500);   /* the first teleport is some time off */
    state.ship.x = 160L << 16;
    state.ship.y = 120L << 16;
    for (frame = 0; frame < 1700 && alien->active; ++frame) {
        const long before_x = alien->x;
        const long before_y = alien->y;

        state.ship.x = 160L << 16;
        state.ship.y = 120L << 16;
        keep_ship_safe(&state);
        game_step(&state, &no_input);
        if (!alien->active) {
            break;
        }
        if (labs(alien->x - before_x) > (30L << 16) || labs(alien->y - before_y) > (30L << 16)) {
            const long dx = (alien->x - state.ship.x) >> 16;
            const long dy = (alien->y - state.ship.y) >> 16;

            ++teleports;
            far_enough &= (dx * dx + dy * dy >= 80L * 80L);   /* 88 px, from the ship */
            CHECK(alien->x >= (20L << 16) && alien->x <= (300L << 16));
        }
        memset(state.enemy_bullets, 0, sizeof(state.enemy_bullets));
    }
    CHECK(teleports >= 2 && teleports <= 5);   /* about every 7-10 s, no longer every few seconds */
    CHECK(far_enough);
    CHECK(!alien->active);                      /* and after 30 s it teleports away for good */
    CHECK(frame < 1700);
}

static void test_boing_ball_stages(void) {
    GameState state;
    int frame;
    int max_stage = 0;
    int saw_floor_shots = 0;
    int saw_hover = 0;
    int saw_drop = 0;
    int stage_changes = 0;
    int last_stage = 0;

    quiet_playing(&state, 4);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    step_to_next_wave(&state);
    CHECK(state.boss.kind == GAME_BOSS_AMIGA_BALL);
    CHECK(state.boss.hp == 14 && state.boss.max_hp == 14);   /* (3 + wave) x 1.8 */
    CHECK(state.ship.invulnerability >= 99);                 /* a longer grace at the start of a boss fight */
    state.boss.max_hp = 100;
    state.boss.hp = 100;

    for (frame = 0; frame < 2400; ++frame) {
        int index;

        keep_ship_safe(&state);
        state.ship.x = (frame < 1200 ? 60L : 260L) << 16;
        state.ship.y = 200L << 16;
        /* wear it down steadily through the three stages */
        if (frame % 8 == 0 && state.boss.hp > 3 && state.boss.entered) {
            --state.boss.hp;
        }
        game_step(&state, &no_input);
        if (!state.boss.active) {
            break;
        }
        if (state.boss.entered) {
            CHECK(state.boss.x >= (23L << 16) && state.boss.x <= (297L << 16));
            CHECK(state.boss.y >= (15L << 16) && state.boss.y <= (209L << 16));
        }
        max_stage = state.boss.stage > max_stage ? state.boss.stage : max_stage;
        if (state.boss.stage != last_stage) {
            ++stage_changes;
            last_stage = state.boss.stage;
            CHECK(state.boss.stage_flash > 0);
        }
        saw_hover |= state.boss.dash_state == 1;
        saw_drop |= state.boss.dash_state == 2;
        for (index = 0; index < GAME_MAX_ENEMY_BULLETS; ++index) {
            const GameBullet *bullet = &state.enemy_bullets[index];
            if (bullet->active && bullet->y == (220L << 16) && bullet->vy == 0) {
                saw_floor_shots = 1;
            }
        }
    }
    CHECK(max_stage == 2 && stage_changes == 2);   /* two changes: at 66% and 40% of its hit points */
    CHECK(saw_floor_shots);                        /* every landing sends a shockwave along the floor */
    CHECK(saw_hover && saw_drop);                  /* and in the last stage it hovers over the ship and drops */
}

static void test_game_over_ignores_held_fire(void) {
    GameState state;
    GameInput fire = {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0};
    int frame;

    quiet_playing(&state, 1);
    state.lives = 1;
    state.ship.invulnerability = 0;
    put_rock_on_ship(&state, GAME_ASTEROID_LARGE);
    game_step(&state, &fire);   /* fire held all the way from the game into the Game Over screen */
    CHECK(state.mode == GAME_MODE_GAME_OVER);
    for (frame = 0; frame < 100; ++frame) {
        game_step(&state, &fire);
    }
    CHECK(state.mode == GAME_MODE_GAME_OVER);   /* it stays up: held fire does not skip it */
}

static int offset_calls;
static int offset_cache_hits;

static void capture_offsets(void *context, int center_x, int center_y, const int8_t *off_x, const int8_t *off_y,
                            int count, uint8_t color, void *cache, uint8_t *cache_valid) {
    int index;

    (void) cache;
    ++offset_calls;
    offset_cache_hits += *cache_valid;
    *cache_valid = 1;   /* as a drawer would after preparing its cache */
    for (index = 0; index < count; ++index) {
        const int next = (index + 1 == count) ? 0 : index + 1;

        capture_line(context, center_x + off_x[index], center_y + off_y[index], center_x + off_x[next],
                     center_y + off_y[next], color);
    }
}

/* Rocks well inside the field go through the offsets route, edge rocks through the plain polygon route, and
   both draw exactly the same lines. */
static void test_rock_offset_route(void) {
    GameState state;
    GameRenderer renderer;
    int lines_a[MAX_LINES][4];
    int count_a;
    int index;
    int at_edge;

    for (at_edge = 0; at_edge < 2; ++at_edge) {
        quiet_playing(&state, 12);
        state.asteroids[1] = state.asteroids[0];
        state.asteroids[1].size = GAME_ASTEROID_LARGE;
        state.asteroids[1].point_count = 10;
        state.asteroids[1].x = (at_edge ? 2L : 160L) << GAME_FIX_SHIFT;
        state.asteroids[1].y = 120L << GAME_FIX_SHIFT;
        for (index = 0; index < 10; ++index) {
            state.asteroids[1].radius[index] = (uint8_t) (14 + index % 4);
        }

        renderer = make_renderer();
        reset_capture();
        game_render(&state, &renderer);
        count_a = line_count < MAX_LINES ? line_count : MAX_LINES;
        memcpy(lines_a, lines, sizeof(lines_a));

        state.asteroids[1].cache_valid = 0;
        state.asteroids[0].cache_valid = 0;
        state.screen_refresh = 0;
        renderer.polygon_offsets = capture_offsets;
        renderer.sprite = NULL;
        offset_calls = 0;
        offset_cache_hits = 0;
        reset_capture();
        game_render(&state, &renderer);
        CHECK(offset_cache_hits == 0);   /* a rebuilt outline starts with an empty drawer cache */
        CHECK(line_count == count_a);
        CHECK(memcmp(lines, lines_a, sizeof(int) * 4 * (size_t) count_a) == 0);
        /* the parked small rock lies inside the field; the big one only takes the offsets route away from the edge */
        CHECK(offset_calls == (at_edge ? 1 : 2));

        /* drawing the same outline again reuses what the drawer prepared */
        offset_calls = 0;
        offset_cache_hits = 0;
        reset_capture();
        game_render(&state, &renderer);
        CHECK(offset_calls > 0 && offset_cache_hits == offset_calls);
    }
}

#ifdef ATARI_ST_TARGET
/* The assembly loops of game_step against their C reference versions, on random states. */
static unsigned long asm_rng = 31337;

static int asm_rand(int limit) {
    asm_rng = asm_rng * 1103515245ul + 12345ul;
    return (int) (((asm_rng >> 16) & 0x7fffu) % (unsigned) limit);
}

static void randomize_step_state(GameState *state) {
    int index;

    init_playing(state);
    memset(state->asteroids, 0, sizeof(state->asteroids));
    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        GameAsteroid *rock = &state->asteroids[index];

        rock->active = (uint8_t) (asm_rand(3) != 0);
        rock->size = (uint8_t) (1 + asm_rand(3));
        rock->point_count = (uint8_t) (8 + asm_rand(4));
        /* near and beyond the edges of the world, so wrapping (and the nudge after it) happens a lot */
        rock->x = ((int32_t) (asm_rand(400) - 40) << 16) + asm_rand(65536);
        rock->y = ((int32_t) (asm_rand(320) - 40) << 16) + asm_rand(65536);
        rock->vx = ((int32_t) (asm_rand(9) - 4) << 16) + asm_rand(65536);
        rock->vy = ((int32_t) (asm_rand(9) - 4) << 16) + asm_rand(65536);
        rock->angle = (uint16_t) asm_rand(65536);
        rock->spin = (int16_t) (asm_rand(2000) - 1000);
    }
    state->ship.x = ((int32_t) asm_rand(320) << 16) + asm_rand(65536);
    state->ship.y = ((int32_t) asm_rand(240) << 16) + asm_rand(65536);
    for (index = 0; index < GAME_MAX_BULLETS; ++index) {
        GameBullet *bullet = &state->bullets[index];

        bullet->active = (uint8_t) (asm_rand(3) != 0);
        bullet->life = (uint8_t) asm_rand(5);
        bullet->x = ((int32_t) (asm_rand(400) - 40) << 16) + asm_rand(65536);
        bullet->y = ((int32_t) (asm_rand(320) - 40) << 16) + asm_rand(65536);
        bullet->vx = ((int32_t) (asm_rand(13) - 6) << 16) + asm_rand(65536);
        bullet->vy = ((int32_t) (asm_rand(13) - 6) << 16) + asm_rand(65536);
    }
}

static void test_step_assembly_matches_c(void) {
    static GameState a;
    static GameState b;
    static const uint16_t reach[4] = {0, 6 << 4, 11 << 4, 17 << 4};
    int trial;

    for (trial = 0; trial < 400; ++trial) {
        int first;

        randomize_step_state(&a);
        b = a;
        game_update_rocks_ref(&a);
        st_update_rocks(&b);
        CHECK(memcmp(a.asteroids, b.asteroids, sizeof(a.asteroids)) == 0);

        randomize_step_state(&a);
        b = a;
        game_update_bullets_ref(&a);
        st_update_bullets(&b);
        CHECK(memcmp(a.bullets, b.bullets, sizeof(a.bullets)) == 0);

        /* hits: put bullets close to rocks so most searches find something, some on the exact edge of reach */
        randomize_step_state(&a);
        for (first = 0; first < GAME_MAX_BULLETS; ++first) {
            GameBullet *bullet = &a.bullets[first];
            const GameAsteroid *rock = &a.asteroids[asm_rand(GAME_MAX_ASTEROIDS)];

            if (asm_rand(4) != 0) {
                bullet->x = rock->x + ((int32_t) (asm_rand(41) - 20) << 12) + asm_rand(4096);
                bullet->y = rock->y + ((int32_t) (asm_rand(41) - 20) << 12) + asm_rand(4096);
            }
        }
        for (first = 0; first <= GAME_MAX_BULLETS; ++first) {
            int rock_c = -2;
            long rock_asm = -2;
            const int bullet_c = game_find_bullet_hit_ref(&a, first, &rock_c);
            const long bullet_asm = st_find_bullet_hit(&a, first, reach, &rock_asm);

            CHECK(bullet_c == (int) bullet_asm);
            CHECK(bullet_c < 0 || rock_c == (int) rock_asm);
        }
    }
}
#endif

static void test_enemy_rendering(void) {
    GameState state;
    int kind;
    int frame;

    /* every alien and UFO kind draws something in its colour and reports a rectangle */
    for (kind = GAME_ENEMY_UFO_LARGE; kind <= GAME_ENEMY_PURPLE; ++kind) {
        const int color = (kind == GAME_ENEMY_UFO_LARGE) ? GAME_COLOR_YELLOW : (kind == GAME_ENEMY_UFO_SMALL) ? GAME_COLOR_MAGENTA
                          : (kind == GAME_ENEMY_BROWN) ? GAME_COLOR_BROWN : (kind == GAME_ENEMY_GREEN) ? GAME_COLOR_SHIP
                          : (kind == GAME_ENEMY_BLUE) ? GAME_COLOR_ASTEROID_SMALL : GAME_COLOR_PURPLE;

        quiet_playing(&state, 12);
        state.enemies[0].active = 1;
        state.enemies[0].kind = (uint8_t) kind;
        state.enemies[0].hp = 1;
        state.enemies[0].x = 150L << 16;
        state.enemies[0].y = 100L << 16;
        render(&state);
        CHECK(color_counts[color] >= 4);
        CHECK(rect_count >= 3);
    }

    /* an invisible sentinel is not drawn (the parked test rock also uses this colour, so compare) */
    quiet_playing(&state, 12);
    render(&state);
    {
        const int baseline = color_counts[GAME_COLOR_ASTEROID_SMALL];

        state.enemies[0].active = 1;
        state.enemies[0].kind = GAME_ENEMY_BLUE;
        state.enemies[0].flags = 1;
        state.enemies[0].x = 150L << 16;
        state.enemies[0].y = 100L << 16;
        render(&state);
        CHECK(color_counts[GAME_COLOR_ASTEROID_SMALL] == baseline);
        state.enemies[0].flags = 0;
        render(&state);
        CHECK(color_counts[GAME_COLOR_ASTEROID_SMALL] > baseline);
    }

    /* enemy shots are drawn in their own colour */
    quiet_playing(&state, 1);
    state.enemy_bullets[0].active = 1;
    state.enemy_bullets[0].life = 10;
    state.enemy_bullets[0].color = GAME_COLOR_YELLOW;
    state.enemy_bullets[0].x = 100L << 16;
    state.enemy_bullets[0].y = 100L << 16;
    render(&state);
    CHECK(color_counts[GAME_COLOR_YELLOW] >= 1);

    /* all four bosses, at several moments of their animation, draw and stay covered by their rectangles */
    for (kind = GAME_BOSS_AMIGA_BALL; kind <= GAME_BOSS_BORG_CUBE; ++kind) {
        quiet_playing(&state, kind * 5 - 1);
        memset(state.asteroids, 0, sizeof(state.asteroids));
        step_to_next_wave(&state);
        CHECK(state.boss.kind == kind);
        state.boss.entered = 1;
        for (frame = 0; frame < 300; ++frame) {
            keep_ship_safe(&state);
            game_step(&state, &no_input);
            state.ufo_timer = 30000;
            if (frame % 7 == 0) {
                int index;
                render(&state);
                CHECK(line_count > 8);
                CHECK(rect_count >= 2);
                for (index = 0; index < line_count && index < MAX_LINES; ++index) {
                    CHECK(point_is_covered(lines[index][0], lines[index][1]));
                    CHECK(point_is_covered(lines[index][2], lines[index][3]));
                }
            }
        }
    }
}

/* Long random play with everything switched on: nothing may leave the rectangles unreported. */
static void test_dirty_rects_with_enemies(void) {
    GameState state;
    int frame;

    init_playing(&state);
    for (frame = 0; frame < 3000; ++frame) {
        GameInput input = {0, 0, 0, 0, 0, 0, 0, 0};
        input.thrust = (uint8_t) ((frame / 40) & 1);
        input.left = (uint8_t) ((frame / 25) & 1);
        input.fire = (uint8_t) ((frame / 7) & 1);
        input.hyperspace = (uint8_t) (frame % 173 == 0);
        if (state.mode != GAME_MODE_PLAYING) {
            game_start(&state);
        }
        if (frame % 400 == 200) {
            /* jump to a later wave so aliens and bosses appear */
            state.wave = (uint8_t) (state.wave + 4);
            memset(state.asteroids, 0, sizeof(state.asteroids));
        }
        state.ship.invulnerability = (uint8_t) (frame % 3 == 0 ? 255 : state.ship.invulnerability);
        game_step(&state, &input);
        if (frame % 5 == 0) {
            check_dirty_coverage(&state);
        }
    }
}

int main(void) {
    test_initial_state();
    test_start_from_title();
    test_no_autopilot();
    test_wave_sizes();
    test_asteroid_shapes();
    test_bullet_breaks_asteroids_and_scores();
    test_ship_wraps_across_screen();
    test_turning_speed();
    test_thrust_and_drag();
    test_bullets();
    test_ship_collision();
    test_wave_clear_advances();
    test_asteroid_speeds_scale_with_wave();
    test_extra_lives();
    test_hyperspace();
    test_pause();
    test_game_over_without_high_score();
    test_game_over_skip_with_key();
    test_initials_entry();
    test_high_score_ranking();
    test_score_file_round_trip();
    test_render_playing_geometry();
    test_ship_is_long_and_narrow();
    test_dirty_rects_cover_everything_drawn();
    test_render_cache_is_reused();
    test_text_layout_is_aligned();
    test_hud_content_and_redraw();
    test_static_screens_draw_once_per_buffer();
    test_title_prompt_blinks();
    test_game_over_and_initials_screens();
    test_banner_and_playing_screen();
    test_powerup_drops();
    test_powerup_economy();
    test_powerup_expires_and_wraps();
    test_powerup_shield();
    test_powerup_rapid_fire();
    test_powerup_multiplier_and_life();
    test_powerup_render_and_hud();
    test_stars();
    test_stars_render_and_erase();
    test_ufo_spawn_and_flight();
    test_ufo_shots_and_points();
    test_alien_waves_and_gates();
    test_brown_alien_chases();
    test_green_alien_shoots_and_takes_two_hits();
    test_blue_sentinel();
    test_purple_swarm();
    test_enemy_bullets_hurt();
    test_wave_waits_for_enemies();
    test_boss_waves();
    test_boss_hurts_the_ship();
    test_boss_hit_flash_and_sound();
    test_wave_clear_bonus_and_position();
    test_respawn_at_a_quiet_spot();
    test_green_alien_balance();
    test_blue_sentinel_balance();
    test_boing_ball_stages();
    test_game_over_ignores_held_fire();
    test_enemy_rendering();
    test_rock_offset_route();
    test_escape_pauses_and_leaves();
    test_p_pauses_too();
    test_menu_setting();
    test_hyperspace_on_space();
    test_start_key_does_not_jump();
    test_typing_initials();
#ifdef ATARI_ST_TARGET
    test_step_assembly_matches_c();
#endif
    test_dirty_rects_with_enemies();
    test_sound_engine();
    test_game_sound_events();
    test_sound_ids_are_valid();

    printf("%d checks, %d failures\n", checks, failures);
#ifdef ATARI_ST_TARGET
    printf("press a key\n");
    getchar();
#endif
    return failures != 0;
}
