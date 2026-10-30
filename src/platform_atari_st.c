#include "platform.h"

#include "st_text.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(ATARI_ST_TARGET)
#include <osbind.h>

#define SCREEN_BYTES 32000
#define ST_PALETTE_COLORS 16

#define ST_FRCLOCK (*(volatile uint32_t *) 0x466UL)
#define ST_HZ200 (*(volatile uint32_t *) 0x4baUL)
#define ST_VIDEO_BASE_HIGH (*(volatile uint8_t *) 0xff8201UL)
#define ST_VIDEO_BASE_MID (*(volatile uint8_t *) 0xff8203UL)
#define ST_HW_PALETTE ((volatile uint16_t *) 0xff8240UL)
#define ST_PSG_SELECT (*(volatile uint8_t *) 0xff8800UL)
#define ST_PSG_DATA (*(volatile uint8_t *) 0xff8802UL)

/* The playing field in 16-pixel groups and rows, for the rectangle clear. */
#define FIELD_GROUP0 (PLATFORM_FIELD_X / 16)
#define FIELD_GROUP1 ((PLATFORM_FIELD_X + PLATFORM_FIELD_WIDTH) / 16 - 1)
#define FIELD_Y0 PLATFORM_FIELD_Y
#define FIELD_Y1 (PLATFORM_FIELD_Y + PLATFORM_FIELD_HEIGHT - 1)
#define FIELD_X0 PLATFORM_FIELD_X
#define FIELD_X1 (PLATFORM_FIELD_X + PLATFORM_FIELD_WIDTH - 1)

#define SCORE_FILE "ASTROIDS.SCO"
#define SETTINGS_FILE "ASTROIDS.CFG"
#define MAX_DIRTY_RECTS 96
#define POLYGON_MAX_POINTS 24

extern void st_clear_buffer(unsigned char *buffer);
extern void st_draw_line_low(unsigned char *buffer, long x0, long y0, long x1, long y1, long color);
extern void st_draw_poly_m3(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m3(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m5(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m5(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m6(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m6(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m7(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m7(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m9(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m9(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m10(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m10(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m11(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m11(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m12(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m12(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m13(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m13(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m14(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m14(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_poly_m15(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_polyline_m15(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_plot_points(unsigned char *buffer, const short *points, long count, long color);
extern void st_draw_pair(unsigned char *buffer, long x, long y, long plane_offset);
extern void st_draw_polyline(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_line_plane(unsigned char *buffer, long x0, long y0, long x1, long y1, long plane_offset);
extern void st_draw_poly_plane(unsigned char *buffer, const short *points, long count, long plane_offset);
extern void st_draw_rock(unsigned char *buffer, long cx, long cy, const short *cache, long plane_offset);
extern void st_clear_rects(unsigned char *buffer, const short *rects, long count);
extern void st_clear_rect(unsigned char *buffer, long group0, long group1, long y0, long y1);
static void pace_poll_key(void);
extern void st_ikbd_install(void);
extern void st_ikbd_remove(void);

/* Written by the IKBD interrupt handler in st_ikbd.S: 1 while the key is held; the make codes in the order
   pressed (a ring of 16, the handler moves head, we move tail); the last joystick byte of each port. */
volatile unsigned char st_key_state[128];
extern volatile unsigned char st_key_queue[16];
extern volatile unsigned char st_key_head;
extern volatile unsigned char st_key_tail;
extern volatile unsigned char st_joystick[2];

typedef struct DirtyRect {
    short x0;
    short y0;
    short x1;
    short y1;
} DirtyRect;

extern void st_draw_rocks(GameState *state, unsigned char *buffer, DirtyRect *rects, int *count, unsigned char *full, long max_rects, uint16_t *const *sprites);

static unsigned char screen_storage[2][SCREEN_BYTES + 255];
static unsigned char *screen_pages[2];
static unsigned char *draw_buffer;
static unsigned char *show_buffer;
static void *original_physbase;
static void *original_logbase;
static int original_resolution;
static uint16_t original_palette[ST_PALETTE_COLORS];
/* Per screen page: what was drawn on it last time, so only that gets erased. */
static DirtyRect dirty_rects[2][MAX_DIRTY_RECTS];
static int dirty_count[2];
static unsigned char dirty_full[2];
static uint32_t last_frame_clock;
static uint32_t pace_work_start;   /* see the pacing section below */
static uint32_t pace_last_flip;
static long saved_ssp;
static int entered_supervisor;

static unsigned char *aligned_screen(int index) {
    unsigned long address = (unsigned long) screen_storage[index];
    address = (address + 255u) & ~255u;
    return (unsigned char *) address;
}

/* Wait for the next vertical blank; give up after ~50 ms so a dead VBL cannot hang the machine. */
static void wait_vbl(void) {
    const uint32_t frame = ST_FRCLOCK;
    const uint32_t start = ST_HZ200;

    while (ST_FRCLOCK == frame && (ST_HZ200 - start) < 10u) {
    }
}

static void set_palette(void) {
    /* Lovable "Classic" theme, quantised to the ST's 3 bits per channel. The line colours
       (ship, rocks) sit on single bitplanes (1, 2, 4, 8) for fast drawing. */
    static const uint16_t palette[ST_PALETTE_COLORS] = {
        0x000, 0x272, 0x777, 0x741,   /* black field, ship+bullets, large rock, thrust flame */
        0x467, 0x124, 0x770, 0x722,   /* medium rock, frame, yellow, red */
        0x247, 0x555, 0x757, 0x333,   /* small rock, grey, magenta, dim star */
        0x057, 0x531, 0x527, 0x666    /* cyan, brown alien, purple swarm, bright star */
    };
    int i;

    for (i = 0; i < ST_PALETTE_COLORS; ++i) {
        ST_HW_PALETTE[i] = palette[i];
    }
}

static void show_screen(const unsigned char *buffer) {
    const unsigned long address = (unsigned long) buffer;

    ST_VIDEO_BASE_HIGH = (uint8_t) (address >> 16);
    ST_VIDEO_BASE_MID = (uint8_t) (address >> 8);
}

/* Fill a whole screen with the frame colour, then cut out the black playing field. */
static void draw_frame(unsigned char *buffer) {
    unsigned short *words = (unsigned short *) buffer;
    int index;
    int plane;

    for (index = 0; index < SCREEN_BYTES / 8; ++index) {
        for (plane = 0; plane < 4; ++plane) {
            *words++ = (unsigned short) ((((GAME_COLOR_FRAME >> plane) & 1) != 0) ? 0xffffu : 0u);
        }
    }
    st_clear_rect(buffer, FIELD_GROUP0, FIELD_GROUP1, FIELD_Y0, FIELD_Y1);
}

static int page_index(void) {
    return (draw_buffer == screen_pages[0]) ? 0 : 1;
}

static void enter_video(void) {
    if (Getrez() != 0) {
        Setscreen((void *) -1L, (void *) -1L, 0);
        wait_vbl();
        wait_vbl();
    }
    draw_frame(screen_pages[0]);
    draw_frame(screen_pages[1]);
    dirty_count[0] = 0;
    dirty_count[1] = 0;
    dirty_full[0] = 0;
    dirty_full[1] = 0;
    show_buffer = screen_pages[0];
    draw_buffer = screen_pages[1];
    show_screen(show_buffer);
    wait_vbl();
    set_palette();
    last_frame_clock = ST_FRCLOCK - 1;
    pace_last_flip = ST_FRCLOCK;
    pace_work_start = ST_HZ200;
}

int platform_init(void) {
    static const char ikbd_game_mode[] = { 0x12, 0x14 };  /* mouse off, joystick events on */
    int i;

    entered_supervisor = 0;
    if (Super((void *) 1L) == 0) {
        saved_ssp = Super((void *) 0L);
        entered_supervisor = 1;
    }

    if (Getrez() == 2) {
        (void) Cconws("Atari colour monitor required.\r\n");
        if (entered_supervisor) {
            (void) Super((void *) saved_ssp);
            entered_supervisor = 0;
        }
        return 0;
    }

    original_physbase = Physbase();
    original_logbase = Logbase();
    original_resolution = Getrez();
    for (i = 0; i < ST_PALETTE_COLORS; ++i) {
        original_palette[i] = ST_HW_PALETTE[i];
    }

    screen_pages[0] = aligned_screen(0);
    screen_pages[1] = aligned_screen(1);
    memset((void *) st_key_state, 0, sizeof(st_key_state));
    st_joystick[0] = 0;
    st_joystick[1] = 0;
    st_key_head = 0;
    st_key_tail = 0;

    enter_video();
    (void) Ikbdws(1, ikbd_game_mode);
    st_ikbd_install();
    return 1;
}

void platform_shutdown(void) {
    /* Joystick events first, relative mouse last: a joystick command stops port 0 being scanned as a
       mouse until the next mouse command, so ending with 0x14 leaves the GEM pointer dead.
       The commands go out while our handler is still installed, and then we wait a few frames: a joystick packet
       that is already on its way is read (and skipped) by our handler, so the TOS handler never gets half a packet
       and loses its place. Only then does TOS get its handler back, and the mouse command is repeated for good measure. */
    static const char ikbd_tos_mode[] = { 0x14, 0x08 };
    static const char ikbd_mouse_on[] = { 0x08 };
    int i;

    if (!entered_supervisor) {
        return;
    }

    (void) Ikbdws(1, ikbd_tos_mode);
    wait_vbl();
    wait_vbl();
    wait_vbl();
    st_ikbd_remove();
    (void) Ikbdws(0, ikbd_mouse_on);
    Setscreen(original_logbase, original_physbase, original_resolution);
    wait_vbl();
    wait_vbl();
    for (i = 0; i < ST_PALETTE_COLORS; ++i) {
        ST_HW_PALETTE[i] = original_palette[i];
    }
    (void) Super((void *) saved_ssp);
    entered_supervisor = 0;
}

/* The letter on the key with this scan code (US layout positions, as on the ST keyboards), or 0. */
static char letter_for_scancode(unsigned scancode) {
    static const char top_row[] = "QWERTYUIOP";
    static const char home_row[] = "ASDFGHJKL";
    static const char bottom_row[] = "ZXCVBNM";

    if (scancode >= 0x10u && scancode <= 0x19u) {
        return top_row[scancode - 0x10u];
    }
    if (scancode >= 0x1eu && scancode <= 0x26u) {
        return home_row[scancode - 0x1eu];
    }
    if (scancode >= 0x2cu && scancode <= 0x32u) {
        return bottom_row[scancode - 0x2cu];
    }
    return 0;
}

void platform_poll_input(GameInput *input) {
    /* Joystick port 1 only. Port 0 is the mouse port: with the keyboard processor in joystick mode a mouse plugged in
       there looks like a joystick whose direction lines flicker or stay stuck (up is thrust), so it is ignored. */
    const unsigned char joystick = st_joystick[1];

    memset(input, 0, sizeof(*input));

    input->left = (uint8_t) (st_key_state[0x1eu] || st_key_state[0x4bu] || (joystick & 4u));      /* A, left */
    input->right = (uint8_t) (st_key_state[0x20u] || st_key_state[0x4du] || (joystick & 8u));     /* D, right */
    input->thrust = (uint8_t) (st_key_state[0x11u] || st_key_state[0x48u] || (joystick & 1u));    /* W, up */
    input->down = (uint8_t) (st_key_state[0x1fu] || st_key_state[0x50u] || (joystick & 2u));      /* S, down */
    input->space = st_key_state[0x39u];
    input->fire_alt = (uint8_t) (st_key_state[0x1du] || st_key_state[0x38u] || (joystick & 0x80u));   /* Ctrl, Alt, button */
    input->fire = (uint8_t) (input->space || input->fire_alt);
    input->hyperspace = st_key_state[0x23u];                                                       /* H */
    input->start = (uint8_t) (st_key_state[0x39u] || st_key_state[0x1cu] || st_key_state[0x72u] ||
                              (joystick & 0x80u));                                                 /* space, return, enter, button */
    input->pause = st_key_state[0x19u];                                                            /* P */
    input->escape = st_key_state[0x01u];                                                           /* Esc */

    /* typing: one queued key press per poll, so nothing is lost when several arrive in one frame */
    if (st_key_tail != st_key_head) {
        const unsigned scancode = st_key_queue[st_key_tail];

        st_key_tail = (unsigned char) ((st_key_tail + 1u) & 15u);
        input->typed = (uint8_t) letter_for_scancode(scancode);
        input->backspace = (uint8_t) (scancode == 0x0eu);
    }
    pace_poll_key();
}

void platform_begin_frame(void) {
    const int page = page_index();

    if (dirty_full[page]) {
        st_clear_rect(draw_buffer, FIELD_GROUP0, FIELD_GROUP1, FIELD_Y0, FIELD_Y1);
    } else {
        st_clear_rects(draw_buffer, (const short *) dirty_rects[page], dirty_count[page]);
    }
    dirty_count[page] = 0;
    dirty_full[page] = 0;
}

void platform_clear_field(void *context) {
    (void) context;
    st_clear_rect(draw_buffer, FIELD_GROUP0, FIELD_GROUP1, FIELD_Y0, FIELD_Y1);
}

/* ---- pre-drawn rocks ----
 * For every (size, shared shape, orientation, x offset within a 16-pixel group) the outline is drawn once into a
 * scratch bitmap, and a tiny routine is generated that ORs exactly the words the outline touches into the
 * screen: `ori.w #mask,offset(a0)` per word, then rts. Drawing a rock is then a jsr to one of these. */
#define SPRITE_ORIENTS (1 << GAME_ROCK_ORIENT_BITS)
#define SPRITE_COUNT (3 * GAME_ROCK_SHAPES * SPRITE_ORIENTS * 16)
#define SPRITE_ROWS 48
#define SPRITE_CENTER_ROW 24
#define SPRITE_CENTER_X 32
#define SPRITE_POOL_WORDS (0x10000UL << GAME_ROCK_ORIENT_BITS)

static uint16_t *rock_sprites[SPRITE_COUNT];
static int rock_sprites_ready;
static unsigned long rock_sprite_words;
static uint16_t rock_code_pool[SPRITE_POOL_WORDS];
static uint16_t sprite_scratch[SPRITE_ROWS * 80];

void platform_build_rock_sprites(const GameState *state) {
    uint16_t *out = rock_code_pool;
    uint16_t *const limit = rock_code_pool + SPRITE_POOL_WORDS;
    int size;
    int shape;
    int orient;
    int phase;

    memset(rock_sprites, 0, sizeof(rock_sprites));
    for (size = GAME_ASTEROID_SMALL; size <= GAME_ASTEROID_LARGE; ++size) {
        for (shape = 0; shape < GAME_ROCK_SHAPES; ++shape) {
            for (orient = 0; orient < SPRITE_ORIENTS; ++orient) {
                GameAsteroid rock;

                game_rock_shape(size, shape, &rock);
                rock.angle = (uint16_t) ((orient << (6 - GAME_ROCK_ORIENT_BITS)) << 10);
                game_prepare_rock(state, &rock);
                for (phase = 0; phase < 16; ++phase) {
                    const int index = ((((size - 1) * GAME_ROCK_SHAPES + shape) * SPRITE_ORIENTS + orient) << 4) + phase;
                    short points[GAME_MAX_ASTEROID_POINTS * 2];
                    int vertex;
                    int row;
                    int group;

                    for (vertex = 0; vertex < rock.draw_count; ++vertex) {
                        points[vertex * 2] = (short) (SPRITE_CENTER_X + phase + rock.off_x[vertex]);
                        points[vertex * 2 + 1] = (short) (SPRITE_CENTER_ROW + rock.off_y[vertex]);
                    }
                    st_clear_rect((unsigned char *) sprite_scratch, 0, 4, 0, SPRITE_ROWS - 1);
                    st_draw_poly_plane((unsigned char *) sprite_scratch, points, rock.draw_count, 0);

                    if ((unsigned long) (limit - out) < 3UL * SPRITE_ROWS * 5 + 1) {
                        continue;   /* out of room: this one stays on the line drawer */
                    }
                    rock_sprites[index] = out;
                    for (row = SPRITE_CENTER_ROW + rock.bound_y0; row <= SPRITE_CENTER_ROW + rock.bound_y1; ++row) {
                        for (group = 0; group < 5; ++group) {
                            const uint16_t word = sprite_scratch[row * 80 + group * 4];

                            if (word != 0) {
                                *out++ = 0x0068;   /* ori.w #imm,d16(a0) */
                                *out++ = word;
                                *out++ = (uint16_t) ((row - SPRITE_CENTER_ROW) * 160 + (group - SPRITE_CENTER_X / 16) * 8);
                            }
                        }
                    }
                    *out++ = 0x4e75;   /* rts */
                }
            }
        }
    }
    rock_sprite_words = (unsigned long) (out - rock_code_pool);
    rock_sprites_ready = 1;
}

unsigned long platform_rock_sprite_bytes(void) {
    return rock_sprite_words * 2UL;
}

void platform_draw_rocks(void *context, GameState *state) {
    const int page = page_index();

    (void) context;
    st_draw_rocks(state, draw_buffer, dirty_rects[page], &dirty_count[page], &dirty_full[page], MAX_DIRTY_RECTS,
                  rock_sprites_ready ? rock_sprites : NULL);
}

int platform_dirty_count(void) {
    return dirty_count[page_index()];
}

void platform_dirty_get(int index, short *out) {
    const DirtyRect *rect = &dirty_rects[page_index()][index];

    out[0] = rect->x0;
    out[1] = rect->y0;
    out[2] = rect->x1;
    out[3] = rect->y1;
}

void platform_mark_dirty(void *context, int x0, int y0, int x1, int y1) {
    const int page = page_index();
    DirtyRect *rect;

    (void) context;

    if (x0 < FIELD_X0) {
        x0 = FIELD_X0;
    }
    if (y0 < FIELD_Y0) {
        y0 = FIELD_Y0;
    }
    if (x1 > FIELD_X1) {
        x1 = FIELD_X1;
    }
    if (y1 > FIELD_Y1) {
        y1 = FIELD_Y1;
    }
    if (x0 > x1 || y0 > y1) {
        return;
    }
    if (dirty_count[page] >= MAX_DIRTY_RECTS) {
        dirty_full[page] = 1;
        return;
    }

    rect = &dirty_rects[page][dirty_count[page]++];
    rect->x0 = (short) x0;
    rect->y0 = (short) y0;
    rect->x1 = (short) x1;
    rect->y1 = (short) y1;
}

/* Vertical blanks since the last call (1 when the frame kept up), so the game can step once per blank. */
int platform_take_elapsed_frames(void) {
    const uint32_t now = ST_FRCLOCK;
    uint32_t elapsed = now - last_frame_clock;

    last_frame_clock = now;
    if (elapsed < 1u) {
        elapsed = 1u;
    }
    if (elapsed > 4u) {
        elapsed = 4u;
    }
    return (int) elapsed;
}

/* Cohen-Sutherland outcodes for the playing field (inclusive bounds). */
static int outcode(long x, long y) {
    int code = 0;

    if (x < FIELD_X0) {
        code |= 1;
    } else if (x > FIELD_X1) {
        code |= 2;
    }
    if (y < FIELD_Y0) {
        code |= 4;
    } else if (y > FIELD_Y1) {
        code |= 8;
    }
    return code;
}

/* (a * b) / c with a single muls.w and divs.w instead of the 32-bit library division. All three are small,
   and |b| <= |c|, so the quotient fits in 16 bits. Truncates toward zero, like C. */
static long muldiv16(long a, long b, long c) {
    long value = (long) (short) a * (short) b;

    __asm__ ("divs.w %1,%0\n\text.l %0" : "+d" (value) : "dm" ((short) c) : "cc");
    return value;
}

/* Clip a line to the playing field. Returns 0 when nothing of it is visible. */
static int clip_line(long *x0, long *y0, long *x1, long *y1) {
    int code0 = outcode(*x0, *y0);
    int code1 = outcode(*x1, *y1);
    int guard;

    for (guard = 0; guard < 8; ++guard) {
        const int code = code0 ? code0 : code1;
        long x = 0;
        long y = 0;

        if ((code0 | code1) == 0) {
            return 1;
        }
        if ((code0 & code1) != 0) {
            return 0;
        }

        if (code & 8) {
            y = FIELD_Y1;
            x = *x0 + muldiv16(*x1 - *x0, y - *y0, *y1 - *y0);
        } else if (code & 4) {
            y = FIELD_Y0;
            x = *x0 + muldiv16(*x1 - *x0, y - *y0, *y1 - *y0);
        } else if (code & 2) {
            x = FIELD_X1;
            y = *y0 + muldiv16(*y1 - *y0, x - *x0, *x1 - *x0);
        } else {
            x = FIELD_X0;
            y = *y0 + muldiv16(*y1 - *y0, x - *x0, *x1 - *x0);
        }

        if (code == code0) {
            *x0 = x;
            *y0 = y;
            code0 = outcode(x, y);
        } else {
            *x1 = x;
            *y1 = y;
            code1 = outcode(x, y);
        }
    }
    return 0;
}

static int inside_field(int x, int y) {
    /* one unsigned compare per axis instead of two signed ones */
    return (uint16_t) (x - FIELD_X0) <= (uint16_t) (FIELD_X1 - FIELD_X0) &&
           (uint16_t) (y - FIELD_Y0) <= (uint16_t) (FIELD_Y1 - FIELD_Y0);
}

static long plane_offset_for(uint8_t color) {
    return (color == 1) ? 0 : (color == 2) ? 2 : (color == 4) ? 4 : 6;
}

/* Polygon and polyline drawers by colour: one bitplane, or one pass over several (see st_video.S). */
typedef void (*PolyDrawer)(unsigned char *buffer, const short *points, long count, long plane_offset);
static const PolyDrawer poly_drawers[16] = {0, st_draw_poly_plane, st_draw_poly_plane, st_draw_poly_m3, st_draw_poly_plane, st_draw_poly_m5, st_draw_poly_m6, st_draw_poly_m7, st_draw_poly_plane, st_draw_poly_m9, st_draw_poly_m10, st_draw_poly_m11, st_draw_poly_m12, st_draw_poly_m13, st_draw_poly_m14, st_draw_poly_m15};
static const PolyDrawer polyline_drawers[16] = {0, st_draw_polyline, st_draw_polyline, st_draw_polyline_m3, st_draw_polyline, st_draw_polyline_m5, st_draw_polyline_m6, st_draw_polyline_m7, st_draw_polyline, st_draw_polyline_m9, st_draw_polyline_m10, st_draw_polyline_m11, st_draw_polyline_m12, st_draw_polyline_m13, st_draw_polyline_m14, st_draw_polyline_m15};

/* what the drawers take as plane offset: the plane for a one-plane colour, none for the others */
static long plane_argument(uint8_t color) {
    return (color == 1 || color == 2 || color == 4 || color == 8) ? plane_offset_for(color) : 0;
}

void platform_draw_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    long cx0 = x0;
    long cy0 = y0;
    long cx1 = x1;
    long cy1 = y1;

    (void) context;

    if (!inside_field(x0, y0) || !inside_field(x1, y1)) {
        if (!clip_line(&cx0, &cy0, &cx1, &cy1)) {
            return;
        }
    }

    if (color == 1 || color == 2 || color == 4 || color == 8) {
        if (cy1 == cy0 && cx1 == cx0 + 1) {
            st_draw_pair(draw_buffer, cx0, cy0, plane_offset_for(color));   /* a bullet */
        } else {
            st_draw_line_plane(draw_buffer, cx0, cy0, cx1, cy1, plane_offset_for(color));
        }
    } else if (color >= 1 && color <= 15) {
        short ends[4];

        ends[0] = (short) cx0;
        ends[1] = (short) cy0;
        ends[2] = (short) cx1;
        ends[3] = (short) cy1;
        polyline_drawers[color](draw_buffer, ends, 2, 0);
    } else {
        st_draw_line_low(draw_buffer, cx0, cy0, cx1, cy1, color);
    }
}

/* Work out, once per outline, what st_draw_rock needs for every edge (see st_video.S for the layout). */
static void build_rock_cache(int16_t *cache, const int8_t *off_x, const int8_t *off_y, int count) {
    int16_t *out = cache + 3;
    int edges = 0;
    int index;

    cache[1] = off_x[0];
    cache[2] = off_y[0];
    for (index = 0; index < count; ++index) {
        const int next = (index + 1 == count) ? 0 : index + 1;
        const int dx = off_x[next] - off_x[index];
        const int dy = off_y[next] - off_y[index];
        const int adx = dx < 0 ? -dx : dx;
        const int ady = dy < 0 ? -dy : dy;
        const int major = adx >= ady ? adx : ady;

        if (major == 0) {
            continue;
        }
        out[0] = (int16_t) adx;
        out[1] = (int16_t) ady;
        out[2] = (int16_t) (major >> 1);
        out[3] = (int16_t) (major - 1);
        out[4] = (int16_t) (dy < 0 ? -160 : 160);
        out[5] = (int16_t) (((adx >= ady) ? 0 : 2 * 4) + (dx < 0 ? 4 : 0));
        out += 6;
        ++edges;
    }
    cache[0] = (int16_t) (edges - 1);
}

/* Called from st_rocks.S when a rock's prepared edge list is out of date. */
void platform_prepare_rock(GameAsteroid *asteroid) {
    build_rock_cache((int16_t *) asteroid->draw_cache, asteroid->off_x, asteroid->off_y, asteroid->draw_count);
    asteroid->draw_cache_valid = 1;
}

/* colour must be one of 1, 2, 4, 8 and the outline must lie inside the field */
void platform_draw_polygon_offsets(void *context, int center_x, int center_y, const int8_t *off_x,
                                   const int8_t *off_y, int count, uint8_t color, void *cache, uint8_t *cache_valid) {
    (void) context;
    if (!*cache_valid) {
        build_rock_cache((int16_t *) cache, off_x, off_y, count);
        *cache_valid = 1;
    }
    st_draw_rock(draw_buffer, center_x, center_y, (const short *) cache, plane_offset_for(color));
}

void platform_draw_polygon(void *context, const int16_t *points, int count, uint8_t color) {
    int index;

    if (color >= 1 && color <= 15 && count <= POLYGON_MAX_POINTS) {
        uint8_t inside[POLYGON_MAX_POINTS];
        const long plane = plane_argument(color);
        int outside_points = 0;
        int run_start = -1;

        for (index = 0; index < count; ++index) {
            /* one unsigned compare per axis instead of two signed ones */
            inside[index] = (uint8_t) ((uint16_t) (points[index * 2] - FIELD_X0) <= (uint16_t) (FIELD_X1 - FIELD_X0) &&
                                       (uint16_t) (points[index * 2 + 1] - FIELD_Y0) <= (uint16_t) (FIELD_Y1 - FIELD_Y0));
            outside_points += !inside[index];
        }
        if (outside_points == 0) {
            poly_drawers[color](draw_buffer, points, count, plane);
            return;
        }

        /* partly off the field: runs of edges that lie inside go through the fast polyline drawer, the few
           edges that cross the border are clipped one by one */
        for (index = 0; index + 1 < count; ++index) {
            if (inside[index] && inside[index + 1]) {
                if (run_start < 0) {
                    run_start = index;
                }
                continue;
            }
            if (run_start >= 0) {
                polyline_drawers[color](draw_buffer, points + run_start * 2, index - run_start + 1, plane);
                run_start = -1;
            }
            platform_draw_line(context, points[index * 2], points[index * 2 + 1], points[index * 2 + 2],
                               points[index * 2 + 3], color);
        }
        if (run_start >= 0) {
            polyline_drawers[color](draw_buffer, points + run_start * 2, count - run_start, plane);
        }
        platform_draw_line(context, points[(count - 1) * 2], points[(count - 1) * 2 + 1], points[0], points[1], color);
        return;
    }

    /* a mixed-plane colour: edge by edge, with clipping */
    for (index = 0; index < count; ++index) {
        const int next = (index + 1 == count) ? 0 : index + 1;
        platform_draw_line(context, points[index * 2], points[index * 2 + 1], points[next * 2],
                           points[next * 2 + 1], color);
    }
}

void platform_draw_points(void *context, const int16_t *points, int count, uint8_t color) {
    (void) context;
    st_plot_points(draw_buffer, points, count, color);
}

void platform_draw_text(void *context, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t scale) {
    (void) context;
    st_text_draw(draw_buffer, x, y, text, fg, bg, scale);
}

/* ---- pacing ----
 * A frame that takes a little more than 20 ms gets shown for two vertical blanks, one that takes a little less
 * for one, so a load near the limit makes the speed flap between 50 and 25 fps. Instead the number of blanks per
 * frame (the cadence) is chosen from how long frames take and only lowered again after a second of easier
 * frames, and every frame is held for that many blanks. F1 switches this off (frames are then shown as soon as
 * they are ready) and on again. */
#define PACE_MARGIN_MS 2
#define PACE_HICCUP_MS 120
#define PACE_RELAX_FRAMES 50
#define PACE_MESSAGE_FRAMES 100

static int pace_locked = 1;
static int pace_cadence = 1;
static int pace_easy_frames;
static uint32_t pace_average_ms = 10;
static int pace_message_frames;
static int pace_f1_was_down;

int platform_pace_cadence(void) {
    return pace_cadence;
}

void platform_set_pace_lock(int locked) {
    pace_locked = locked;
    pace_cadence = 1;
    pace_easy_frames = 0;
    pace_average_ms = 10;
    pace_work_start = ST_HZ200;
    pace_last_flip = ST_FRCLOCK;
}

/* Called once a frame with the time the work took; picks the cadence (blanks per frame). */
static void pace_update(uint32_t work_ms) {
    int needed;

    if (work_ms > PACE_HICCUP_MS) {
        return;   /* a disk write or the like, not what frames cost: do not let it move the cadence */
    }
    pace_average_ms = (pace_average_ms * 3u + work_ms) / 4u;
    needed = (int) ((pace_average_ms + PACE_MARGIN_MS + 19u) / 20u);
    if (needed < 1) {
        needed = 1;
    }
    if (needed > 4) {
        needed = 4;
    }
    if (needed > pace_cadence) {
        pace_cadence = needed;
        pace_easy_frames = 0;
    } else if (needed < pace_cadence) {
        if (++pace_easy_frames >= PACE_RELAX_FRAMES) {
            --pace_cadence;
            pace_easy_frames = 0;
        }
    } else {
        pace_easy_frames = 0;
    }
}

/* F1 toggles the lock; the state is shown for a couple of seconds in the frame below the field. */
static void pace_poll_key(void) {
    const int down = st_key_state[0x3bu] != 0;

    if (down && !pace_f1_was_down) {
        pace_locked = !pace_locked;
        pace_message_frames = PACE_MESSAGE_FRAMES;
    }
    pace_f1_was_down = down;
}

static void pace_draw_message(void) {
    if (pace_message_frames > 0) {
        st_text_draw(draw_buffer, 8, 192, pace_locked ? "F1 PACING: STEADY   " : "F1 PACING: FREE     ", 2,
                     GAME_COLOR_FRAME, 1);
        --pace_message_frames;
        if (pace_message_frames == 0) {
            pace_message_frames = -2;   /* blank it on both screen buffers */
        }
    } else if (pace_message_frames < 0) {
        st_text_draw(draw_buffer, 8, 192, "                   ", GAME_COLOR_FRAME, GAME_COLOR_FRAME, 1);
        ++pace_message_frames;
    }
}

void platform_end_frame(void) {
    unsigned char *finished = draw_buffer;
    const uint32_t work_ticks = ST_HZ200 - pace_work_start;

    pace_update(work_ticks * 5u);
    pace_draw_message();

    /* The ST only reloads the screen address at the vertical blank, so the old
       page stays on screen until then: request the flip, wait for the blank that
       carries it out, and only then draw into the page that was on screen. */
    show_screen(finished);
    wait_vbl();
    if (pace_locked) {
        while ((int32_t) (ST_FRCLOCK - (pace_last_flip + (uint32_t) pace_cadence)) < 0) {
            wait_vbl();
        }
    }
    pace_last_flip = ST_FRCLOCK;
    draw_buffer = show_buffer;
    show_buffer = finished;
    pace_work_start = ST_HZ200;
}

/* Register select and data write must not be split by an interrupt (TOS also uses the chip for floppy select). */
void platform_sound_write(uint8_t reg, uint8_t value) {
    unsigned short saved_sr;

    __asm__ volatile ("move.w %%sr,%0" : "=d" (saved_sr) : : "memory");
    __asm__ volatile ("ori.w #0x0700,%%sr" : : : "cc", "memory");
    ST_PSG_SELECT = reg;
    ST_PSG_DATA = value;
    __asm__ volatile ("move.w %0,%%sr" : : "d" (saved_sr) : "cc", "memory");
}

int platform_load_scores(uint8_t *data, int size) {
    FILE *file = fopen(SCORE_FILE, "rb");
    int count = 0;

    if (file != NULL) {
        count = (int) fread(data, 1, (size_t) size, file);
        fclose(file);
    }
    return count;
}

int platform_load_settings(uint8_t *data, int size) {
    FILE *file = fopen(SETTINGS_FILE, "rb");
    int count = 0;

    if (file != NULL) {
        count = (int) fread(data, 1, (size_t) size, file);
        fclose(file);
    }
    return count;
}

void platform_save_settings(const uint8_t *data, int size) {
    FILE *file = fopen(SETTINGS_FILE, "wb");

    if (file != NULL) {
        (void) fwrite(data, 1, (size_t) size, file);
        fclose(file);
    }
}

void platform_save_scores(const uint8_t *data, int size) {
    FILE *file = fopen(SCORE_FILE, "wb");

    if (file != NULL) {
        (void) fwrite(data, 1, (size_t) size, file);
        fclose(file);
    }
}

#else
int platform_init(void) {
    return 0;
}

void platform_shutdown(void) {
}

void platform_poll_input(GameInput *input) {
    memset(input, 0, sizeof(*input));
}

void platform_begin_frame(void) {
}

void platform_draw_line(void *context, int x0, int y0, int x1, int y1, uint8_t color) {
    (void) context;
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
    (void) color;
}

void platform_draw_polygon(void *context, const int16_t *points, int count, uint8_t color) {
    (void) context;
    (void) points;
    (void) count;
    (void) color;
}

void platform_draw_text(void *context, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t scale) {
    (void) context;
    (void) x;
    (void) y;
    (void) text;
    (void) fg;
    (void) bg;
    (void) scale;
}

void platform_draw_points(void *context, const int16_t *points, int count, uint8_t color) {
    (void) context;
    (void) points;
    (void) count;
    (void) color;
}

void platform_clear_field(void *context) {
    (void) context;
}

void platform_end_frame(void) {
}

void platform_mark_dirty(void *context, int x0, int y0, int x1, int y1) {
    (void) context;
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
}

int platform_take_elapsed_frames(void) {
    return 1;
}

int platform_pace_cadence(void) {
    return 1;
}

void platform_set_pace_lock(int locked) {
    (void) locked;
}

void platform_sound_write(uint8_t reg, uint8_t value) {
    (void) reg;
    (void) value;
}

int platform_load_scores(uint8_t *data, int size) {
    (void) data;
    (void) size;
    return 0;
}

int platform_load_settings(uint8_t *data, int size) {
    (void) data;
    (void) size;
    return 0;
}

void platform_save_settings(const uint8_t *data, int size) {
    (void) data;
    (void) size;
}

void platform_save_scores(const uint8_t *data, int size) {
    (void) data;
    (void) size;
}
#endif
