/*
 * Frame cost with each boss (and with a line-up of every enemy) on screen: step + erase + render + draw, ms per
 * frame, without waiting for the vertical blank. Run in Hatari: make boss-bench-atari. Results: C:\BOSS.LOG.
 */
#include "game.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

#define ST_HZ200 (*(volatile uint32_t *) 0x4baUL)
#define FRAMES 60

static void log_line(const char *text) {
    static const char path[] = {'C', ':', 92, 'B', 'O', 'S', 'S', '.', 'L', 'O', 'G', 0};
    FILE *file = fopen(path, "a");

    if (file != NULL) {
        fputs(text, file);
        fputc(10, file);
        fclose(file);
    }
}

static unsigned long measure(GameState *state, const GameRenderer *renderer) {
    GameInput input;
    uint32_t start;
    int frame;

    memset(&input, 0, sizeof(input));
    input.left = 1;
    start = ST_HZ200;
    for (frame = 0; frame < FRAMES; ++frame) {
        state->ship.invulnerability = 255;
        state->ufo_timer = 30000;
        state->alien_timer = 30000;
        game_step(state, &input);
        platform_begin_frame();
        game_render(state, renderer);
        /* no page flip: only the work is timed */
    }
    return (unsigned long) (ST_HZ200 - start) * 5UL * 10UL / FRAMES;   /* tenths of a millisecond */
}

int main(void) {
    static GameState state;
    GameRenderer renderer;
    char text[80];
    int kind;
    int index;

    memset(&renderer, 0, sizeof(renderer));
    renderer.line = platform_draw_line;
    renderer.polygon = platform_draw_polygon;
    renderer.polygon_offsets = platform_draw_polygon_offsets;
    renderer.sprite = NULL;
    renderer.rocks = platform_draw_rocks;
    renderer.dirty = platform_mark_dirty;
    renderer.text = platform_draw_text;
    renderer.clear_field = platform_clear_field;
    renderer.points = platform_draw_points;

    if (!platform_init()) {
        return 1;
    }
    game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
    platform_build_rock_sprites(&state);

    for (kind = GAME_BOSS_AMIGA_BALL; kind <= GAME_BOSS_BORG_CUBE; ++kind) {
        unsigned long tenths;

        game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
        game_start(&state);
        state.wave = (uint8_t) (kind * 5 - 1);
        memset(state.asteroids, 0, sizeof(state.asteroids));
        game_step(&state, &(GameInput) {0, 0, 0, 0, 0, 0, 0, 0});
        state.boss.entered = 1;
        state.boss.y = state.boss.target_y;
        state.banner_timer = 0;
        tenths = measure(&state, &renderer);
        sprintf(text, "boss %d: %lu.%lu ms per frame", kind, tenths / 10, tenths % 10);
        log_line(text);
    }

    /* every enemy at once (as in the enemies line-up) */
    {
        unsigned long tenths;

        game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
        game_start(&state);
        state.wave = 12;
        memset(state.asteroids, 0, sizeof(state.asteroids));
        state.asteroids[0].active = 1;
        state.asteroids[0].size = GAME_ASTEROID_SMALL;
        state.asteroids[0].point_count = 8;
        state.asteroids[0].x = 5L << GAME_FIX_SHIFT;
        state.asteroids[0].y = 5L << GAME_FIX_SHIFT;
        for (index = GAME_ENEMY_UFO_LARGE; index <= GAME_ENEMY_PURPLE; ++index) {
            state.enemies[index - 1].active = 1;
            state.enemies[index - 1].kind = (uint8_t) index;
            state.enemies[index - 1].hp = 2;
            state.enemies[index - 1].x = (int32_t) (50 + (index - 1) * 44) << GAME_FIX_SHIFT;
            state.enemies[index - 1].y = 100L << GAME_FIX_SHIFT;
            state.enemies[index - 1].timer = 30;
            state.enemies[index - 1].timer2 = 100;
            state.enemies[index - 1].flock = 1;
        }
        state.banner_timer = 0;
        tenths = measure(&state, &renderer);
        sprintf(text, "all six enemy kinds: %lu.%lu ms per frame", tenths / 10, tenths % 10);
        log_line(text);
    }
    platform_shutdown();
    return 0;
}
