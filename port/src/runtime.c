/*
 * runtime.c - hand-written parts of the C translation (web build): CPU
 * state, segment lookup, delivery of INT 1Ch timer ticks, the port's marker
 * code and the entry point. The C counterpart of game.asm and of the NASM
 * markers in asm2nasm.py; the game code itself is generated into sab2_code.c
 * by tools/asm2c.py.
 */
#include "runtime.h"

#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#define DS_VALUE 0x12E6             /* CS + 2E6h, set by the entry code (CS:0100) */
#define CS_VALUE 0x1000
#define ENTRY 0x0100
#define IVT_1C 0x70                 /* INT 1Ch vector: offset, then segment */

uint8_t cf, zf, sf, of, af, df;
uint8_t *ds, *es;
uint16_t seg_ds, seg_es;
uint32_t stack[STACK_ENTRIES];
unsigned sp;
uint32_t pc;
volatile uint8_t game_yield;

/* Segment values the game loads: 12E6h (its data segment), B800h (CGA
 * memory) and 0000h (interrupt vectors; also read as noise by
 * speaker_noise, CS:2DD0). */
uint8_t *seg_to_base(uint16_t seg)
{
    switch (seg) {
    case DS_VALUE: return ds_base;
    case 0xB800: return cga_vram;
    case 0x0000: return ivt_seg;
    default: game_bad_segment(seg);
    }
}

_Noreturn void stack_fault(void)
{
    SDL_Log("game: stack over- or underflow at CS:%04X", (unsigned)pc);
    abort();
}

_Noreturn void divide_fault(uint32_t cs_offset)
{
    SDL_Log("game: divide error at CS:%04X", cs_offset);
    abort();
}

/* ---------------------------------------------------------------------------
 * INT 1Ch delivery at the TIMER_POLL points, as timer_poll in game.asm: the
 * installed handler runs once per tick that is due, with the registers at
 * zero, and the game's registers, flags and segments are restored after the
 * last one. Only vectors pointing into the game (segment CS_VALUE) are
 * called; the BIOS default is a plain IRET. */
uint32_t timer_vector;

static struct {
    struct cpu16 cpu;
    uint16_t flags, seg_ds, seg_es;
} timer_saved;
static uint32_t timer_ticks;
static bool in_timer;

int timer_poll_begin(uint32_t wait)
{
    if (in_timer)
        return 0;
    timer_ticks = timer_due(wait);
    if (timer_ticks == 0)
        return 0;
    timer_saved.cpu = cpu;
    timer_saved.flags = flags_image();
    timer_saved.seg_ds = seg_ds;
    timer_saved.seg_es = seg_es;
    in_timer = true;
    return 1;
}

int timer_poll_next(void)
{
    while (timer_ticks > 0) {
        timer_ticks--;
        if (rd16(ivt_seg, IVT_1C + 2) != CS_VALUE)
            continue;
        timer_vector = rd16(ivt_seg, IVT_1C);
        uint16_t port = cpu.port;
        memset(&cpu, 0, sizeof cpu);
        cpu.port = port;
        df = 0;
        return 1;
    }
    cpu = timer_saved.cpu;
    set_flags_image(timer_saved.flags);
    set_ds(timer_saved.seg_ds);
    set_es(timer_saved.seg_es);
    in_timer = false;
    return 0;
}

/* ---------------------------------------------------------------------------
 * The port's changes (asm2nasm.py MARKERS; the cheats and held keys are set
 * by platform.c). */
#define PRINT_POSITION 0x1BA2       /* print_text's position (word) and colour */
#define PRINT_COLOR 0x1BC4
#define TIMER_DIGITS 0x1BE6         /* 3 ASCII digits, counted down by CS:0FA0 */
#define TIMER_MAX 999
#define CONTROL_KEYS 0x5800         /* read_keyboard's 5 entries (see CS:112C) */
#define CONTROL_KEY_COUNT 5
#define CONTROL_STATE 0x5B35

/* What the NASM markers keep around their calls into the game: registers,
 * ES and print_text's position and colour. */
void port_save(void)
{
    push(AX);
    push(BX);
    push(CX);
    push(DX);
    push(SI);
    push(DI);
    push(BP);
    push(seg_es);
    push(rd16(ds_base, PRINT_POSITION));
    push(ds_base[PRINT_COLOR]);
}

void port_restore(void)
{
    ds_base[PRINT_COLOR] = (uint8_t)pop();
    wr16(ds_base, PRINT_POSITION, pop());
    set_es((uint16_t)pop());
    BP = (uint16_t)pop();
    DI = (uint16_t)pop();
    SI = (uint16_t)pop();
    DX = (uint16_t)pop();
    CX = (uint16_t)pop();
    BX = (uint16_t)pop();
    AX = (uint16_t)pop();
}

/* Time cheat (CS:049D): 100 more per press, at most TIMER_MAX. */
void port_add_time(void)
{
    uint8_t *d = ds_base + TIMER_DIGITS;
    unsigned t = (d[0] - '0') * 100u + (d[1] - '0') * 10u + (d[2] - '0');
    t += take_u8(&cheat_time) * 100u;
    if (t > TIMER_MAX)
        t = TIMER_MAX;
    d[0] = (uint8_t)('0' + t / 100);
    d[1] = (uint8_t)('0' + t / 10 % 10);
    d[2] = (uint8_t)('0' + t % 10);
}

/* Held control keys (CS:112C): a held entry whose key is down gets countdown
 * 4; one whose key is up is released, unless its first press came this frame
 * (countdown 2 with 40h, or 6 for down). */
void port_hold_keys(void)
{
    if (!ctl_hold)
        return;
    uint8_t down = ctl_down;
    for (int i = 0; i < CONTROL_KEY_COUNT; i++, down >>= 1) {
        uint8_t *e = ds + CONTROL_KEYS + 4 * i;
        uint8_t flags = e[2];
        if (!(flags & 0x80))
            continue;
        if (down & 1) {
            if (e[3] < 4)
                e[3] = 4;
        } else if (e[3] != 6 && !(e[3] == 2 && (flags & 0x40))) {
            e[2] &= 0x3F;
            e[3] = 0;
            ds[CONTROL_STATE] &= (uint8_t)~flags;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Entry */

/* Sets up the entry point (CS:0100) as game_main in game.asm does: DS = ES =
 * the data segment, registers zero. A return address that no CALL pushes
 * ends the game if the entry code ever returns. A restart (web build, after
 * the game exited) starts again from the original data. */
void game_start(void)
{
    static uint8_t ds_initial[SEGMENT_SIZE];
    static bool started;
    if (!started) {
        memcpy(ds_initial, ds_base, sizeof ds_initial);
        started = true;
    } else {
        memcpy(ds_base, ds_initial, sizeof ds_initial);
    }
    memset(&cpu, 0, sizeof cpu);
    set_flags_image(0);
    set_ds(DS_VALUE);
    set_es(DS_VALUE);
    dos_exit = 0;
    in_timer = false;
    sp = 0;
    push(UINT32_MAX);
    pc = ENTRY;
}

/* Runs the game to its end on the calling thread (non-web C builds). */
void game_main(void)
{
    game_start();
    while (game_run())
        game_yield = 0;
}
