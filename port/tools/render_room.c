/*
 * render_room.c - headless room renderer: puts the game in the state the
 * mission start leaves (CS:03C8-043F), selects the map cell row/col, runs the
 * generated room_setup and writes the 16 KB of emulated CGA memory to out.bin.
 * tools/world2png.py stitches the dumps into the world map.
 *
 * One process per room: room_setup's per-room handlers (DS:2567) and the door
 * table (DS:1369) change the data, so each room starts from SAB2.COM's data.
 * Only the static tiles are drawn: no player, enemies, items or status panel.
 */
#include "bios.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAP 0x0F69                  /* 32x32 screen types, one byte per cell */
#define START_COL 0x11              /* row 0 is only the start screen (CS:03DE) */
#define GRAPHICS_TEXT 0x1BCC        /* bit 0: print_text draws with draw_char_cga */
#define ROW 0x1BD6
#define COL 0x1BD7
#define MAP_PTR 0x1BBC
#define TIMEOUT_S 5                 /* a script that waits for a key or tick */

extern uint8_t ds_base[SEGMENT_SIZE];   /* data.asm */
void render_room_setup(void);

static void put_word(uint16_t off, uint16_t v)
{
    ds_base[off] = v & 0xFF;
    ds_base[off + 1] = v >> 8;
}

/* The game state the mission start sets before room_setup (CS:03C8-043F). */
static void mission_start_state(void)
{
    put_word(0x1BC0, 0x1A6F);
    ds_base[0x1BCD] = 0x07;
    ds_base[0x1BCE] = 0x1D;
    put_word(0x1BB4, 0x00FD);
    ds_base[0x5FAA] = 0x05;
    for (int a = 0x1BCF; a <= 0x1BD3; a++)
        ds_base[a] = 0;
    ds_base[0x1BD4] = 0x02;
    ds_base[0x1BD5] = 0;
    ds_base[0x5FA8] = 0x02;
    put_word(0x1BB6, 0x136D);
    put_word(0x1BB8, 0x5BC0);
    put_word(0x1BBA, 0x11D7);
    for (int a = 0x1BD8; a <= 0x1BDA; a++)
        ds_base[a] = 0;
    ds_base[0x1BDB] = 0x02;
    for (int a = 0x1BDC; a <= 0x1BE0; a++)
        ds_base[a] = 1;
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <row> <col> <out.bin>   (row/col 0-31)\n", argv[0]);
        return 2;
    }
    long row = strtol(argv[1], NULL, 0), col = strtol(argv[2], NULL, 0);
    if (row < 0 || row > 31 || col < 0 || col > 31) {
        fprintf(stderr, "%s: row and col must be 0-31\n", argv[0]);
        return 2;
    }

    if (row == 0 ? col != START_COL : ds_base[MAP + row * 32 + col] == 0) {
        fprintf(stderr, "%s: no screen at row %ld col %ld\n", argv[0], row, col);
        return 3;
    }

    bios_init();
    mission_start_state();
    ds_base[GRAPHICS_TEXT] = 1;
    ds_base[ROW] = (uint8_t)row;
    ds_base[COL] = (uint8_t)col;
    put_word(MAP_PTR, (uint16_t)(MAP + row * 32 + col));
    memset(cga_vram, 0, 0x4000);

    alarm(TIMEOUT_S);
    render_room_setup();
    alarm(0);

    FILE *f = fopen(argv[3], "wb");
    if (f == NULL) {
        perror(argv[3]);
        return 1;
    }
    if (fwrite(cga_vram, 1, 0x4000, f) != 0x4000) {
        perror(argv[3]);
        fclose(f);
        return 1;
    }
    return fclose(f) == 0 ? 0 : 1;
}
