#!/usr/bin/env python3
"""Stitch CGA memory dumps of rooms (tools/render_room.c) into one world map.

Usage: world2png.py <dir> <out.png>

Reads <dir>/rRR_cCC.bin (hex row/column, 0-1F each) and places each room at
its cell of the 32x32 map grid (DS:0F69), so neighbours on the map are
neighbours in the image, as the player walks between them. Cells without a
dump (no screen, or the render failed) are dark grey.

Only the room area is kept: 32x18 tiles of 8x8 (the tile buffers at DS:0004),
drawn from x = 32, y = 0. The status panel below it is not drawn.

Mode 4 colours as the game sets them (CS:0254-0260): background black,
palette 0, low intensity.
"""
import os
import struct
import sys
import zlib

GRID = 32
ROOM_X, WIDTH, HEIGHT = 32, 256, 144
# Indices 0-3: the CGA palette; 4: empty cell.
PALETTE = [(0x00, 0x00, 0x00), (0x00, 0xAA, 0x00), (0xAA, 0x00, 0x00), (0xAA, 0x55, 0x00),
           (0x20, 0x20, 0x20)]
EMPTY = 4


def decode(vram):
    """The room area as rows of palette indices, from interlaced 2 bpp CGA memory."""
    rows = []
    for y in range(HEIGHT):
        base = (y & 1) * 0x2000 + (y >> 1) * 80 + ROOM_X // 4
        row = bytearray()
        for b in vram[base:base + WIDTH // 4]:
            row += bytes((b >> 6 & 3, b >> 4 & 3, b >> 2 & 3, b & 3))
        rows.append(row)
    return rows


def write_png(path, rows):
    """8-bit indexed PNG."""
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF))
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw += row
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', len(rows[0]), len(rows), 8, 3, 0, 0, 0)))
        f.write(chunk(b'PLTE', b''.join(bytes(c) for c in PALETTE)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))


def main():
    d, out = sys.argv[1], sys.argv[2]
    sheet = [bytearray([EMPTY]) * (GRID * WIDTH) for _ in range(GRID * HEIGHT)]
    done = 0
    for r in range(GRID):
        for c in range(GRID):
            src = os.path.join(d, 'r%02X_c%02X.bin' % (r, c))
            if not os.path.exists(src):
                continue
            with open(src, 'rb') as f:
                rows = decode(f.read())
            x0 = c * WIDTH
            for y in range(HEIGHT):
                sheet[r * HEIGHT + y][x0:x0 + WIDTH] = rows[y]
            done += 1
    write_png(out, sheet)
    print('world2png: %d rooms, %s' % (done, out))


if __name__ == '__main__':
    main()
