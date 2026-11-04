/*
 * Micro-benchmarks of the drawing primitives (Hatari): make micro-atari. Results in C:\MICRO.LOG.
 * Times are 200 Hz ticks for N calls; cycles per call = ticks * 40000 / N.
 */
#include "game.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

#define ST_HZ200 (*(volatile uint32_t *) 0x4baUL)
#define N 400

extern void st_draw_poly_plane(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_line_plane(unsigned char *buffer, long x0, long y0, long x1, long y1, long plane_offset);
extern void st_clear_rect(unsigned char *buffer, long group0, long group1, long y0, long y1);

static unsigned char buffer[32000 + 4];

static void log_line(const char *text) {
    static const char path[] = {'C', ':', 92, 'M', 'I', 'C', 'R', 'O', '.', 'L', 'O', 'G', 0};
    FILE *file = fopen(path, "a");

    if (file != NULL) {
        fputs(text, file);
        fputc(10, file);
        fclose(file);
    }
}

static void report(const char *name, uint32_t ticks, int calls, int pixels) {
    char text[100];
    const unsigned long cycles = (unsigned long) ticks * 40000UL / (unsigned long) calls;

    sprintf(text, "%-28s %6lu cycles/call  (%lu per pixel)", name, cycles, pixels ? cycles / (unsigned long) pixels : 0UL);
    log_line(text);
}

/* a rock-like 11-gon of radius about 15 around (160, 100) */
static const short rock[22] = {
    175, 100, 172, 108, 166, 114, 158, 116, 150, 113, 145, 106,
    146, 96, 152, 88, 160, 85, 168, 88, 173, 94
};


static int n_line, n_poly, n_offs, n_dirty, n_text, n_points, n_point_px, n_clear, px_offs, px_poly, px_line;

static int perimeter(const int16_t *p, int n) {
    int total = 0;
    int i;

    for (i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        int dx = p[i * 2] - p[j * 2];
        int dy = p[i * 2 + 1] - p[j * 2 + 1];

        dx = dx < 0 ? -dx : dx;
        dy = dy < 0 ? -dy : dy;
        total += dx > dy ? dx : dy;
    }
    return total;
}


static void c_line(void *c, int a, int b, int d, int e, uint8_t f) { ++n_line; px_line += (d > a ? d - a : a - d) > (e > b ? e - b : b - e) ? (d > a ? d - a : a - d) : (e > b ? e - b : b - e); platform_draw_line(c, a, b, d, e, f); }
static void c_poly(void *c, const int16_t *p, int n, uint8_t f) { ++n_poly; px_poly += perimeter(p, n); platform_draw_polygon(c, p, n, f); }
static void c_offs(void *c, int x, int y, const int8_t *a, const int8_t *b, int n, uint8_t f, void *cache, uint8_t *valid) { int16_t q[24]; int k; for (k = 0; k < n; ++k) { q[k * 2] = a[k]; q[k * 2 + 1] = b[k]; } ++n_offs; px_offs += perimeter(q, n); platform_draw_polygon_offsets(c, x, y, a, b, n, f, cache, valid); }
static void c_dirty(void *c, int a, int b, int d, int e) { ++n_dirty; platform_mark_dirty(c, a, b, d, e); }
static void c_text(void *c, int x, int y, const char *t, uint8_t f, uint8_t g, uint8_t s) { ++n_text; platform_draw_text(c, x, y, t, f, g, s); }
static void c_points(void *c, const int16_t *p, int n, uint8_t f) { ++n_points; n_point_px += n; platform_draw_points(c, p, n, f); }
static void c_clear(void *c) { ++n_clear; platform_clear_field(c); }

static void diagnose(void) {
    static GameState state;
    GameRenderer r;
    GameInput in;
    int frame;
    int wave;
    char text[160];

    memset(&in, 0, sizeof(in));
    r.context = NULL;
    r.line = c_line;
    r.polygon = c_poly;
    r.polygon_offsets = c_offs;
    r.sprite = NULL;
    r.dirty = c_dirty;
    r.text = c_text;
    r.points = c_points;
    r.clear_field = c_clear;
    for (wave = 1; wave <= 8; wave += 3) {
        game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
        game_start(&state);
        memset(state.asteroids, 0, sizeof(state.asteroids));
        state.wave = (uint8_t) (wave - 1);
        state.ship.invulnerability = 255;
        game_step(&state, &in);
        state.banner_timer = 0;
        for (frame = 0; frame < 60; ++frame) {
            state.ship.invulnerability = 255;
            game_step(&state, &in);
        }
        for (frame = 0; frame < 4; ++frame) {
            n_line = n_poly = n_offs = n_dirty = n_text = n_points = n_point_px = n_clear = px_offs = px_poly = px_line = 0;
            game_render(&state, &r);
            if (frame == 3) {
                sprintf(text, "wave %d: lines %d (px %d) poly %d (px %d) offs %d (px %d) dirty %d text %d pts %d/%d", wave,
                        n_line, px_line, n_poly, px_poly, n_offs, px_offs, n_dirty, n_text, n_points, n_point_px);
                log_line(text);
            }
        }
    }
}


static void n_line_cb(void *c, int a, int b, int d, int e, uint8_t f) { (void) c; (void) a; (void) b; (void) d; (void) e; (void) f; }
static void n_poly_cb(void *c, const int16_t *p, int n, uint8_t f) { (void) c; (void) p; (void) n; (void) f; }
static void n_dirty_cb(void *c, int a, int b, int d, int e) { (void) c; (void) a; (void) b; (void) d; (void) e; }

static void fixed_cost(void) {
    static GameState state;
    GameRenderer r;
    GameInput in;
    uint32_t start;
    int frame;

    memset(&in, 0, sizeof(in));
    memset(&r, 0, sizeof(r));
    r.line = n_line_cb;
    r.polygon = n_poly_cb;
    game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
    game_start(&state);
    state.ship.invulnerability = 255;
    game_step(&state, &in);
    state.banner_timer = 0;
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.asteroids[0].active = 1;
    state.asteroids[0].size = GAME_ASTEROID_SMALL;
    state.asteroids[0].point_count = 8;
    state.asteroids[0].x = 5L << GAME_FIX_SHIFT;
    state.asteroids[0].y = 5L << GAME_FIX_SHIFT;
    for (frame = 0; frame < 5; ++frame) {
        game_render(&state, &r);
    }
    start = ST_HZ200;
    for (frame = 0; frame < 200; ++frame) {
        game_render(&state, &r);
    }
    report("render, 1 rock, no dirty cb", ST_HZ200 - start, 200, 0);
    r.dirty = n_dirty_cb;
    start = ST_HZ200;
    for (frame = 0; frame < 200; ++frame) {
        game_render(&state, &r);
    }
    report("render, 1 rock, dirty cb", ST_HZ200 - start, 200, 0);
    state.mode = GAME_MODE_GAME_OVER;
    game_render(&state, &r);
    game_render(&state, &r);
    start = ST_HZ200;
    for (frame = 0; frame < 200; ++frame) {
        game_render(&state, &r);
    }
    report("render, game over (hud only)", ST_HZ200 - start, 200, 0);
    state.mode = GAME_MODE_PLAYING;
    state.ship.invulnerability = 255;
    start = ST_HZ200;
    for (frame = 0; frame < 200; ++frame) {
        state.ship.invulnerability = 255;
        state.asteroids[0].x = 5L << GAME_FIX_SHIFT;
        state.asteroids[0].y = 5L << GAME_FIX_SHIFT;
        game_step(&state, &in);
    }
    report("game_step, ~1 rock", ST_HZ200 - start, 200, 0);
}

int main(void) {
    unsigned char *base = (unsigned char *) (((unsigned long) buffer + 1) & ~1UL);
    uint32_t start;
    int i;
    int pixels = 0;
    int edge;

    for (edge = 0; edge < 11; ++edge) {
        const int n = (edge + 1) % 11;
        const int dx = rock[edge * 2] - rock[n * 2];
        const int dy = rock[edge * 2 + 1] - rock[n * 2 + 1];
        const int adx = dx < 0 ? -dx : dx;
        const int ady = dy < 0 ? -dy : dy;

        pixels += (adx > ady ? adx : ady) + 1;
    }

    if (!platform_init()) {
        return 1;
    }
    diagnose();
    fixed_cost();
    /* calibration: 20000 x (or.w d1,(a0); dbra) should be 20000 x 24 cycles */
    start = ST_HZ200;
    {
        unsigned char *target = base;
        long count = 20000 - 1;

        __asm__ volatile ("1: or.w %%d1,(%0)\n\tdbra %1,1b" : "+a" (target), "+d" (count) : : "d1", "memory", "cc");
    }
    report("calibration 24 cycles x20000", ST_HZ200 - start, 20000, 0);

    {
        signed char ox[11];
        signed char oy[11];
        int v;

        for (v = 0; v < 11; ++v) {
            ox[v] = (signed char) (rock[v * 2] - 160);
            oy[v] = (signed char) (rock[v * 2 + 1] - 100);
        }
        static uint16_t cache[GAME_DRAW_CACHE_WORDS];
        uint8_t valid = 0;

        start = ST_HZ200;
        for (i = 0; i < N; ++i) {
            platform_draw_polygon_offsets(NULL, 160, 100, ox, oy, 11, 2, cache, &valid);
        }
        report("rock from cached edges", ST_HZ200 - start, N, pixels);
    }

    start = ST_HZ200;
    for (i = 0; i < N; ++i) {
        st_draw_poly_plane(base, rock, 11, 2);
    }
    report("asm polygon (11 edges)", ST_HZ200 - start, N, pixels);

    start = ST_HZ200;
    for (i = 0; i < N; ++i) {
        int e;

        for (e = 0; e < 11; ++e) {
            const int n = (e + 1) % 11;

            st_draw_line_plane(base, rock[e * 2], rock[e * 2 + 1], rock[n * 2], rock[n * 2 + 1], 2);
        }
    }
    report("asm lines one by one", ST_HZ200 - start, N, pixels);

    start = ST_HZ200;
    for (i = 0; i < N; ++i) {
        st_draw_line_plane(base, 100, 100, 130, 100, 2);
    }
    report("horizontal line, 31 px", ST_HZ200 - start, N, 31);

    start = ST_HZ200;
    for (i = 0; i < N; ++i) {
        st_draw_line_plane(base, 100, 100, 130, 115, 2);
    }
    report("shallow line, 31 px", ST_HZ200 - start, N, 31);

    start = ST_HZ200;
    for (i = 0; i < N; ++i) {
        st_draw_line_plane(base, 100, 100, 100, 130, 2);
    }
    report("vertical line, 31 px", ST_HZ200 - start, N, 31);

    start = ST_HZ200;
    for (i = 0; i < N * 4; ++i) {
        st_draw_line_plane(base, 100, 100, 104, 102, 2);
    }
    report("short line, 5 px", ST_HZ200 - start, N * 4, 5);

    start = ST_HZ200;
    for (i = 0; i < N * 4; ++i) {
        st_clear_rect(base, 8, 10, 80, 110);
    }
    report("clear 3 groups x 31 rows", ST_HZ200 - start, N * 4, 31);

    start = ST_HZ200;
    for (i = 0; i < N * 4; ++i) {
        platform_draw_polygon(NULL, rock, 11, 2);
    }
    report("platform_draw_polygon rock", ST_HZ200 - start, N * 4, pixels);

    start = ST_HZ200;
    for (i = 0; i < N * 10; ++i) {
        platform_mark_dirty(NULL, 100, 100, 130, 130);
        if ((i % 50) == 49) {
            platform_begin_frame();
        }
    }
    report("mark_dirty (+ clears)", ST_HZ200 - start, N * 10, 0);
    platform_shutdown();
    return 0;
}
