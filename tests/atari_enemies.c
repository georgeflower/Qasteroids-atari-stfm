/*
 * Visual walk-through of the enemies: a line-up of every UFO and alien for a few seconds, then each of
 * the four bosses in turn (about 4 seconds each), with the ship kept safe. About 22 seconds in all.
 * Run in Hatari and take screenshots: make enemies-atari.
 */
#include "game.h"
#include "platform.h"
#include "sound.h"

#include <stdio.h>
#include <string.h>

static void show_frames(GameState *state, const GameRenderer *renderer, int frames) {
    int frame;
    GameInput input;

    memset(&input, 0, sizeof(input));
    for (frame = 0; frame < frames; ++frame) {
        state->ship.invulnerability = 255;
        state->ufo_timer = 30000;
        state->alien_timer = 30000;
        input.left = (uint8_t) ((frame / 60) & 1);
        input.fire = (uint8_t) ((frame / 9) & 1);
        game_step(state, &input);
        platform_begin_frame();
        game_render(state, renderer);
        platform_end_frame();
    }
}

int main(void) {
    GameState state;
    GameRenderer renderer;
    int kind;
    int index;

    renderer.context = NULL;
    renderer.line = platform_draw_line;
    renderer.polygon = platform_draw_polygon;
    renderer.dirty = platform_mark_dirty;
    renderer.text = platform_draw_text;
    renderer.clear_field = platform_clear_field;
    renderer.points = platform_draw_points;
    renderer.polygon_offsets = platform_draw_polygon_offsets;
    renderer.sprite = NULL;
    renderer.rocks = platform_draw_rocks;
    renderer.low_detail = 0;

    if (!platform_init()) {
        return 1;
    }

    /* the line-up */
    game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
    game_start(&state);
    state.wave = 12;
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.asteroids[0].active = 1;
    state.asteroids[0].size = GAME_ASTEROID_SMALL;
    state.asteroids[0].point_count = 8;
    state.asteroids[0].x = 5L << GAME_FIX_SHIFT;
    state.asteroids[0].y = 5L << GAME_FIX_SHIFT;
    for (kind = GAME_ENEMY_UFO_LARGE; kind <= GAME_ENEMY_PURPLE; ++kind) {
        index = kind - 1;
        state.enemies[index].active = 1;
        state.enemies[index].kind = (uint8_t) kind;
        state.enemies[index].hp = 2;
        state.enemies[index].x = (int32_t) (50 + index * 44) << GAME_FIX_SHIFT;
        state.enemies[index].y = 100L << GAME_FIX_SHIFT;
        state.enemies[index].timer = 30;
        state.enemies[index].timer2 = 100;
        state.enemies[index].flock = 1;
        state.enemies[index].vx = 0;
    }
    state.banner_timer = 0;
    show_frames(&state, &renderer, 250);

    /* each boss */
    for (kind = GAME_BOSS_AMIGA_BALL; kind <= GAME_BOSS_BORG_CUBE; ++kind) {
        game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
        game_start(&state);
        state.wave = (uint8_t) (kind * 5 - 1);
        memset(state.asteroids, 0, sizeof(state.asteroids));
        game_step(&state, &(GameInput) {0, 0, 0, 0, 0, 0, 0, 0});
        state.boss.entered = 1;
        state.boss.y = state.boss.target_y;
        state.banner_timer = 0;
        show_frames(&state, &renderer, 220);
    }

    platform_shutdown();
    printf("enemy walk-through done\n");
    return 0;
}
