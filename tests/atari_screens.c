/*
 * Visual walk-through of the screens that are awkward to reach by hand: the wave banner, pause,
 * game over, initials entry and the hall of fame. Runs the real game with scripted input for about
 * 16 seconds (800 frames). Run in Hatari and take screenshots: make screens-atari.
 *
 *   frame    0-99   playing (wave banner for the first 75)
 *   frame  100-169  paused
 *   frame  200      the ship is destroyed on its last life -> game over screen (3 s)
 *   frame  350-529  entering initials (letters B, C, then Z)
 *   frame  530-800  title screen with the new score in the hall of fame
 */
#include "game.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    GameState state;
    GameInput input;
    GameRenderer renderer;
    int frame;

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

    game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
    game_start(&state);
    state.score = 12340;
    {
        /* one of each power-up on show, and some effects running, to see the icons and the HUD badges */
        int index;

        for (index = 0; index < GAME_POWERUP_TYPES; ++index) {
            state.powerups[index].active = 1;
            state.powerups[index].type = (uint8_t) index;
            state.powerups[index].x = (int32_t) (70 + index * 60) << GAME_FIX_SHIFT;
            state.powerups[index].y = 60L << GAME_FIX_SHIFT;
            state.powerups[index].life = 240;
        }
        state.shield_timer = 200;
        state.rapid_timer = 300;
        state.multiplier_timer = 500;
    }

    for (frame = 0; frame < 800; ++frame) {
        memset(&input, 0, sizeof(input));
        if (frame == 100 || frame == 170) {
            input.pause = 1;
        }
        if (frame == 380 || frame == 440) {
            input.right = 1;
        }
        if (frame == 500) {
            input.left = 1;
        }
        if (frame == 410 || frame == 470 || frame == 530) {
            input.start = 1;
            input.fire = 1;
        }
        if (frame == 200) {
            /* the ship is hit on its last life */
            state.lives = 1;
            state.ship.invulnerability = 0;
            state.asteroids[1] = state.asteroids[0];
            state.asteroids[1].active = 1;
            state.asteroids[1].size = GAME_ASTEROID_LARGE;
            state.asteroids[1].x = state.ship.x;
            state.asteroids[1].y = state.ship.y;
        }
        if (state.mode == GAME_MODE_PLAYING && frame > 100 && frame < 200) {
            state.ship.invulnerability = 255;
        }

        game_step(&state, &input);
        platform_begin_frame();
        game_render(&state, &renderer);
        platform_end_frame();
    }

    /* save the table like the game does, so the file can be inspected and reloaded */
    {
        uint8_t data[GAME_SCORE_FILE_BYTES];

        game_scores_pack(&state, data);
        platform_save_scores(data, (int) sizeof(data));
    }

    platform_shutdown();
    printf("scripted screens done, mode %d, best %lu\n", state.mode, (unsigned long) state.high_scores[0].score);
    return 0;
}
