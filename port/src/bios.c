/*
 * bios.c - emulated PC hardware and BIOS/DOS services (see bios.h).
 */
#include "bios.h"
#include "font8x8.h"
#include "speaker.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#define PIT_HZ 1193182ULL
#define DS_VALUE 0x12E6             /* the game's data segment (game.asm) */
#define TEXT_COLS 40                /* modes 0/1 and 4/5: 40x25 character cells */
#define TEXT_ROWS 25
#define TEXT_ATTR 0x07              /* light grey on black, after a mode set */
#define CGA_ODD_BANK 0x2000
#define CGA_BYTES_PER_ROW 80
#define CGA_GRAPHICS_BYTES 0x4000
#define IVT_BYTES 0x400
#define IVT_1C 0x70
#define KEY_BUFFER 16               /* the BIOS buffer holds 15 keys */
#define RECORD_SIZE 128             /* FCB default record size */

extern uint8_t ds_base[SEGMENT_SIZE];   /* data.asm */

struct cpu16 cpu;
uint8_t cga_vram[SEGMENT_SIZE];
uint8_t ivt_seg[SEGMENT_SIZE];
uint8_t dos_exit;
volatile uint8_t cheat_invincible;
volatile uint8_t cheat_pay;
volatile uint8_t cheat_onehit;
volatile uint8_t ctl_hold = 1;
volatile uint8_t ctl_down;

static uint64_t start_ns;
static volatile uint8_t video_mode = 3;  /* what DOS leaves: 80x25 colour text */
static volatile uint8_t cga_color_select = 0x30;
static uint8_t port61;
static uint8_t cursor_row, cursor_col;
static char save_dir[1024];             /* where the high-score file lives */

uint64_t host_ns(void)
{
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t c = SDL_GetPerformanceCounter();
    return c / freq * 1000000000ULL + c % freq * 1000000000ULL / freq;
}

/* Logs an unsupported request once per key. */
static void log_once(uint32_t key, const char *what)
{
    static uint32_t seen[64];
    static int n;
    for (int i = 0; i < n; i++) {
        if (seen[i] == key)
            return;
    }
    if (n < 64)
        seen[n++] = key;
    SDL_Log("bios: %s (%04X)", what, key & 0xFFFF);
}

void bios_init(void)
{
    start_ns = host_ns();
    font8x8_init();
    /* speaker_noise (CS:2DD0) times its clicks with bytes read from 0000:0000
     * upwards, i.e. the interrupt vectors. Fill them like a PC's: BIOS
     * offsets in segment F000h. INT 1Ch starts as F000:FF53 (an IRET). */
    uint16_t lfsr = 0xACE1;
    for (int v = 0; v < IVT_BYTES / 4; v++) {
        for (int bit = 0; bit < 16; bit++)
            lfsr = (uint16_t)(lfsr >> 1 ^ (-(lfsr & 1) & 0xB400));
        ivt_seg[v * 4] = lfsr & 0xFF;
        ivt_seg[v * 4 + 1] = lfsr >> 8;
        ivt_seg[v * 4 + 2] = 0x00;
        ivt_seg[v * 4 + 3] = 0xF0;
    }
    ivt_seg[IVT_1C] = 0x53;
    ivt_seg[IVT_1C + 1] = 0xFF;
    /* SAB_II.SCR goes to the per-user SDL preferences folder. */
    char *dir = SDL_GetPrefPath("petarov", "saboteur2");
    if (dir != NULL) {
        SDL_strlcpy(save_dir, dir, sizeof save_dir);
        SDL_free(dir);
    } else {
        SDL_Log("bios: no preferences folder (%s); high scores go to the current directory",
                SDL_GetError());
    }
}

uint8_t bios_video_mode(void)
{
    return video_mode;
}

uint8_t bios_cga_color_select(void)
{
    return cga_color_select;
}

/* ---------------------------------------------------------------------------
 * 8088 speed pacing and the game clock
 *
 * The music and noise routines toggle the speaker from software, so their
 * pitch depends on the CPU speed. CPU_PACE points (asm2nasm.py) report the
 * 8088 time of the code they precede; it accumulates on a virtual clock that
 * starts at the host time. The game thread sleeps whenever that clock is more
 * than PACE_AHEAD_NS ahead, so it never spins, and speaker changes are stamped
 * with the virtual clock, so the audio thread replays them at 8088 spacing.
 * After a gap longer than PACE_RESYNC_NS the clock restarts from now. */
#define PACE_AHEAD_NS 3000000ULL
#define PACE_RESYNC_NS 10000000ULL

static uint64_t pace_ns;
static unsigned empty_key_polls;

uint64_t game_clock_ns(void)
{
    uint64_t now = host_ns();
    return pace_ns > now ? pace_ns : now;
}

void cpu_pace(uint32_t ns)
{
    uint64_t now = host_ns();
    if (pace_ns + PACE_RESYNC_NS < now)
        pace_ns = now;
    pace_ns += ns;
    if (pace_ns > now + PACE_AHEAD_NS)
        SDL_Delay((Uint32)((pace_ns - now) / 1000000ULL));
    empty_key_polls = 0;
}

/* ---------------------------------------------------------------------------
 * INT 1Ch timer tick (18.2065 Hz)
 *
 * The game installs its own INT 1Ch handlers (CS:0600 frame counter, CS:2D56
 * music). game.asm calls timer_due at the TIMER_POLL points and runs the
 * installed handler as many times as it returns. With wait set and no tick
 * due it sleeps until the next one. At most MAX_TICKS_PER_POLL are delivered
 * at once; older ones are dropped (e.g. after a blocking key read), so the
 * game does not race through a burst of frames. */
#define MAX_TICKS_PER_POLL 2

static uint64_t ticks_at(uint64_t ns)
{
    return (ns - start_ns) / 1000 * PIT_HZ / 65536 / 1000000;
}

static uint64_t tick_start_ns(uint64_t tick)
{
    return start_ns + (tick * 65536 * 1000000000ULL + PIT_HZ - 1) / PIT_HZ;
}

uint32_t timer_due(uint32_t wait)
{
    static uint64_t delivered;
    uint64_t due = ticks_at(game_clock_ns());
    if (due <= delivered && wait) {
        uint64_t next = tick_start_ns(delivered + 1);
        uint64_t now = host_ns();
        if (next > now)
            SDL_Delay((Uint32)((next - now + 999999ULL) / 1000000ULL));
        due = delivered + 1;
    }
    if (due <= delivered)
        return 0;
    uint64_t n = due - delivered;
    delivered = due;
    empty_key_polls = 0;
    return n > MAX_TICKS_PER_POLL ? MAX_TICKS_PER_POLL : (uint32_t)n;
}

/* ---------------------------------------------------------------------------
 * INT 10h video */

static uint8_t *cga_line(int y)
{
    return cga_vram + (y & 1) * CGA_ODD_BANK + (y >> 1) * CGA_BYTES_PER_ROW;
}

static bool text_mode(void)
{
    return video_mode < 4;
}

/* Draws ch in the cell at row/col, as the BIOS does in mode 4: set pixels get
 * color (bits 0-1), the rest of the cell is cleared; with bit 7 the glyph is
 * XORed onto the screen instead. */
static void draw_char(int row, int col, uint8_t ch, uint8_t color)
{
    const uint8_t *glyph = font8x8_glyph(ch);
    for (int r = 0; r < 8; r++) {
        uint16_t pixels = 0;
        for (int x = 0; x < 8; x++) {
            if (glyph[r] & (0x80 >> x))
                pixels |= (uint16_t)(color & 3) << (14 - 2 * x);
        }
        uint8_t *p = cga_line(row * 8 + r) + col * 2;
        if (color & 0x80) {
            p[0] ^= pixels >> 8;
            p[1] ^= pixels & 0xFF;
        } else {
            p[0] = pixels >> 8;
            p[1] = pixels & 0xFF;
        }
    }
}

/* INT 10h/09h: CX copies of AL with attribute (text) or color (graphics) BL
 * from the cursor on; the cursor does not move. */
static void write_chars(uint8_t ch, uint8_t attr, uint16_t count)
{
    int pos = cursor_row * TEXT_COLS + cursor_col;
    for (; count > 0 && pos < TEXT_COLS * TEXT_ROWS; count--, pos++) {
        if (text_mode()) {
            cga_vram[pos * 2] = ch;
            cga_vram[pos * 2 + 1] = attr;
        } else {
            draw_char(pos / TEXT_COLS, pos % TEXT_COLS, ch, attr);
        }
    }
}

static void set_mode(uint8_t mode)
{
    video_mode = mode;
    if (mode < 4) {
        for (int i = 0; i < TEXT_COLS * TEXT_ROWS; i++) {
            cga_vram[i * 2] = ' ';
            cga_vram[i * 2 + 1] = TEXT_ATTR;
        }
    } else {
        memset(cga_vram, 0, CGA_GRAPHICS_BYTES);
    }
    if (mode != 0 && mode != 1 && mode != 4 && mode != 5)
        log_once(0x1000 | mode, "INT 10h: unsupported video mode");
    cga_color_select = 0x30;
    cursor_row = 0;
    cursor_col = 0;
}

void bios_int10(void)
{
    uint8_t ah = cpu.ax >> 8, al = cpu.ax & 0xFF;
    uint8_t bh = cpu.bx >> 8, bl = cpu.bx & 0xFF;
    switch (ah) {
    case 0x00:  /* set mode */
        set_mode(al & 0x7F);
        break;
    case 0x01:  /* cursor shape: no cursor is drawn */
        break;
    case 0x02:  /* set cursor position */
        if ((cpu.dx >> 8) >= TEXT_ROWS || (cpu.dx & 0xFF) >= TEXT_COLS)
            log_once(0x0200, "INT 10h/02h: cursor position off screen, clamped");
        cursor_row = (cpu.dx >> 8) < TEXT_ROWS ? cpu.dx >> 8 : TEXT_ROWS - 1;
        cursor_col = (cpu.dx & 0xFF) < TEXT_COLS ? cpu.dx & 0xFF : TEXT_COLS - 1;
        break;
    case 0x09:  /* write character and attribute */
        write_chars(al, bl, cpu.cx);
        break;
    case 0x0B:  /* set colour palette */
        if (bh == 0)
            cga_color_select = (cga_color_select & 0xE0) | (bl & 0x1F);
        else
            cga_color_select = (cga_color_select & ~0x20) | ((bl & 1) << 5);
        break;
    case 0x0F:  /* get mode: AL = mode, AH = columns, BH = page */
        cpu.ax = (uint16_t)((video_mode == 2 || video_mode == 3 ? 80 : TEXT_COLS) << 8 | video_mode);
        cpu.bx &= 0x00FF;
        break;
    default:
        log_once(0x1000 | ah, "INT 10h: unsupported function");
        break;
    }
}

/* ---------------------------------------------------------------------------
 * INT 16h keyboard
 *
 * Single producer (main thread, kbd_push), single consumer (game thread). */
static uint16_t key_buffer[KEY_BUFFER];
static atomic_uint key_head, key_tail;

void kbd_push(uint16_t key)
{
    unsigned head = atomic_load_explicit(&key_head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&key_tail, memory_order_acquire);
    if (head - tail >= KEY_BUFFER - 1)
        return;
    key_buffer[head % KEY_BUFFER] = key;
    atomic_store_explicit(&key_head, head + 1, memory_order_release);
}

static bool key_peek(uint16_t *key)
{
    unsigned tail = atomic_load_explicit(&key_tail, memory_order_relaxed);
    if (tail == atomic_load_explicit(&key_head, memory_order_acquire))
        return false;
    *key = key_buffer[tail % KEY_BUFFER];
    return true;
}

static void key_drop(void)
{
    atomic_fetch_add_explicit(&key_tail, 1, memory_order_release);
}

/* The game waits for keys by polling AH=01h in a loop (wait_key, CS:05BB).
 * After IDLE_AFTER_POLLS empty polls with no timer tick and no paced code in
 * between, each further empty poll sleeps 1 ms. The game loop polls once per
 * frame and the music player once per note, so neither is slowed down. */
#define IDLE_AFTER_POLLS 3
#define KEY_WAIT_MS 5

uint32_t bios_int16(void)
{
    uint16_t key;
    switch (cpu.ax >> 8) {
    case 0x00:  /* read key, waiting for one */
        while (!key_peek(&key))
            SDL_Delay(KEY_WAIT_MS);
        key_drop();
        cpu.ax = key;
        return 0;
    case 0x01:  /* key waiting? ZF = 1 if not */
        if (key_peek(&key)) {
            cpu.ax = key;
            empty_key_polls = 0;
            return 0;
        }
        if (++empty_key_polls >= IDLE_AFTER_POLLS)
            SDL_Delay(1);
        return 1;
    default:
        log_once(0x1600 | (cpu.ax >> 8), "INT 16h: unsupported function");
        return 0;
    }
}

/* ---------------------------------------------------------------------------
 * INT 1Ah/00h: ticks since start in CX:DX, AL = 0 (no midnight). The game
 * reads it once, as a random seed. */
void bios_int1a(void)
{
    if (cpu.ax >> 8 != 0) {
        log_once(0x1A00 | (cpu.ax >> 8), "INT 1Ah: unsupported function");
        return;
    }
    uint64_t ticks = ticks_at(host_ns());
    cpu.cx = (uint16_t)(ticks >> 16);
    cpu.dx = (uint16_t)ticks;
    cpu.ax &= 0xFF00;
}

/* ---------------------------------------------------------------------------
 * INT 21h: the DOS 1 FCB file calls the game uses for SAB_II.SCR, AH=09h and
 * AH=4Ch. FCB and DTA addresses are DS offsets (DS is always DS_VALUE). */
#define FCB_NAME 0x01
#define FCB_EXT 0x09
#define FCB_BLOCK 0x0C
#define FCB_RECORD_SIZE 0x0E
#define FCB_FILE_SIZE 0x10
#define FCB_RECORD 0x20

static FILE *fcb_file;
static uint16_t dta = 0x80;

static uint8_t *ds_ptr(uint16_t offset)
{
    return ds_base + offset;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = v >> 8;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* Host path of the FCB's file: "NAME.EXT" in save_dir. */
static void fcb_path(const uint8_t *fcb, char *out, size_t size)
{
    char name[13];
    int n = 0;
    for (int i = 0; i < 8 && fcb[FCB_NAME + i] != ' '; i++)
        name[n++] = (char)fcb[FCB_NAME + i];
    if (fcb[FCB_EXT] != ' ')
        name[n++] = '.';
    for (int i = 0; i < 3 && fcb[FCB_EXT + i] != ' '; i++)
        name[n++] = (char)fcb[FCB_EXT + i];
    name[n] = '\0';
    SDL_snprintf(out, size, "%s%s", save_dir, name);
}

static void fcb_opened(uint8_t *fcb)
{
    long size = 0;
    if (fseek(fcb_file, 0, SEEK_END) == 0)
        size = ftell(fcb_file);
    put16(fcb + FCB_BLOCK, 0);
    put16(fcb + FCB_RECORD_SIZE, RECORD_SIZE);
    put16(fcb + FCB_FILE_SIZE, (uint16_t)size);
    put16(fcb + FCB_FILE_SIZE + 2, (uint16_t)(size >> 16));
}

static long fcb_position(const uint8_t *fcb)
{
    return ((long)get16(fcb + FCB_BLOCK) * 128 + fcb[FCB_RECORD]) * RECORD_SIZE;
}

static void fcb_advance(uint8_t *fcb)
{
    if (++fcb[FCB_RECORD] == 128) {
        fcb[FCB_RECORD] = 0;
        put16(fcb + FCB_BLOCK, get16(fcb + FCB_BLOCK) + 1);
    }
}

/* Returns AL. */
static uint8_t dos_fcb(uint8_t ah, uint8_t *fcb)
{
    char path[1100];
    fcb_path(fcb, path, sizeof path);
    switch (ah) {
    case 0x0F:  /* open */
        if (fcb_file != NULL)
            fclose(fcb_file);
        fcb_file = fopen(path, "r+b");
        if (fcb_file == NULL)
            return 0xFF;
        fcb_opened(fcb);
        return 0;
    case 0x10:  /* close */
        if (fcb_file == NULL)
            return 0xFF;
        fclose(fcb_file);
        fcb_file = NULL;
        return 0;
    case 0x13:  /* delete */
        return remove(path) == 0 ? 0 : 0xFF;
    case 0x14: {    /* sequential read: 0 = ok, 1 = end of file, 3 = partial */
        if (fcb_file == NULL || fseek(fcb_file, fcb_position(fcb), SEEK_SET) != 0)
            return 1;
        uint8_t *buf = ds_ptr(dta);
        size_t n = fread(buf, 1, RECORD_SIZE, fcb_file);
        if (n == 0)
            return 1;
        memset(buf + n, 0, RECORD_SIZE - n);
        fcb_advance(fcb);
        return n < RECORD_SIZE ? 3 : 0;
    }
    case 0x15:  /* sequential write: 0 = ok, 1 = disk full */
        if (fcb_file == NULL || fseek(fcb_file, fcb_position(fcb), SEEK_SET) != 0 ||
            fwrite(ds_ptr(dta), 1, RECORD_SIZE, fcb_file) != RECORD_SIZE)
            return 1;
        fcb_advance(fcb);
        return 0;
    case 0x16:  /* create */
        if (fcb_file != NULL)
            fclose(fcb_file);
        fcb_file = fopen(path, "w+b");
        if (fcb_file == NULL) {
            SDL_Log("dos: cannot create %s", path);
            return 0xFF;
        }
        fcb_opened(fcb);
        return 0;
    default:
        return 0xFF;
    }
}

void bios_int21(void)
{
    uint8_t ah = cpu.ax >> 8;
    switch (ah) {
    case 0x09: {    /* print '$'-terminated string at DS:DX */
        char msg[256];
        int n = 0;
        for (uint16_t p = cpu.dx; ds_base[p] != '$' && n < (int)sizeof msg - 1; p++)
            msg[n++] = (char)ds_base[p];
        msg[n] = '\0';
        SDL_Log("game: %s", msg);
        break;
    }
    case 0x0F: case 0x10: case 0x13: case 0x14: case 0x15: case 0x16:
        cpu.ax = (uint16_t)(ah << 8 | dos_fcb(ah, ds_ptr(cpu.dx)));
        break;
    case 0x1A:  /* set DTA */
        dta = cpu.dx;
        break;
    case 0x4C:  /* exit */
        if (fcb_file != NULL) {
            fclose(fcb_file);
            fcb_file = NULL;
        }
        dos_exit = 1;
        break;
    default:
        log_once(0x2100 | ah, "INT 21h: unsupported function");
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Ports: only 61h (bit 1 = speaker data; the game keeps the PIT gate, bit 0,
 * off and drives the speaker from software). */
uint32_t io_in(void)
{
    if (cpu.port == 0x61)
        return port61;
    log_once(cpu.port, "IN from unsupported port");
    return 0xFF;
}

void io_out(void)
{
    if (cpu.port == 0x61) {
        port61 = cpu.ax & 0xFF;
        speaker_set(port61, 0);
        return;
    }
    log_once(cpu.port, "OUT to unsupported port");
}

_Noreturn void game_trap(uint32_t cs_offset)
{
    SDL_Log("game: untranslated instruction reached at CS:%04X", cs_offset);
    abort();
}

_Noreturn void game_bad_segment(uint32_t segment)
{
    SDL_Log("game: unknown segment value %04X loaded", segment);
    abort();
}

_Noreturn void game_bad_jump(uint32_t cs_offset)
{
    SDL_Log("game: computed jump to CS:%04X, which is not an instruction", cs_offset);
    abort();
}
