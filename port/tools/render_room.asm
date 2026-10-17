; render_room.asm - runs the generated room_setup (CS:0660), which builds the
; current room's tile buffers and draws them into CGA memory (CS:0C0D), then
; the first part of one game_loop frame: CS:0EBC adds the tiles some cells get
; per frame (e.g. lifts, the start screen's ground) and CS:0C0D draws them.
; tools/render_room.c sets up the room and dumps the CGA memory.

%include "runtime.inc"

extern L_0660
extern L_0EBC
extern L_0C0D

global render_room_setup

section .text

; void render_room_setup(void)
render_room_setup:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    PUSH_WIN64_NONVOL
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    mov word [seg_ds], 0x12e6
    mov word [seg_es], 0x12e6
    lea r12, [ds_base]
    mov r13, r12
    call L_0660
    call L_0EBC
    call L_0C0D
    POP_WIN64_NONVOL
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
