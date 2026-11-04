#include "game.h"
#include "platform.h"
#include "sound.h"

#include <stddef.h>

/* Hand the game's sound events, and the looping sounds, to the sound engine. */
static void play_sounds(GameState *game) {
    const uint16_t events = game_take_sound_events(game);
    int sfx;

    for (sfx = 1; sfx < SFX_COUNT; ++sfx) {
        if (events & (1u << sfx)) {
            sound_play(sfx);
        }
    }
    sound_set_thrust(game->mode == GAME_MODE_PLAYING && game->ship.thrusting && !game->paused);
    sound_set_ufo(game->mode == GAME_MODE_PLAYING && game->ufo_present && !game->paused);
}

int main(void) {
    GameState game;
    GameInput input;
    GameRenderer renderer;
    uint8_t score_file[GAME_SCORE_FILE_BYTES];
    uint8_t settings_file[4];
    int steps;

    renderer.context = NULL;
    renderer.line = platform_draw_line;
    renderer.polygon = platform_draw_polygon;
    renderer.dirty = platform_mark_dirty;
    renderer.text = platform_draw_text;
    renderer.clear_field = platform_clear_field;
    renderer.points = platform_draw_points;
    renderer.polygon_offsets = platform_draw_polygon_offsets;
    renderer.rocks = platform_draw_rocks;
    renderer.sprite = platform_draw_sprite;
    renderer.low_detail = 0;

    if (!platform_init()) {
        return 1;
    }

    sound_init(platform_sound_write);
    game_init(&game, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
    /* the pre-drawn rocks take a second or two to prepare */
    platform_draw_text(NULL, 88, 96, "PREPARING ROCKS", GAME_COLOR_WHITE, GAME_COLOR_BLACK, 1);
    platform_end_frame();
    platform_build_rock_sprites(&game);
    if (platform_load_scores(score_file, (int) sizeof(score_file)) == (int) sizeof(score_file)) {
        (void) game_scores_unpack(&game, score_file);
    }
    if (platform_load_settings(settings_file, (int) sizeof(settings_file)) == (int) sizeof(settings_file) &&
        settings_file[0] == 'C' && settings_file[1] == 'F' && settings_file[2] == '1') {
        game.hyper_on_space = (uint8_t) (settings_file[3] & 1);
    }

    for (;;) {
        platform_poll_input(&input);
        if (game.quit_requested) {
            break;
        }

        /* one game step per vertical blank, so the game keeps its speed when a frame takes longer */
        for (steps = platform_take_elapsed_frames(); steps > 0; --steps) {
            game_step(&game, &input);
            input.typed = 0;        /* typed letters and backspace happen once, however many steps this frame has */
            input.backspace = 0;
            play_sounds(&game);
            sound_tick();
        }
        if (game.scores_changed) {
            game_scores_pack(&game, score_file);
            sound_silence();
            platform_save_scores(score_file, (int) sizeof(score_file));
            game.scores_changed = 0;
        }

        if (game.settings_changed) {
            settings_file[0] = 'C';
            settings_file[1] = 'F';
            settings_file[2] = '1';
            settings_file[3] = game.hyper_on_space;
            sound_silence();
            platform_save_settings(settings_file, (int) sizeof(settings_file));
            game.settings_changed = 0;
        }

        renderer.low_detail = (uint8_t) (platform_pace_cadence() >= 3);
        platform_begin_frame();
        game_render(&game, &renderer);
        platform_end_frame();
    }

    sound_silence();
    platform_shutdown();
    return 0;
}
