/*
 * check_main.c - headless check: run the generated print_text in graphics
 * mode on the strings listed in argv[1] (written by tools/ref_text.py) and
 * write the 16 KB of emulated CGA memory to argv[2].
 */
#include "bios.h"

#include <stdio.h>

#define TEST_STRING 0x7B00          /* tools/ref_text.py */
#define POSITION 0x1BA2
#define COLOR 0x1BC4
#define FLAGS 0x1BC5
#define GRAPHICS_TEXT 0x1BCC        /* bit 0: print_text draws with draw_char_cga */
#define MAX_STRINGS 64

extern uint8_t ds_base[SEGMENT_SIZE];   /* data.asm */
void check_print_text(uint32_t ds_offset);

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.bin> <out.bin>\n", argv[0]);
        return 2;
    }
    FILE *in = fopen(argv[1], "rb");
    if (in == NULL) {
        perror(argv[1]);
        return 1;
    }
    uint8_t hdr[2 + 2 * MAX_STRINGS];
    uint16_t offsets[MAX_STRINGS];
    int n = 0;
    if (fread(hdr, 1, 2, in) == 2)
        n = hdr[0] | hdr[1] << 8;
    if (n < 1 || n > MAX_STRINGS || fread(hdr + 2, 2, (size_t)n, in) != (size_t)n) {
        fprintf(stderr, "%s: bad header\n", argv[1]);
        fclose(in);
        return 1;
    }
    for (int i = 0; i < n; i++)
        offsets[i] = (uint16_t)(hdr[2 + 2 * i] | hdr[3 + 2 * i] << 8);
    fread(ds_base + TEST_STRING, 1, SEGMENT_SIZE - TEST_STRING, in);
    fclose(in);

    bios_init();
    ds_base[POSITION] = 0;
    ds_base[POSITION + 1] = 0;
    ds_base[COLOR] = 3;
    ds_base[FLAGS] = 0;
    ds_base[GRAPHICS_TEXT] = 1;
    for (int i = 0; i < n; i++)
        check_print_text(offsets[i]);

    FILE *f = fopen(argv[2], "wb");
    if (f == NULL) {
        perror(argv[2]);
        return 1;
    }
    if (fwrite(cga_vram, 1, 0x4000, f) != 0x4000) {
        perror(argv[2]);
        fclose(f);
        return 1;
    }
    return fclose(f) == 0 ? 0 : 1;
}
