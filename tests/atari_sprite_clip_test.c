#include "game.h"
#include "platform.h"

#include <osbind.h>
#include <stdio.h>
#include <string.h>

extern void platform_draw_rock_clipped(GameAsteroid *rock, long cx, long cy);
extern void st_clear_rects(unsigned char *buffer, const short *rects, long count);

static GameState state;
static uint16_t reference[48][5][4];
static uint16_t erase_buffer[320];

static int check_erase(void) {
    int width;
    int left;

    for (width = 1; width <= 20; ++width) {
        for (left = 0; left + width <= 20; ++left) {
            const short rect[4] = {(short) (left * 16), 1, (short) ((left + width) * 16 - 1), 2};
            const unsigned char *pixels = (const unsigned char *) erase_buffer;
            int byte;

            memset(erase_buffer, 0xa5, sizeof(erase_buffer));
            st_clear_rects((unsigned char *) erase_buffer, rect, 1);
            for (byte = 0; byte < (int) sizeof(erase_buffer); ++byte) {
                const int row = byte / 160;
                const int column = byte % 160;
                const int cleared = row >= 1 && row <= 2 && column >= left * 8 &&
                                    column < (left + width) * 8;

                if (pixels[byte] != (cleared ? 0 : 0xa5)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static const uint16_t *draw(GameAsteroid *rock, int x, int y) {
    platform_begin_frame();
    platform_clear_field(NULL);
    platform_draw_rock_clipped(rock, x, y);
    platform_end_frame();
    return (const uint16_t *) Physbase();
}

static int check_edge(GameAsteroid *rock, int x, int y) {
    const uint16_t *screen = draw(rock, x, y);
    int row;
    int group;
    int plane;

    for (row = 0; row < 200; ++row) {
        for (group = 0; group < 20; ++group) {
            const int source_row = row - y + 24;
            const int source_group = group - (x >> 4) + 2;
            const int inside = row >= 16 && row < 192 && group >= 1 && group <= 18;

            for (plane = 0; plane < 4; ++plane) {
                uint16_t expected = inside ? 0 : (plane == 0 || plane == 2 ? 0xffffu : 0);

                if (inside && source_row >= 0 && source_row < 48 &&
                    source_group >= 0 && source_group < 5) {
                    expected = reference[source_row][source_group][plane];
                }
                if (screen[row * 80 + group * 4 + plane] != expected) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

int main(void) {
    int size;
    int shape;
    int orient;
    int phase;
    int passed = check_erase();
    FILE *log;

    if (!platform_init()) {
        return 1;
    }
    game_init(&state, PLATFORM_FIELD_X, PLATFORM_FIELD_Y, PLATFORM_FIELD_WIDTH, PLATFORM_FIELD_HEIGHT);
    platform_build_rock_sprites(&state);
    platform_set_pace_lock(0);

    for (size = 1; size <= 3 && passed; ++size) {
        for (shape = 0; shape < GAME_ROCK_SHAPES && passed; ++shape) {
            GameAsteroid rock;

            game_rock_shape((uint8_t) size, shape, &rock);
            for (orient = 0; orient < (1 << GAME_ROCK_ORIENT_BITS) && passed; ++orient) {
                rock.angle = (uint16_t) (orient << (16 - GAME_ROCK_ORIENT_BITS));
                game_prepare_rock(&state, &rock);
                for (phase = 0; phase < 16 && passed; ++phase) {
                    const uint16_t *screen = draw(&rock, 160 + phase, 104);
                    int row;
                    int edge;
                    int visible = 0;
                    static const int heights[] = {16, 104, 191};

                    for (row = 0; row < 48; ++row) {
                        int word;

                        memcpy(reference[row], screen + (80 + row) * 80 + 8 * 4, sizeof(reference[row]));
                        for (word = 0; word < 20; ++word) {
                            visible |= screen[(80 + row) * 80 + 8 * 4 + word];
                        }
                    }
                    passed = visible != 0;
                    for (edge = 0; edge < 3 && passed; ++edge) {
                        passed = check_edge(&rock, phase, heights[edge]) &&
                                 check_edge(&rock, 304 + phase, heights[edge]);
                    }
                }
            }
        }
    }
    log = fopen("CLIPTEST.LOG", "wb");
    if (log == NULL) {
        platform_shutdown();
        return 1;
    }
    {
        const char *result = passed ? "PASS: all erase widths and rock shapes, sizes, headings, phases and side/corner clips\r\n" :
                                      "FAIL: rectangle erase or rock clipping changed unexpected pixels\r\n";

        (void) fwrite(result, 1, strlen(result), log);
    }
    fclose(log);
    platform_shutdown();
    return passed ? 0 : 1;
}
