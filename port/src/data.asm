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
    incbin "../SAB2.COM", 0x2D60, 0x5800    ; DS:0000..57FF
    ; Control keys read by read_keyboard (CS:10A4): key, second key, action,
    ; countdown. The original has A/a up, Z/z down, N/n left, M/m right; the
    ; port defaults to the cursor keys, stored as the game's key definition
    ; menu (CS:27D1) stores an extended key: 00h, then the scan code.
    db 0x00, 0x48, 0x08, 0x00               ; up
    db 0x00, 0x50, 0x04, 0x00               ; down
    db 0x00, 0x4B, 0x02, 0x00               ; left
    db 0x00, 0x4D, 0x01, 0x00               ; right
    incbin "../SAB2.COM", 0x2D60 + 0x5810, 0x7A2A - 0x5810  ; DS:5810..7A29 (fire: space)
    times 0x10000 - ($ - ds_base) db 0      ; pad to a full 64 KB segment
