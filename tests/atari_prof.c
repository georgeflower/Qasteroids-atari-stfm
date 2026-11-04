/*
 * Sampling profiler run (Hatari): make prof-atari WAVE=4. Plays a fixed scripted game with the real frame
 * loop and samples the program counter about 10000 times a second. Writes C:\PROF.BIN: text base, count, samples.
 */
#include "game.h"
#include "platform.h"
#include "sound.h"

#include <mint/basepage.h>
#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#ifndef PROF_WAVE
#define PROF_WAVE 4
#endif
#if defined(PROF_RENDER_ONLY) || defined(PROF_STEP_ONLY)
#define FRAMES 2500
#else
#define FRAMES 400
#endif
#define MAX_SAMPLES 60000L

extern void prof_isr(void);
extern uint32_t *prof_ptr;
extern uint32_t *prof_end;

static uint32_t samples[MAX_SAMPLES];

#define ST_FRCLOCK (*(volatile uint32_t *) 0x466UL)
#define MFP_IERA (*(volatile uint8_t *) 0xfffa07UL)
#define MFP_IMRA (*(volatile uint8_t *) 0xfffa13UL)
#define MFP_TACR (*(volatile uint8_t *) 0xfffa19UL)
#define MFP_TADR (*(volatile uint8_t *) 0xfffa1fUL)
#define VECTOR_TIMER_A (*(volatile uint32_t *) 0x134UL)

#if defined(PROF_RENDER_ONLY)
static void null_line(void *c, int a, int b, int d, int e, uint8_t f) { (void) c; (void) a; (void) b; (void) d; (void) e; (void) f; }
#endif

int main(void) {
    static GameState state;
    GameRenderer renderer;
    GameInput input;
    int frame;
    uint32_t count;
    uint32_t buckets[5] = {0, 0, 0, 0, 0};
    uint32_t last_clock;
    FILE *file;
    static const char path[] = {'C', ':', 92, 'P', 'R', 'O', 'F', '.', 'B', 'I', 'N', 0};

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
    game_start(&state);
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.wave = (uint8_t) (PROF_WAVE - 1);
    state.rng_state = 12345;
#ifdef PROF_BOSS
    state.wave = (uint8_t) (PROF_BOSS * 5 - 1);
    game_step(&state, &input);
    state.boss.entered = 1;
    state.boss.y = state.boss.target_y;
    state.banner_timer = 0;
#endif
#ifdef PROF_ENEMIES
    state.ufo_timer = 30;
    state.alien_timer = 40;
#endif
    memset(&input, 0, sizeof(input));
    game_step(&state, &input);
    state.banner_timer = 0;

    prof_ptr = samples;
    prof_end = samples + MAX_SAMPLES;
    VECTOR_TIMER_A = (uint32_t) prof_isr;
    MFP_TACR = 0;
    MFP_TADR = 15;
    MFP_IERA |= 0x20;
    MFP_IMRA |= 0x20;
    MFP_TACR = 3;   /* divide by 16: 153600 / 15 = 10240 Hz */

#ifdef PROF_STEP_ONLY
    /* the natural rocks of the wave, shot at (and so split) all the time */
    for (frame = 0; frame < FRAMES; ++frame) {
        state.ship.invulnerability = 255;
        input.left = (uint8_t) ((frame / 40) & 1);
        input.right = (uint8_t) (!input.left);
        input.thrust = (uint8_t) ((frame / 25) & 1);
        input.fire = (uint8_t) ((frame / 6) & 1);
        game_step(&state, &input);
    }
#elif defined(PROF_RENDER_ONLY)
    /* one parked rock and nothing else, rendering only, with the drawing calls stubbed out */
    memset(state.asteroids, 0, sizeof(state.asteroids));
    state.asteroids[0].active = 1;
    state.asteroids[0].size = GAME_ASTEROID_SMALL;
    state.asteroids[0].point_count = 8;
    state.asteroids[0].x = 100L << GAME_FIX_SHIFT;
    state.asteroids[0].y = 100L << GAME_FIX_SHIFT;
    renderer.line = null_line;
    renderer.polygon = NULL;
    renderer.polygon_offsets = NULL;
    renderer.sprite = NULL;
    renderer.text = NULL;
    renderer.points = NULL;
    renderer.dirty = NULL;
    for (frame = 0; frame < FRAMES; ++frame) {
        state.frame = (uint16_t) frame;
        game_render(&state, &renderer);
    }
#else
    last_clock = ST_FRCLOCK;
    for (frame = 0; frame < FRAMES; ++frame) {
        state.ship.invulnerability = 255;
        input.left = (uint8_t) ((frame / 40) & 1);
        input.right = (uint8_t) (!input.left);
        input.thrust = (uint8_t) ((frame / 25) & 1);
        input.fire = (uint8_t) ((frame / 6) & 1);
        game_step(&state, &input);
        (void) game_take_sound_events(&state);
        platform_begin_frame();
        game_render(&state, &renderer);
        platform_end_frame();
        {
            const uint32_t now = ST_FRCLOCK;
            uint32_t taken = now - last_clock;

            last_clock = now;
            ++buckets[taken > 4 ? 4 : taken];
        }
    }
#endif

    (void) last_clock;
    MFP_TACR = 0;
    MFP_IMRA &= (uint8_t) ~0x20;
    MFP_IERA &= (uint8_t) ~0x20;

    count = (uint32_t) (prof_ptr - samples);
    file = fopen(path, "wb");
    if (file != NULL) {
        uint32_t header[2];

        header[0] = (uint32_t) _base->p_tbase;
        header[1] = count;
        fwrite(header, sizeof(header), 1, file);
        fwrite(samples, sizeof(uint32_t), count, file);
        fclose(file);
    }
    {
        static const char log_path[] = {'C', ':', 92, 'P', 'R', 'O', 'F', '.', 'L', 'O', 'G', 0};
        FILE *log = fopen(log_path, "w");

        if (log != NULL) {
            fprintf(log, "wave %d: frames taking 1,2,3,4+ vblanks: %lu %lu %lu %lu", PROF_WAVE, (unsigned long) buckets[1],
                    (unsigned long) buckets[2], (unsigned long) buckets[3], (unsigned long) buckets[4]);
            fputc(10, log);
            fclose(log);
        }
    }
    platform_shutdown();
    return 0;
}
