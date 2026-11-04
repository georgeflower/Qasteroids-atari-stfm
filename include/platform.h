#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#include "game.h"

/*
 * Screen layout (320x200, 4 bitplanes): a coloured frame with a two-row HUD strip along the
 * top, and a black playing field inside it. All measurements are multiples of 16 pixels
 * horizontally so the field lines up with the ST's 16-pixel bitplane groups.
 */
#define PLATFORM_SCREEN_WIDTH 320
#define PLATFORM_SCREEN_HEIGHT 200
#define PLATFORM_FIELD_X 16
#define PLATFORM_FIELD_Y 16
#define PLATFORM_FIELD_WIDTH 288
#define PLATFORM_FIELD_HEIGHT 176

int platform_init(void);
void platform_shutdown(void);
void platform_poll_input(GameInput *input);
void platform_begin_frame(void);
void platform_draw_line(void *context, int x0, int y0, int x1, int y1, uint8_t color);
void platform_draw_polygon(void *context, const int16_t *points, int count, uint8_t color);
void platform_draw_polygon_offsets(void *context, int center_x, int center_y, const int8_t *off_x,
                                   const int8_t *off_y, int count, uint8_t color, void *cache, uint8_t *cache_valid);
/* Prepare the pre-drawn rocks for the field of this game state (call once after game_init); until then, and for
   rocks with an outline of their own, platform_draw_rocks uses the line drawer. Returns nothing; the memory the
   generated routines use is reported by platform_rock_sprite_bytes(). */
void platform_build_rock_sprites(const GameState *state);
unsigned long platform_rock_sprite_bytes(void);

/* All the rocks that lie inside the field, in assembly (see GameRocksDrawer in game.h). */
void platform_draw_rocks(void *context, GameState *state);

/* The dirty rectangles reported for the page being drawn (for the tests). */
int platform_dirty_count(void);
void platform_dirty_get(int index, short *out);
void platform_draw_text(void *context, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t scale);
void platform_draw_points(void *context, const int16_t *points, int count, uint8_t color);
void platform_clear_field(void *context);
void platform_end_frame(void);
void platform_mark_dirty(void *context, int x0, int y0, int x1, int y1);
/* Shaded ship and glowing bullet sprites (GameSpriteDrawer); returns 1 when drawn. */
int platform_draw_sprite(void *context, int kind, int frame, int x, int y);
int platform_take_elapsed_frames(void);

/* Pacing: the number of vertical blanks each frame is currently held for (1 = 50 fps, 2 = 25 fps ...), chosen from
   how long frames take, with hysteresis so that the speed does not flap. F1 switches it off; the setter is for tests. */
int platform_pace_cadence(void);
void platform_set_pace_lock(int locked);

/* Write one YM2149 register (for the sound engine). */
void platform_sound_write(uint8_t reg, uint8_t value);

/* High score file (ASTROIDS.SCO in the current directory). Load returns the bytes read, 0 if none. */
int platform_load_scores(uint8_t *data, int size);
void platform_save_scores(const uint8_t *data, int size);

/* Settings file (ASTROIDS.CFG in the current directory), same idea as the score file. */
int platform_load_settings(uint8_t *data, int size);
void platform_save_settings(const uint8_t *data, int size);

#endif
