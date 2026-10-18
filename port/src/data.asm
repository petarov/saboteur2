; data.asm - the original data segment (DS = CS + 2E6h) of SAB2.COM.
;
; Bytes are taken verbatim from the .COM file, so every DS offset in
; re/SAB2.asm addresses the same byte here: [1BC2h] becomes [r12 + 1BC2h].

%define RUNTIME_DEFINES_DS
%include "runtime.inc"

section .data
align 16
global ds_base
ds_base:
    incbin "../SAB2.COM", 0x2D60, 0x0E21    ; DS:0000..0E20
    ; Game font (DS:0CB1, 8 bytes per character): '.' is blank in the original
    ; and no game text uses it. The port draws a dot for the menu version.
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00
    incbin "../SAB2.COM", 0x2D60 + 0x0E29, 0x5800 - 0x0E29  ; DS:0E29..57FF
    ; Control keys read by read_keyboard (CS:10A4): key, second key, action,
    ; countdown. The original has A/a up, Z/z down, N/n left, M/m right; the
    ; port defaults to the cursor keys, stored as the game's key definition
    ; menu (CS:27D1) stores an extended key: 00h, then the scan code.
    db 0x00, 0x48, 0x08, 0x00               ; up
    db 0x00, 0x50, 0x04, 0x00               ; down
    db 0x00, 0x4B, 0x02, 0x00               ; left
    db 0x00, 0x4D, 0x01, 0x00               ; right
    incbin "../SAB2.COM", 0x2D60 + 0x5810, 0x7A2A - 0x5810  ; DS:5810..7A29 (fire: space)

    ; Port additions past the original data, print_text strings (FEh colour,
    ; FDh column row, FFh end). The game font has only 20h-5Ah, so the text is
    ; upper case.
    ; The version at the top left of the menu panel and the menu item for the
    ; cheat keys screen below START GAME (marker at CS:2742). SAB2_VERSION
    ; comes from VER in the Makefile.
    times VERSION_TEXT - ($ - ds_base) db 0
    db 0xFE, 0x0A, 0xFD, 0x00, 0x00, "VER ", SAB2_VERSION
    db 0xFE, 0x08, 0xFD, 0x05, 0x10, "C  SEE CHEATS", 0xFF
    ; The cheat keys screen (platform.c), laid out like HIGH SCORES (CS:2897;
    ; marker at CS:275A). Lines stay within columns 0-20, the part of the
    ; panel the menu clears (CS:2A93); the font has no '+'.
    times CHEATS_TEXT - ($ - ds_base) db 0
    db 0xFE, 0x08, 0xFD, 0x04, 0x01, "CHEATS"
    db 0xFD, 0x00, 0x04, "CTRL I  INVINCIBLE"
    db 0xFD, 0x00, 0x06, "CTRL O  ONE HIT KILLS"
    db 0xFD, 0x00, 0x08, "CTRL P  ADD $100 PAY"
    db 0xFD, 0x00, 0x0A, "CTRL J  ADD 100 TIME"
    db 0xFD, 0x00, 0x0C, "CTRL K  HOLD KEYS", 0xFF
    times 0x10000 - ($ - ds_base) db 0      ; pad to a full 64 KB segment
