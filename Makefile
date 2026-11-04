PROJECT := ASTROIDS
BUILD_DIR := build
SRC_DIR := src
INC_DIR := include
TEST_DIR := tests

CROSS ?= m68k-atari-mint-
CC := $(CROSS)gcc
HOST_CC ?= cc
CFLAGS := -std=c99 -m68000 -O2 -Wall -Wextra -Wno-missing-field-initializers -Werror -fomit-frame-pointer -I$(INC_DIR) -I$(BUILD_DIR) $(EXTRA_CFLAGS)
ASFLAGS := -m68000 -I$(BUILD_DIR)
LDFLAGS := -s
TARGET := $(BUILD_DIR)/$(PROJECT).PRG
DISK_IMAGE := $(BUILD_DIR)/asteroids-stfm.st
TEST_TARGET := $(BUILD_DIR)/game_tests

ATARI_SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c
ATARI_ASMS := $(SRC_DIR)/st_video.S $(SRC_DIR)/st_ikbd.S $(SRC_DIR)/st_rocks.S $(SRC_DIR)/st_sprite.S $(SRC_DIR)/st_step.S
ASM_INC := $(BUILD_DIR)/asm_offsets.inc
ATARI_OBJS := $(ATARI_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(ATARI_ASMS:$(SRC_DIR)/%.S=$(BUILD_DIR)/%.o)
TEST_SRCS := $(TEST_DIR)/game_tests.c $(SRC_DIR)/game.c $(SRC_DIR)/sound.c

.PHONY: all atari-st disk-image test test-atari test-gfx-atari test-keys-atari test-sound-atari test-poly-atari test-rocks-atari test-pace-atari screens-atari enemies-atari micro-atari boss-bench-atari prof-atari bench-atari host-sanity clean

all: atari-st disk-image

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET -MMD -MP -c $< -o $@

# byte offsets of GameState / GameAsteroid fields for the assembly, worked out by the compiler (see src/offsets.c)
$(ASM_INC): $(SRC_DIR)/offsets.c $(INC_DIR)/game.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -S $(SRC_DIR)/offsets.c -o - | sed -n 's/^->\([A-Za-z_0-9]*\) #\{0,1\}\(-\{0,1\}[0-9]*\).*/.set \1, \2/p' > $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S $(ASM_INC) | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# rebuild objects when a header they include changes (a stale object with an old struct layout crashes at startup)
-include $(ATARI_OBJS:.o=.d)

$(TARGET): $(ATARI_OBJS)
	$(CC) $(CFLAGS) $(ATARI_OBJS) $(LDFLAGS) -o $@

atari-st: $(TARGET)

$(DISK_IMAGE): $(TARGET) | $(BUILD_DIR)
	rm -f $@
	truncate -s 737280 $@
	mformat -i $@ -f 720 -v ASTEROIDS ::
	mcopy -i $@ $(TARGET) ::ASTROIDS.PRG

disk-image: $(DISK_IMAGE)

test: $(BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-unused-const-variable -I$(INC_DIR) $(TEST_SRCS) -o $(TEST_TARGET)
	$(TEST_TARGET)

# The same tests as a TOS program (prints results, waits for a key), for running in Hatari.
test-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_SRCS) $(SRC_DIR)/st_step.S $(LDFLAGS) -o $(BUILD_DIR)/GAMETEST.PRG

# Pixel-exact checks of the assembly drawing routines (run in Hatari).
test-gfx-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_gfx_test.c $(SRC_DIR)/st_video.S $(SRC_DIR)/st_text.c $(LDFLAGS) -o $(BUILD_DIR)/GFXTEST.PRG

# Keyboard diagnostic: logs which keys reach the game (run in Hatari, read C:\KEYTEST.LOG).
test-poly-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_poly_test.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/game.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/POLYTEST.PRG

test-rocks-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_rocks_test.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/game.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/ROCKTEST.PRG

test-pace-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_pace_test.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/game.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/PACETEST.PRG

test-keys-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_key_test.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/KEYTEST.PRG

# Sound path on the emulated hardware: plays effects and reads the YM registers back (run in Hatari, read C:\SNDTEST.LOG).
test-sound-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_sound_test.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/SNDTEST.PRG

# Scripted walk through the banner, pause, game over, initials and title screens (run in Hatari, take screenshots).
screens-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_screens.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/SCREENS.PRG

# Line-up of every enemy and the four bosses (run in Hatari, take screenshots).
enemies-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_enemies.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/ENEMIES.PRG

# Frame cost with each boss and a line-up of every enemy on screen (C:\BOSS.LOG).
boss-bench-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_boss_bench.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/BOSSBENCH.PRG

# Timings of the drawing primitives (run in Hatari, results in C:\MICRO.LOG).
micro-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_micro.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/MICRO.PRG

# Sampling profiler (run in Hatari, results in C:\PROF.BIN; see tools/prof_report.py): make prof-atari WAVE=4
WAVE ?= 4
prof-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DPROF_WAVE=$(WAVE) -DATARI_ST_TARGET $(TEST_DIR)/atari_prof.c $(TEST_DIR)/prof_isr.S $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) -o $(BUILD_DIR)/PROF.PRG

# Frame-cost benchmark (run in Hatari): game step + clear + render for a few wave sizes.
bench-atari: $(BUILD_DIR) $(ASM_INC)
	$(CC) $(CFLAGS) -DATARI_ST_TARGET $(TEST_DIR)/atari_bench.c $(SRC_DIR)/game.c $(SRC_DIR)/platform_atari_st.c $(SRC_DIR)/st_text.c $(SRC_DIR)/sound.c $(ATARI_ASMS) $(LDFLAGS) -o $(BUILD_DIR)/GAMEBENCH.PRG

host-sanity: $(BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) -c $(SRC_DIR)/main.c -o $(BUILD_DIR)/main.host.o
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -I$(INC_DIR) -c $(SRC_DIR)/platform_atari_st.c -o $(BUILD_DIR)/platform_atari_st.host.o

clean:
	rm -rf $(BUILD_DIR)
