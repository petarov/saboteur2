/*
 * bios.h - emulated PC hardware and BIOS/DOS services for the translated game:
 * memory segments, INT 10h/16h/1Ah/21h, port 61h (speaker), the INT 1Ch timer
 * tick and the 8088 speed pacing. No window code.
 */
#ifndef SAB2_BIOS_H
#define SAB2_BIOS_H

#include <stdbool.h>
#include <stdint.h>

#define SEGMENT_SIZE 0x10000

/* Register image exchanged with runtime.inc (BIOS / PORT_* macros).
 * Field offsets are fixed: CPU_AX = 0 ... CPU_PORT = 14. */
struct cpu16 {
    uint16_t ax, bx, cx, dx, si, di, bp, port;
};

extern struct cpu16 cpu;

/* Emulated segments: B800h (CGA) and 0000h (interrupt vectors). */
extern uint8_t cga_vram[SEGMENT_SIZE];
extern uint8_t ivt_seg[SEGMENT_SIZE];

void bios_init(void);

/* Host monotonic clock in ns (any thread). */
uint64_t host_ns(void);
/* The game's clock: host time, or the paced 8088 time when that runs ahead
 * (see cpu_pace). Speaker changes are stamped with it. Game thread. */
uint64_t game_clock_ns(void);

/* Current video mode (INT 10h/00h): 0/1 = 40x25 text, 4/5 = 320x200. */
uint8_t bios_video_mode(void);
/* CGA color select register as last set by INT 10h/0Bh. */
uint8_t bios_cga_color_select(void);

/* Main thread: queues one key for INT 16h as scancode << 8 | ASCII. Keys
 * beyond the BIOS buffer size (15) are dropped, as on the PC. */
void kbd_push(uint16_t key);

/* Set by INT 21h/4Ch; the DOS macro (runtime.inc) then leaves game_main. */
extern uint8_t dos_exit;

/* Invincibility cheat, toggled by the main thread (Ctrl-I). While set, the
 * damage (CS:1F89, see asm2nasm.py) takes nothing off the energy bar. */
extern volatile uint8_t cheat_invincible;
/* Pay cheat presses (Ctrl-P) not yet applied; the game loop adds $100 for
 * each (see asm2nasm.py, CS:049A). */
extern volatile uint8_t cheat_pay;

/* Called from runtime.inc / game.asm. */
void bios_int10(void);
uint32_t bios_int16(void);   /* returns 1 when ZF is to be set */
void bios_int1a(void);
void bios_int21(void);
uint32_t io_in(void);
void io_out(void);
void cpu_pace(uint32_t ns);
uint32_t timer_due(uint32_t wait);

/* Called from game.asm / runtime.inc; none of them return. */
_Noreturn void game_trap(uint32_t cs_offset);
_Noreturn void game_bad_segment(uint32_t segment);
_Noreturn void game_bad_jump(uint32_t cs_offset);

/* Entry point of the translated game (game.asm); returns after INT 21h/4Ch. */
void game_main(void);

#endif
