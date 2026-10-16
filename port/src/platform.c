/*
 * platform.c - SDL2 front end: window, presenting the emulated CGA screen,
 * events, and the game thread.
 *
 * The translated game runs on its own thread, like the original program on
 * the CPU, while this (main) thread does what the hardware did concurrently:
 * it scans out cga_vram at the display rate (40x25 text or 320x200 graphics)
 * and puts key presses into the BIOS keyboard buffer (kbd_push), which the
 * game reads with INT 16h. The speaker is rendered on SDL's audio thread
 * (speaker.c).
 */
#include "bios.h"
#include "font8x8.h"
#include "speaker.h"

#include <SDL2/SDL.h>

#define CGA_WIDTH 320
#define CGA_HEIGHT 200
#define CGA_BYTES_PER_ROW 80
#define CGA_ODD_BANK 0x2000
#define TEXT_COLS 40
#define TEXT_ROWS 25
#define WINDOW_SCALE 4

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t pixels[CGA_WIDTH * CGA_HEIGHT];
static SDL_atomic_t game_finished;

/* SDL (USB HID) scancode -> XT set 1 scancode, the high byte of an INT 16h
 * key; 0 = no XT equivalent. The keypad gives the same codes as the
 * arrow/navigation block, as on the XT keyboard. */
static const uint8_t xt_scancode[SDL_NUM_SCANCODES] = {
    [SDL_SCANCODE_ESCAPE] = 0x01,
    [SDL_SCANCODE_1] = 0x02, [SDL_SCANCODE_2] = 0x03, [SDL_SCANCODE_3] = 0x04,
    [SDL_SCANCODE_4] = 0x05, [SDL_SCANCODE_5] = 0x06, [SDL_SCANCODE_6] = 0x07,
    [SDL_SCANCODE_7] = 0x08, [SDL_SCANCODE_8] = 0x09, [SDL_SCANCODE_9] = 0x0A,
    [SDL_SCANCODE_0] = 0x0B, [SDL_SCANCODE_MINUS] = 0x0C, [SDL_SCANCODE_EQUALS] = 0x0D,
    [SDL_SCANCODE_BACKSPACE] = 0x0E, [SDL_SCANCODE_TAB] = 0x0F,
    [SDL_SCANCODE_Q] = 0x10, [SDL_SCANCODE_W] = 0x11, [SDL_SCANCODE_E] = 0x12,
    [SDL_SCANCODE_R] = 0x13, [SDL_SCANCODE_T] = 0x14, [SDL_SCANCODE_Y] = 0x15,
    [SDL_SCANCODE_U] = 0x16, [SDL_SCANCODE_I] = 0x17, [SDL_SCANCODE_O] = 0x18,
    [SDL_SCANCODE_P] = 0x19, [SDL_SCANCODE_LEFTBRACKET] = 0x1A, [SDL_SCANCODE_RIGHTBRACKET] = 0x1B,
    [SDL_SCANCODE_RETURN] = 0x1C, [SDL_SCANCODE_KP_ENTER] = 0x1C,
    [SDL_SCANCODE_LCTRL] = 0x1D, [SDL_SCANCODE_RCTRL] = 0x1D,
    [SDL_SCANCODE_A] = 0x1E, [SDL_SCANCODE_S] = 0x1F, [SDL_SCANCODE_D] = 0x20,
    [SDL_SCANCODE_F] = 0x21, [SDL_SCANCODE_G] = 0x22, [SDL_SCANCODE_H] = 0x23,
    [SDL_SCANCODE_J] = 0x24, [SDL_SCANCODE_K] = 0x25, [SDL_SCANCODE_L] = 0x26,
    [SDL_SCANCODE_SEMICOLON] = 0x27, [SDL_SCANCODE_APOSTROPHE] = 0x28, [SDL_SCANCODE_GRAVE] = 0x29,
    [SDL_SCANCODE_LSHIFT] = 0x2A, [SDL_SCANCODE_BACKSLASH] = 0x2B,
    [SDL_SCANCODE_Z] = 0x2C, [SDL_SCANCODE_X] = 0x2D, [SDL_SCANCODE_C] = 0x2E,
    [SDL_SCANCODE_V] = 0x2F, [SDL_SCANCODE_B] = 0x30, [SDL_SCANCODE_N] = 0x31,
    [SDL_SCANCODE_M] = 0x32, [SDL_SCANCODE_COMMA] = 0x33, [SDL_SCANCODE_PERIOD] = 0x34,
    [SDL_SCANCODE_SLASH] = 0x35, [SDL_SCANCODE_RSHIFT] = 0x36, [SDL_SCANCODE_KP_MULTIPLY] = 0x37,
    [SDL_SCANCODE_LALT] = 0x38, [SDL_SCANCODE_RALT] = 0x38, [SDL_SCANCODE_SPACE] = 0x39,
    [SDL_SCANCODE_CAPSLOCK] = 0x3A,
    [SDL_SCANCODE_F1] = 0x3B, [SDL_SCANCODE_F2] = 0x3C, [SDL_SCANCODE_F3] = 0x3D,
    [SDL_SCANCODE_F4] = 0x3E, [SDL_SCANCODE_F5] = 0x3F, [SDL_SCANCODE_F6] = 0x40,
    [SDL_SCANCODE_F7] = 0x41, [SDL_SCANCODE_F8] = 0x42, [SDL_SCANCODE_F9] = 0x43,
    [SDL_SCANCODE_F10] = 0x44,
    [SDL_SCANCODE_HOME] = 0x47, [SDL_SCANCODE_KP_7] = 0x47,
    [SDL_SCANCODE_UP] = 0x48, [SDL_SCANCODE_KP_8] = 0x48,
    [SDL_SCANCODE_PAGEUP] = 0x49, [SDL_SCANCODE_KP_9] = 0x49,
    [SDL_SCANCODE_KP_MINUS] = 0x4A,
    [SDL_SCANCODE_LEFT] = 0x4B, [SDL_SCANCODE_KP_4] = 0x4B,
    [SDL_SCANCODE_KP_5] = 0x4C,
    [SDL_SCANCODE_RIGHT] = 0x4D, [SDL_SCANCODE_KP_6] = 0x4D,
    [SDL_SCANCODE_KP_PLUS] = 0x4E,
    [SDL_SCANCODE_END] = 0x4F, [SDL_SCANCODE_KP_1] = 0x4F,
    [SDL_SCANCODE_DOWN] = 0x50, [SDL_SCANCODE_KP_2] = 0x50,
    [SDL_SCANCODE_PAGEDOWN] = 0x51, [SDL_SCANCODE_KP_3] = 0x51,
    [SDL_SCANCODE_INSERT] = 0x52, [SDL_SCANCODE_KP_0] = 0x52,
    [SDL_SCANCODE_DELETE] = 0x53, [SDL_SCANCODE_KP_PERIOD] = 0x53,
};

/* The 16 RGBI colors of a CGA monitor, as XRGB8888. */
static const uint32_t cga_rgbi[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static void build_palette(uint32_t out[4])
{
    uint8_t sel = bios_cga_color_select();
    int bright = (sel & 0x10) ? 8 : 0;
    out[0] = cga_rgbi[sel & 0x0F];
    if (bios_video_mode() == 5) {   /* the "black and white" mode on a colour monitor */
        out[1] = cga_rgbi[3 + bright];  /* cyan */
        out[2] = cga_rgbi[4 + bright];  /* red */
        out[3] = cga_rgbi[7 + bright];  /* white */
    } else if (sel & 0x20) {
        out[1] = cga_rgbi[3 + bright];  /* cyan */
        out[2] = cga_rgbi[5 + bright];  /* magenta */
        out[3] = cga_rgbi[7 + bright];  /* white */
    } else {
        out[1] = cga_rgbi[2 + bright];  /* green */
        out[2] = cga_rgbi[4 + bright];  /* red */
        out[3] = cga_rgbi[6 + bright];  /* brown/yellow */
    }
}

/* Port settings: one file per setting in the per-user SDL preferences folder
 * (next to SAB_II.SCR), holding '1' or '0'. */
static SDL_RWops *open_setting(const char *name, const char *mode)
{
    char *dir = SDL_GetPrefPath("petarov", "saboteur2");
    if (dir == NULL)
        return NULL;
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s%s", dir, name);
    SDL_free(dir);
    return SDL_RWFromFile(path, mode);
}

static bool load_setting(const char *name, bool default_value)
{
    SDL_RWops *f = open_setting(name, "rb");
    char c = default_value ? '1' : '0';
    if (f != NULL) {
        SDL_RWread(f, &c, 1, 1);
        SDL_RWclose(f);
    }
    return c == '1';
}

static void save_setting(const char *name, bool value)
{
    SDL_RWops *f = open_setting(name, "wb");
    if (f != NULL) {
        char c = value ? '1' : '0';
        SDL_RWwrite(f, &c, 1, 1);
        SDL_RWclose(f);
    }
}

/* Fullscreen uses the desktop resolution (no display mode change); the
 * logical size and integer scaling letterbox the picture as in a window. The
 * game has no mouse, so the cursor is hidden while fullscreen. */
static void set_fullscreen(bool on)
{
    if (SDL_SetWindowFullscreen(window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
        return;
    }
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

/* The toggled state is kept for the next start. */
static void toggle_fullscreen(void)
{
    set_fullscreen((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0);
    save_setting("fullscreen", (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0);
}

/* Alt-Enter and F11 toggle fullscreen (the port's keys, like the quit keys). */
static bool is_fullscreen_key(const SDL_KeyboardEvent *k)
{
    if (k->keysym.scancode == SDL_SCANCODE_F11)
        return true;
    return (k->keysym.mod & KMOD_ALT) &&
           (k->keysym.scancode == SDL_SCANCODE_RETURN || k->keysym.scancode == SDL_SCANCODE_KP_ENTER);
}

static bool platform_init(bool fullscreen)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    window = SDL_CreateWindow("Saboteur II v" SAB2_VERSION, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              CGA_WIDTH * WINDOW_SCALE, CGA_HEIGHT * WINDOW_SCALE, SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    if (fullscreen)
        set_fullscreen(true);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }
    if (SDL_RenderSetLogicalSize(renderer, CGA_WIDTH, CGA_HEIGHT) != 0 ||
        SDL_RenderSetIntegerScale(renderer, SDL_TRUE) != 0) {
        SDL_Log("SDL_RenderSetLogicalSize failed: %s", SDL_GetError());
        return false;
    }
    /* SDL_PIXELFORMAT_RGB888 is SDL2's name for XRGB8888. */
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING,
                                CGA_WIDTH, CGA_HEIGHT);
    if (texture == NULL) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    if (!speaker_init())
        SDL_Log("continuing without sound");
    return true;
}

static void platform_shutdown(void)
{
    speaker_shutdown();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

/* 40x25 text: character/attribute pairs from B800:0000, foreground in bits
 * 0-3, background in bits 4-6 (bit 7, blink, is ignored). */
static void render_text(void)
{
    for (int row = 0; row < TEXT_ROWS; row++) {
        for (int col = 0; col < TEXT_COLS; col++) {
            const uint8_t *cell = cga_vram + (row * TEXT_COLS + col) * 2;
            const uint8_t *g = font8x8_glyph(cell[0]);
            uint32_t fg = cga_rgbi[cell[1] & 0x0F], bg = cga_rgbi[(cell[1] >> 4) & 0x07];
            for (int r = 0; r < 8; r++) {
                uint32_t *out = pixels + (row * 8 + r) * CGA_WIDTH + col * 8;
                for (int c = 0; c < 8; c++)
                    out[c] = (g[r] & (0x80 >> c)) ? fg : bg;
            }
        }
    }
}

/* 320x200, 2 bpp, interlaced. */
static void render_graphics(void)
{
    uint32_t pal[4];
    build_palette(pal);

    for (int y = 0; y < CGA_HEIGHT; y++) {
        const uint8_t *row = cga_vram + (y & 1) * CGA_ODD_BANK + (y >> 1) * CGA_BYTES_PER_ROW;
        uint32_t *out = pixels + y * CGA_WIDTH;
        for (int x = 0; x < CGA_BYTES_PER_ROW; x++) {
            uint8_t b = row[x];
            out[0] = pal[(b >> 6) & 3];
            out[1] = pal[(b >> 4) & 3];
            out[2] = pal[(b >> 2) & 3];
            out[3] = pal[b & 3];
            out += 4;
        }
    }
}

/* Converts cga_vram to RGB and shows it. A frame identical to the last one
 * shown is skipped unless force is set (window events), so a static screen
 * costs almost nothing, e.g. over remote desktop. */
static void present(bool force)
{
    static uint32_t shown[CGA_WIDTH * CGA_HEIGHT];

    if (bios_video_mode() < 4)
        render_text();
    else
        render_graphics();

    if (!force && SDL_memcmp(shown, pixels, sizeof shown) == 0)
        return;
    SDL_memcpy(shown, pixels, sizeof shown);

    SDL_UpdateTexture(texture, NULL, pixels, CGA_WIDTH * sizeof(uint32_t));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/* F10, Ctrl-W and Ctrl-Q quit (the port's keys; the game has no quit key).
 * Quitting this way does not save the high scores; the game writes them when
 * it ends itself. */
static bool is_quit_key(const SDL_KeyboardEvent *k)
{
    if (k->keysym.scancode == SDL_SCANCODE_F10)
        return true;
    return (k->keysym.mod & KMOD_CTRL) && (k->keysym.sym == SDLK_w || k->keysym.sym == SDLK_q);
}

/* The ASCII byte the BIOS would return with the key: letters (Shift/Caps Lock
 * for capitals), digits and symbols as typed without Shift, Enter, Backspace,
 * Tab, Esc, and Ctrl+letter as 01h-1Ah. 0 for keys without one (arrows,
 * function keys); the game then sees only the scancode. */
static uint8_t key_ascii(const SDL_Keysym *k)
{
    SDL_Keycode sym = k->sym;
    if (sym >= SDLK_a && sym <= SDLK_z) {
        if (k->mod & KMOD_CTRL)
            return (uint8_t)(sym - SDLK_a + 1);
        bool upper = ((k->mod & KMOD_SHIFT) != 0) != ((k->mod & KMOD_CAPS) != 0);
        return (uint8_t)(upper ? sym - SDLK_a + 'A' : sym);
    }
    if (k->mod & KMOD_CTRL)
        return 0;
    switch (sym) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0D;
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB: return 0x09;
    case SDLK_ESCAPE: return 0x1B;
    default: return (sym >= 0x20 && sym < 0x7F) ? (uint8_t)sym : 0;
    }
}

static int game_thread(void *unused)
{
    (void)unused;
    game_main();
    SDL_AtomicSet(&game_finished, 1);
    return 0;
}

int main(int argc, char *argv[])
{
    /* Fullscreen by default, or as last toggled; --window starts in a window
     * for this run only. SDL_GetPrefPath needs no SDL_Init. */
    bool fullscreen = load_setting("fullscreen", true);
    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--window") == 0)
            fullscreen = false;
        else
            SDL_Log("ignoring unknown option %s", argv[i]);
    }
    if (!platform_init(fullscreen))
        return 1;
    bios_init();

    SDL_Thread *thread = SDL_CreateThread(game_thread, "game", NULL);
    if (thread == NULL) {
        SDL_Log("SDL_CreateThread failed: %s", SDL_GetError());
        platform_shutdown();
        return 1;
    }
    SDL_DetachThread(thread);

    bool quit = false;
    bool force = true;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 frame = freq / 60;
    Uint64 next_frame = SDL_GetPerformanceCounter();
    while (!quit && !SDL_AtomicGet(&game_finished)) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (e.type == SDL_WINDOWEVENT || e.type == SDL_RENDER_TARGETS_RESET) {
                force = true;
            } else if (e.type == SDL_KEYDOWN) {
                SDL_Scancode sc = e.key.keysym.scancode;
                if (is_quit_key(&e.key)) {
                    quit = true;
                } else if (is_fullscreen_key(&e.key)) {
                    if (e.key.repeat == 0)
                        toggle_fullscreen();
                } else if (xt_scancode[sc] != 0) {
                    /* Auto-repeat is passed on, like the PC's typematic
                     * repeat: the game moves only while keys keep arriving. */
                    kbd_push((uint16_t)(xt_scancode[sc] << 8 | key_ascii(&e.key.keysym)));
                }
            }
        }
        present(force);
        force = false;
        /* At most 60 frames per second: vsync does not always wait (e.g. over
         * xrdp or VNC), and the loop would then spin and redraw nonstop. */
        next_frame += frame;
        Uint64 now = SDL_GetPerformanceCounter();
        if (now < next_frame)
            SDL_Delay((Uint32)((next_frame - now) * 1000 / freq));
        else if (now - next_frame > frame)
            next_frame = now;   /* fell behind (e.g. window dragged): resync */
    }
    /* The game thread may still be running; returning from main ends the
     * process. */
    platform_shutdown();
    return 0;
}
