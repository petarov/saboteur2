#!/usr/bin/env python3
"""Independent model of print_text in graphics mode, for `make check`.

Re-implements print_text (CS:04B7) with [1BCCh] bit 0 set, i.e. drawing with
draw_char_cga (CS:054A), directly from the SAB2.COM data: the 8x8 font at
DS:0CB1, the colour masks at DS:1B72 and the 2-bit pixel expansion table at
DS:1972. It also writes the check's input, so the generated code prints the
same strings.

Usage: ref_text.py <SAB2.COM> <out_input.bin> <out_ref.bin>

Input file: u16 count, count u16 DS offsets of the strings to print, then
the test string, which the check copies to DS:TEST_STRING.
"""
import struct
import sys

DS_FILE_OFFSET = 0x2D60
TEST_STRING = 0x7B00                # past the game's data (DS:0000..7A29)
FONT, MASKS, EXPAND = 0x0CB1, 0x1B72, 0x1972
# The port's '.' glyph (src/data.asm); blank in SAB2.COM.
DOT_GLYPH = bytes([0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00])
POSITION, COLOR, FLAGS = 0x1BA2, 0x1BC4, 0x1BC5
# Mission briefing strings printed by start_mission (CS:0299..032B).
GAME_STRINGS = [0x1704, 0x173A, 0x17A9, 0x180B, 0x17CC, 0x17EE, 0x173E, 0x1537]


def test_string():
    s = bytearray([0xFD, 0, 0])                         # position 0,0
    s += bytes(range(0x20, 0x7F))                       # every printable char
    for color in (0x01, 0x02, 0x03, 0x05, 0x0A, 0x17, 0x2C, 0x3F, 0xE6):
        s += bytes([0xFE, color]) + b'SABOTEUR '
    s += bytes([0xFD, 0x1E, 0x15]) + b'WRAP.'           # wraps at column 20h
    return bytes(s + b'\xFF')


def main():
    com = open(sys.argv[1], 'rb').read()
    ds = bytearray(com[DS_FILE_OFFSET:]) + bytearray(0x10000 - (len(com) - DS_FILE_OFFSET))
    ds[FONT + ord('.') * 8:FONT + ord('.') * 8 + 8] = DOT_GLYPH
    text = test_string()
    ds[TEST_STRING:TEST_STRING + len(text)] = text
    ds[POSITION:POSITION + 2] = b'\0\0'
    ds[COLOR] = 3
    ds[FLAGS] = 0
    vram = bytearray(0x10000)

    def w16(off):
        return struct.unpack_from('<H', ds, off)[0]

    def draw_char(ch):
        x, y = ds[POSITION], ds[POSITION + 1]
        di = (y * 0xA0 * 2 + (x * 2 + 8 & 0xFF)) & 0xFFFF
        color = ds[COLOR]
        fg = w16(MASKS + (color & 7) * 2)
        bg = w16(MASKS + ((color >> 2) & 0xE))
        src = FONT + ch * 8
        for _ in range(4):
            for step in (0x2000, -0x2000 + 0x50):
                w = w16(EXPAND + ds[src] * 2)
                src += 1
                struct.pack_into('<H', vram, di, (w & fg) | (~w & bg & 0xFFFF))
                di = (di + step) & 0xFFFF

    def advance():
        x, y = ds[POSITION], ds[POSITION + 1]
        if ds[FLAGS] & 0x20:
            y += 1
        else:
            x += 1
            if x == 0x20:
                x, y = 0, y + 1
        ds[POSITION], ds[POSITION + 1] = x & 0xFF, y & 0xFF

    def print_text(si):
        while True:
            ch = ds[si]
            si += 1
            if ch < 0xFD:
                draw_char(ch)
                advance()
            elif ch == 0xFF:
                return
            elif ch == 0xFE:
                ds[COLOR] = ds[si]
                si += 1
            else:
                ds[POSITION], ds[POSITION + 1] = ds[si], ds[si + 1]
                si += 2

    offsets = [TEST_STRING] + GAME_STRINGS
    for off in offsets:
        print_text(off)

    with open(sys.argv[2], 'wb') as f:
        f.write(struct.pack('<H', len(offsets)))
        f.write(b''.join(struct.pack('<H', o) for o in offsets))
        f.write(text)
    open(sys.argv[3], 'wb').write(vram[:0x4000])


if __name__ == '__main__':
    main()
