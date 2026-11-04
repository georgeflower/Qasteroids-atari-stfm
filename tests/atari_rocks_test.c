/*
 * The assembly rocks loop (platform_draw_rocks) against the C route (draw_asteroid through polygon_offsets /
 * polygon), on the real screen memory (run in Hatari: make test-rocks-atari). Random rock populations, many of
 * them straddling the border, are rendered both ways from identical game states; the screens must be identical
 * and the same dirty rectangles must have been reported. Results go to C:\ROCKTEST.LOG.
 */
#include "game.h"
#include "platform.h"

#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_BYTES 32000
#define MAX_RECTS 128

static unsigned long rng = 99;
static int failures;
static int checks;
static unsigned char shot_c[SCREEN_BYTES];
static unsigned char shot_asm[SCREEN_BYTES];
static short rects_c[MAX_RECTS][4];
static short rects_asm[MAX_RECTS][4];
static GameState base;
static GameState work;

static unsigned rand_below(unsigned limit) {
    rng = rng * 1103515245ul + 12345ul;
    return (unsigned) (((rng >> 16) & 0xffffu) * limit >> 16);
}

static void log_line(const char *text) {
    static const char path[] = {'C', ':', 92, 'R', 'O', 'C', 'K', 'T', 'E', 'S', 'T', '.', 'L', 'O', 'G', 0};
    FILE *file = fopen(path, "a");

    if (file != NULL) {
        fputs(text, file);
        fputc(10, file);
        fclose(file);
    }
}

static void fail(const char *what, int detail) {
    char text[80];

    ++failures;
    if (failures <= 8) {
        sprintf(text, "FAIL %s %d", what, detail);
        log_line(text);
    }
}

static int rect_less(const short *a, const short *b) {
    int i;

    for (i = 0; i < 4; ++i) {
        if (a[i] != b[i]) {
            return a[i] < b[i];
        }
    }
    return 0;
}

static void sort_rects(short rects[][4], int count) {
    int i;
    int j;

    for (i = 1; i < count; ++i) {
        short key[4];

        memcpy(key, rects[i], sizeof(key));
        for (j = i - 1; j >= 0 && rect_less(key, rects[j]); --j) {
            memcpy(rects[j + 1], rects[j], sizeof(key));
        }
        memcpy(rects[j + 1], key, sizeof(key));
    }
}

/* Render the state once with the given renderer; returns the number of dirty rectangles. */
static int render_once(const GameRenderer *renderer, unsigned char *shot, short rects[][4]) {
    int count;
    int index;

    work = base;
    platform_begin_frame();
    game_render(&work, renderer);
    count = platform_dirty_count();
    if (count > MAX_RECTS) {
        count = MAX_RECTS;
    }
    for (index = 0; index < count; ++index) {
        platform_dirty_get(index, rects[index]);
    }
    platform_end_frame();
    memcpy(shot, Physbase(), SCREEN_BYTES);
    sort_rects(rects, count);
    return count;
}

int main(void) {
    static const int radius_of[4] = {0, 5, 10, 16};
    GameRenderer via_c;
    GameRenderer via_asm;
    int trial;
    char text[80];

    if (!platform_init()) {
        return 1;
    }
    memset(&via_c, 0, sizeof(via_c));
    via_c.line = platform_draw_line;
    via_c.polygon = platform_draw_polygon;
    via_c.polygon_offsets = platform_draw_polygon_offsets;
    via_c.sprite = NULL;
    via_c.dirty = platform_mark_dirty;
    via_c.text = platform_draw_text;
    via_c.clear_field = platform_clear_field;
    via_c.points = platform_draw_points;
    via_asm = via_c;
    via_asm.rocks = platform_draw_rocks;

    game_init(&base, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
    {
        const unsigned long start = *(volatile unsigned long *) 0x4baUL;

        platform_build_rock_sprites(&base);
        sprintf(text, "rock sprites: %lu bytes of generated code, built in %lu ms", platform_rock_sprite_bytes(),
                (*(volatile unsigned long *) 0x4baUL - start) * 5UL);
    }
    log_line(text);

    for (trial = 0; trial < 400; ++trial) {
        const int rocks = 1 + (int) rand_below(GAME_MAX_ASTEROIDS);
        int slot;
        int c_count;
        int asm_count;

        game_init(&base, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
        game_start(&base);
        memset(base.asteroids, 0, sizeof(base.asteroids));
        base.banner_timer = 0;
        base.screen_refresh = 0;
        base.hud_refresh = 0;
        base.ship.invulnerability = 255;
        for (slot = 0; slot < rocks; ++slot) {
            GameAsteroid *rock = &base.asteroids[slot];
            int index;

            rock->active = 1;
            rock->size = (uint8_t) (1 + rand_below(3));
            rock->point_count = (uint8_t) (8 + rand_below(4));
            for (index = 0; index < rock->point_count; ++index) {
                rock->radius[index] = (uint8_t) ((radius_of[rock->size] * (205 + rand_below(103))) >> 8);
            }
            if (rand_below(4) != 0) {
                /* most rocks have one of the shared outlines, which the assembly draws from pre-drawn routines */
                GameAsteroid shared;

                game_rock_shape(rock->size, (int) rand_below(GAME_ROCK_SHAPES), &shared);
                rock->shape = shared.shape;
                rock->point_count = shared.point_count;
                memcpy(rock->radius, shared.radius, sizeof(rock->radius));
            }
            /* a third of them hug the edges of the world, where the outline pokes out of the field */
            if (rand_below(3) == 0) {
                rock->x = (int32_t) (rand_below(2) ? rand_below(14) : 306 + rand_below(14)) << GAME_FIX_SHIFT;
                rock->y = (int32_t) rand_below(240) << GAME_FIX_SHIFT;
            } else {
                rock->x = ((int32_t) rand_below(320) << GAME_FIX_SHIFT) + (int32_t) rand_below(65536);
                rock->y = ((int32_t) rand_below(240) << GAME_FIX_SHIFT) + (int32_t) rand_below(65536);
            }
            rock->angle = (uint16_t) rand_below(65536);
        }
        /* the ship, stars and so on are the same in both renders; keep them out of the way of the comparison */
        c_count = render_once(&via_c, shot_c, rects_c);
        asm_count = render_once(&via_asm, shot_asm, rects_asm);

        ++checks;
        if (memcmp(shot_c, shot_asm, SCREEN_BYTES) != 0) {
            fail("screen differs, trial", trial);
        }
        ++checks;
        if (c_count != asm_count || memcmp(rects_c, rects_asm, sizeof(short) * 4 * (size_t) c_count) != 0) {
            fail("dirty rectangles differ, trial", trial);
        }
    }

    sprintf(text, "%d checks, %d failures", checks, failures);
    log_line(text);
    platform_shutdown();
    return failures != 0;
}
