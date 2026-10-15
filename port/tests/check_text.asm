; check_text.asm - runs the generated print_text (CS:04B7) on one string.
; tests/check_main.c sets up the data segment and dumps the resulting CGA
; memory for tools/ref_text.py.

%include "runtime.inc"

extern L_04B7

global check_print_text

section .text

; void check_print_text(uint32_t ds_offset)
check_print_text:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    PUSH_WIN64_NONVOL
    mov r15d, ARG1D
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
    mov si, r15w
    call L_04B7
    POP_WIN64_NONVOL
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
