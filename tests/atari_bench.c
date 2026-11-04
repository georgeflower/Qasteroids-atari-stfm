/*
 * Frame cost benchmark for the Atari build. Times the parts of a frame
 * (game step, render maths, line drawing, erase) separately for a few
 * wave sizes and prints milliseconds per frame. A frame must fit in 20 ms to
 * hold 50 fps. Run it inside Hatari: make bench-atari.
 */
#include "game.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

#define BENCH_FRAMES 50
#define ST_HZ200 (*(volatile uint32_t *) 0x4baUL)

enum {
    MODE_STEP,
    MODE_RENDER_MATHS,      /* render with the drawing calls stubbed out */
    MODE_MATHS_DIRTY,       /* ... plus reporting the dirty rectangles */
    MODE_MATHS_DIRTY_CLEAR, /* ... plus erasing them again */
    MODE_RENDER_DRAW,       /* render with real drawing (and dirty rectangles) */
    MODE_ALL,               /* the whole frame: step, erase, render, draw */
    MODE_COUNT
};

static uint32_t lines_seen;

static void null_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    (void) context;
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
    (void) color;
    ++lines_seen;
}

static void null_polygon(void *context, const int16_t *points, int count, uint8_t color) {
    (void) context;
    (void) points;
    (void) count;
    (void) color;
    ++lines_seen;
}

static void null_offsets(void *context, int cx, int cy, const int8_t *ox, const int8_t *oy, int count, uint8_t color,
                         void *cache, uint8_t *cache_valid) {
    (void) cache;
    (void) cache_valid;
    (void) context;
    (void) cx;
    (void) cy;
    (void) ox;
    (void) oy;
    (void) count;
    (void) color;
    ++lines_seen;
}

static uint32_t run(GameState *state, int mode) {
    GameRenderer maths_only;
    GameRenderer maths_and_dirty;
    GameRenderer full;
    GameInput input;
    uint32_t start;
    int frame;

    memset(&maths_only, 0, sizeof(maths_only));
    maths_only.line = null_line;
    maths_only.polygon = null_polygon;
    maths_only.polygon_offsets = null_offsets;
    maths_only.sprite = NULL;
    maths_and_dirty = maths_only;
    maths_and_dirty.dirty = platform_mark_dirty;
    memset(&full, 0, sizeof(full));
    full.line = platform_draw_line;
    full.polygon = platform_draw_polygon;
    full.dirty = platform_mark_dirty;
    full.text = platform_draw_text;
    full.clear_field = platform_clear_field;
    full.points = platform_draw_points;
    full.polygon_offsets = platform_draw_polygon_offsets;
    full.sprite = NULL;
#ifndef BENCH_C_ROUTE
    full.rocks = platform_draw_rocks;
#endif

    memset(&input, 0, sizeof(input));
    input.left = 1;   /* manual input: measures the game itself */
    lines_seen = 0;
    start = ST_HZ200;
    for (frame = 0; frame < BENCH_FRAMES; ++frame) {
        state->ship.invulnerability = 255;
        if (mode == MODE_STEP || mode == MODE_ALL) {
            game_step(state, &input);
        }
        if (mode == MODE_MATHS_DIRTY || mode == MODE_MATHS_DIRTY_CLEAR) {
            game_render(state, &maths_and_dirty);
            if (mode == MODE_MATHS_DIRTY_CLEAR) {
                platform_begin_frame();
            }
        }
        if (mode == MODE_ALL) {
            platform_begin_frame();
        }
        if (mode == MODE_RENDER_MATHS) {
            game_render(state, &maths_only);
        }
        if (mode == MODE_RENDER_DRAW || mode == MODE_ALL) {
            game_render(state, &full);
        }
    }
    return ST_HZ200 - start;
}

static int count_rocks(const GameState *state) {
    int rocks = 0;
    int index;

    for (index = 0; index < GAME_MAX_ASTEROIDS; ++index) {
        rocks += state->asteroids[index].active;
    }
    return rocks;
}

/* Rock populations: how many large, medium and small rocks. The first is a fresh early wave, the second what
   a level 4 looks like a while in, the third a crowded late one, the last twelve large ones (the worst case). */
static const int scenarios[4][3] = {{6, 0, 0}, {4, 8, 12}, {5, 8, 14}, {12, 0, 0}};
/* the last scenario keeps every rock within a few pixels of the edge of the world, where they poke out of the field */
#define BORDER_SCENARIO 3
static uint32_t lcg = 777;

static int lcg_below(int limit) {
    lcg = lcg * 1103515245ul + 12345ul;
    return (int) (((lcg >> 16) & 0x7fffu) % (unsigned) limit);
}

/* Turn the rock in slot `slot` into one of the given size at a random place (radii scaled from the large ones). */
static void make_rock(GameState *state, int slot, int size, const GameAsteroid *model, int at_border) {
    static const int radius_scale[4] = {0, 5, 10, 16};
    GameAsteroid *rock = &state->asteroids[slot];
    int index;

    GameAsteroid shared;

    *rock = *model;
    rock->size = (uint8_t) size;
    game_rock_shape(size, lcg_below(GAME_ROCK_SHAPES), &shared);
    rock->shape = shared.shape;
    rock->point_count = shared.point_count;
    memcpy(rock->radius, shared.radius, sizeof(rock->radius));
    rock->x = (int32_t) (at_border ? (lcg_below(2) ? lcg_below(12) : 308 + lcg_below(12)) : 30 + lcg_below(260)) << GAME_FIX_SHIFT;
    rock->y = (int32_t) (at_border ? lcg_below(240) : 30 + lcg_below(180)) << GAME_FIX_SHIFT;
    rock->vx = at_border ? 0 : (int32_t) (lcg_below(60000) - 30000);   /* border rocks stay where they are */
    rock->vy = at_border ? 0 : (int32_t) (lcg_below(60000) - 30000);
    rock->angle = (uint16_t) lcg_below(65535);
    rock->spin = (int16_t) (lcg_below(500) - 250);
    (void) index;
    (void) radius_scale;
    rock->cache_valid = 0;
    rock->draw_cache_valid = 0;
}

int main(void) {
        GameState state;
    uint32_t ticks[4][MODE_COUNT];
    int rocks[4];
    uint32_t star_ticks = 0;
    int wave_index;
    int mode;

    if (!platform_init()) {
        return 1;
    }
    game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
    platform_build_rock_sprites(&state);

    for (wave_index = 0; wave_index < 4; ++wave_index) {
        game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
        game_start(&state);
        memset(state.asteroids, 0, sizeof(state.asteroids));
        state.wave = 0;
        state.ship.invulnerability = 255;
        game_step(&state, &(GameInput) {0, 0, 0, 0, 0, 0, 0, 0});
        state.banner_timer = 0;
        {
            /* a fresh wave gives large rocks; keep the first as a model and rebuild the population from it */
            const GameAsteroid model = state.asteroids[0];
            int slot = 0;
            int size;
            int made;

            memset(state.asteroids, 0, sizeof(state.asteroids));
            for (size = GAME_ASTEROID_LARGE; size >= GAME_ASTEROID_SMALL; --size) {
                for (made = 0; made < scenarios[wave_index][GAME_ASTEROID_LARGE - size] && slot < GAME_MAX_ASTEROIDS; ++made) {
                    make_rock(&state, slot++, size, &model, wave_index == BORDER_SCENARIO);
                }
            }
            state.ufo_timer = 30000;
            state.alien_timer = 30000;
        }
        rocks[wave_index] = count_rocks(&state);
        for (mode = 0; mode < MODE_COUNT; ++mode) {
            ticks[wave_index][mode] = run(&state, mode);
        }
    }

    /* the starfield on its own: 24 single pixels */
    {
        int16_t points[48];
        uint32_t start;
        int frame;
        int index;

        for (index = 0; index < 24; ++index) {
            points[index * 2] = (int16_t) (PLATFORM_FIELD_X + 10 + index * 11);
            points[index * 2 + 1] = (int16_t) (PLATFORM_FIELD_Y + 10 + index * 5);
        }
        start = ST_HZ200;
        for (frame = 0; frame < BENCH_FRAMES; ++frame) {
            platform_draw_points(NULL, points, 24, GAME_COLOR_STAR_BRIGHT);
        }
        star_ticks = ST_HZ200 - start;
    }

    platform_shutdown();

    printf("ms per frame:\n step maths +dirty +erase draw ALL\n");
    for (wave_index = 0; wave_index < 4; ++wave_index) {
        printf("%dL %dM %dS (%d rocks):\n", scenarios[wave_index][0], scenarios[wave_index][1],
               scenarios[wave_index][2], rocks[wave_index]);
        for (mode = 0; mode < MODE_COUNT; ++mode) {
            /* 200 Hz ticks: ms per frame = ticks * 5 / frames, shown with one decimal */
            const unsigned long tenths = (unsigned long) (ticks[wave_index][mode] * 50 / BENCH_FRAMES);
            printf("%4lu.%lu", tenths / 10, tenths % 10);
        }
        printf("\n");
    }
    {
        const unsigned long tenths = (unsigned long) (star_ticks * 50 / BENCH_FRAMES);
        printf("24 star pixels: %lu.%lu ms\n", tenths / 10, tenths % 10);
    }
    printf("20.0 ms = 50 fps\n");
    printf("press a key\n");
    getchar();
    return 0;
}
