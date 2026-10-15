; game.asm - hand-written parts of the SAB2.COM port: runtime state, segment
; lookup, entry/exit, and delivery of INT 1Ch timer ticks to the game's
; handlers. Everything else is generated into build/sab2_code.asm from
; re/SAB2.asm.

%define RUNTIME_DEFINES_STATE
%include "runtime.inc"

extern ivt_seg
extern game_bad_segment
extern timer_due
extern L_0100

global seg_ds
global seg_es
global ccall_ret
global seg_to_base
global game_main
global game_exit
global timer_poll_wait
global timer_poll_run

%define DS_VALUE 0x12E6             ; CS + 2E6h, set by the entry code (CS:0100)
%define IVT_1C 0x70                 ; INT 1Ch vector: offset, then segment

section .bss
seg_ds:      resw 1
seg_es:      resw 1
ccall_ret:   resd 1
exit_rsp:    resq 1
timer_ticks: resd 1                 ; ticks still to deliver in timer_poll
in_timer:    resb 1

section .text

; ---------------------------------------------------------------------------
; seg_to_base - in: r14 = 16-bit segment value; out: r14 = host base.
; Flags preserved. Segment values the game loads: 12E6h (its data segment),
; B800h (CGA memory) and 0000h (interrupt vectors; also read as noise by
; speaker_noise, CS:2DD0).

seg_to_base:
    pushfq
    cmp r14d, DS_VALUE
    je .data
    cmp r14d, 0xb800
    je .vram
    test r14d, r14d
    je .ivt
    mov ARG1D, r14d
    C_STACK
    call game_bad_segment
.data:
    lea r14, [ds_base]
    popfq
    ret
.vram:
    lea r14, [cga_vram]
    popfq
    ret
.ivt:
    lea r14, [ivt_seg]
    popfq
    ret

; ---------------------------------------------------------------------------
; game_main - called from C on the game thread; runs the entry point CS:0100.
; DOS would enter with DS = ES = PSP; the entry code sets both to DS_VALUE
; first, so they start there.

game_main:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    PUSH_WIN64_NONVOL
    mov [exit_rsp], rsp
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    mov word [seg_ds], DS_VALUE
    mov word [seg_es], DS_VALUE
    lea r12, [ds_base]
    mov r13, r12
    call L_0100
    ; The game leaves with INT 21h/4Ch (DOS macro), never with RET.

; game_exit - INT 21h/4Ch: back to the C caller of game_main, from any depth.
game_exit:
    mov rsp, [exit_rsp]
    POP_WIN64_NONVOL
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; ---------------------------------------------------------------------------
; timer_poll_wait / timer_poll_run - TIMER_POLL points (asm2nasm.py MARKERS).
; The original's INT 1Ch handlers ran asynchronously; here the game thread
; runs the installed one at these points, once per tick that is due
; (timer_due in bios.c). _wait sleeps until a tick is due. Only vectors
; pointing into the game (segment CS_VALUE) are called; the BIOS default is a
; plain IRET. All registers and flags are preserved.

timer_poll_wait:
    push r14
    mov r14d, 1
    jmp timer_poll

timer_poll_run:
    push r14
    xor r14d, r14d

timer_poll:
    pushfq
    cmp byte [in_timer], 0
    jne .out
    mov byte [in_timer], 1
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r12
    push r13
    push r15
    movzx eax, word [seg_ds]
    push rax
    movzx eax, word [seg_es]
    push rax

    mov rbp, rsp
    C_STACK
    mov ARG1D, r14d
    call timer_due
    mov rsp, rbp
    mov [timer_ticks], eax

.next:
    cmp dword [timer_ticks], 0
    je .done
    dec dword [timer_ticks]
    movzx eax, word [ivt_seg + IVT_1C + 2]
    cmp eax, CS_VALUE
    jne .next
    movzx eax, word [ivt_seg + IVT_1C]
    cmp eax, CS_LABELS_COUNT
    jae .next
    lea r15, [cs_labels]
    movsxd rax, dword [r15 + rax*4]
    test rax, rax
    jz .next
    add r15, rax
    ; The handler saves the 8086 registers it uses; start them at zero so the
    ; upper-bits-zero invariant holds. Its IRET is a RET.
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    cld
    call r15
    jmp .next

.done:
    pop rax
    mov [seg_es], ax
    pop rax
    mov [seg_ds], ax
    pop r15
    pop r13
    pop r12
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    mov byte [in_timer], 0
.out:
    popfq
    pop r14
    ret
