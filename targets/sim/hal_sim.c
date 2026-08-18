/*
 * CaDS Zero - host implementation of core/cads_hal.h, backed by SDL2.
 *
 * The point of this file is that everything above the HAL runs unchanged: the
 * same bring-up, the same canvas, the same input service. Where the simulator
 * cannot be honest about the hardware it says so here rather than pretending.
 *
 * WHY THE EVENT PUMP LIVES IN THE HAL
 * -----------------------------------
 * cads_bringup_run() never returns - it self tests, then sits in the explorer's
 * command loop forever - so there is no host event loop to give SDL time. If
 * the pump were in main(), the window would be unresponsive and macOS would
 * paint it grey and offer to kill the process. The pump therefore hangs off the
 * calls the application already makes constantly: the delays, the input reads
 * and the console poll. That is also the honest place for it, because those are
 * exactly the points where firmware on the real board is waiting for the world.
 *
 * WHAT THE SIMULATOR CANNOT SEE
 * -----------------------------
 * The blit is synchronous, so cads_hal_display_busy() is always false and a
 * caller that recycles a buffer before the transfer finished gets away with it
 * here and fails on the board. Timing is host timing: no SPI, no DMA, no
 * 16-clocks-per-pixel shift register chain, so any test that asserts on
 * transfer duration is meaningless in this backend. See README.md.
 */

#include "sim.h"

#include "cads_hal.h"

#include <SDL.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* --- window layout, in unscaled pixels ------------------------------------- */

#define CADS_SIM_MARGIN     12
#define CADS_SIM_PANEL_X    CADS_SIM_MARGIN
#define CADS_SIM_PANEL_Y    CADS_SIM_MARGIN
#define CADS_SIM_SIDE_X     (CADS_SIM_PANEL_X + CADS_DISPLAY_WIDTH + 14)
#define CADS_SIM_SIDE_WIDTH 176
#define CADS_SIM_WINDOW_W   (CADS_SIM_SIDE_X + CADS_SIM_SIDE_WIDTH + CADS_SIM_MARGIN)
#define CADS_SIM_WINDOW_H   (CADS_SIM_PANEL_Y + CADS_DISPLAY_HEIGHT + CADS_SIM_MARGIN)

#define CADS_SIM_CELL_W   16
#define CADS_SIM_CELL_H   14
#define CADS_SIM_CELL_GAP 4
#define CADS_SIM_ROW_STEP (CADS_SIM_CELL_H + 4)

/* One repaint per 16 ms. The pump is called thousands of times a second by the
 * explorer's polling loop; repainting on every one of those would turn the
 * simulated firmware into a benchmark of the host's GPU driver. */
#define CADS_SIM_FRAME_US 16000u

#define CADS_SIM_PIXELS (CADS_DISPLAY_WIDTH * CADS_DISPLAY_HEIGHT)

/* --- state ----------------------------------------------------------------- */

typedef struct {
    uint8_t r, g, b;
} cads_sim_rgb_t;

static const cads_sim_rgb_t cads_sim_color_window = {0x10, 0x14, 0x18};
static const cads_sim_rgb_t cads_sim_color_label = {0xB5, 0xC4, 0xD8};
static const cads_sim_rgb_t cads_sim_color_border = {0x6B, 0x74, 0x80};
static const cads_sim_rgb_t cads_sim_color_off = {0x30, 0x35, 0x40};
static const cads_sim_rgb_t cads_sim_color_out = {0x9C, 0xB3, 0x3B};
static const cads_sim_rgb_t cads_sim_color_in = {0xE0, 0xA0, 0x00};
static const cads_sim_rgb_t cads_sim_color_int = {0x1F, 0x8A, 0x80};
static const cads_sim_rgb_t cads_sim_color_touch = {0xA0, 0x30, 0x70};

static struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* panel;

    /* The panel as the application last left it, in native-endian RGB565.
     * Kept separately from the texture so a screenshot works with the dummy
     * video driver, where there is nothing to read back from. */
    uint16_t framebuffer[CADS_SIM_PIXELS];

    uint8_t backlight;
    uint16_t outputs;
    uint8_t inputs;
    uint8_t interrupts;
    bool leds[3];
    bool user_button;
    cads_touch_state_t touch;

    uint64_t epoch_us;
    uint64_t last_present_us;
    uint64_t last_blit_us;
    bool any_blit;
    bool needs_present;

    cads_sim_options_t options;
    bool video_up;
    bool stdin_patched;
    int stdin_flags;
} cads_sim;

/* --- time ------------------------------------------------------------------ */

static uint64_t cads_sim_monotonic_us(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

uint64_t cads_hal_ticks_us(void) {
    return cads_sim_monotonic_us() - cads_sim.epoch_us;
}

uint32_t cads_hal_ticks_ms(void) {
    return (uint32_t)(cads_hal_ticks_us() / 1000u);
}

/* --- the tiny label font ---------------------------------------------------
 *
 * Three columns by five rows, one bit per pixel, bit 0 at the top. Drawing the
 * side panel's labels with the project's own gui/fonts would make the target
 * layer depend on a module that depends on the target layer, so the simulator
 * carries its own alphabet instead - only the characters the labels need.
 */

typedef struct {
    char character;
    uint8_t column[3];
} cads_sim_glyph_t;

static const cads_sim_glyph_t cads_sim_glyphs[] = {
    {'0', {0x1F, 0x11, 0x1F}}, {'1', {0x12, 0x1F, 0x10}}, {'2', {0x19, 0x15, 0x17}},
    {'3', {0x15, 0x15, 0x1F}}, {'4', {0x07, 0x04, 0x1F}}, {'5', {0x17, 0x15, 0x1D}},
    {'6', {0x1F, 0x15, 0x1D}}, {'7', {0x01, 0x01, 0x1F}}, {'8', {0x1F, 0x15, 0x1F}},
    {'9', {0x17, 0x15, 0x1F}}, {'C', {0x1F, 0x11, 0x11}}, {'D', {0x1F, 0x11, 0x0E}},
    {'E', {0x1F, 0x15, 0x15}}, {'H', {0x1F, 0x04, 0x1F}}, {'I', {0x11, 0x1F, 0x11}},
    {'K', {0x1F, 0x04, 0x1B}}, {'L', {0x1F, 0x10, 0x10}}, {'N', {0x1F, 0x02, 0x1F}},
    {'O', {0x1F, 0x11, 0x1F}}, {'R', {0x1F, 0x05, 0x1A}}, {'S', {0x17, 0x15, 0x1D}},
    {'T', {0x01, 0x1F, 0x01}}, {'U', {0x1F, 0x10, 0x1F}}, {'Y', {0x07, 0x1C, 0x07}},
    {'-', {0x04, 0x04, 0x04}}, {'.', {0x00, 0x10, 0x00}}, {' ', {0x00, 0x00, 0x00}},
};

#define CADS_SIM_GLYPH_COUNT (sizeof(cads_sim_glyphs) / sizeof(cads_sim_glyphs[0]))
#define CADS_SIM_TEXT_PIXEL  2 /* the glyphs are 3x5; anything smaller is a smudge */
#define CADS_SIM_TEXT_STEP   (4 * CADS_SIM_TEXT_PIXEL)
#define CADS_SIM_TEXT_HEIGHT (5 * CADS_SIM_TEXT_PIXEL)

static void cads_sim_set_color(cads_sim_rgb_t color) {
    SDL_SetRenderDrawColor(cads_sim.renderer, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
}

static void cads_sim_fill(int x, int y, int width, int height, cads_sim_rgb_t color) {
    SDL_Rect rect = {x, y, width, height};
    cads_sim_set_color(color);
    SDL_RenderFillRect(cads_sim.renderer, &rect);
}

static void cads_sim_text(int x, int y, const char* text, cads_sim_rgb_t color) {
    cads_sim_set_color(color);

    for(const char* p = text; *p; p++) {
        const cads_sim_glyph_t* glyph = NULL;
        for(size_t i = 0; i < CADS_SIM_GLYPH_COUNT; i++) {
            if(cads_sim_glyphs[i].character == *p) {
                glyph = &cads_sim_glyphs[i];
                break;
            }
        }
        if(glyph) {
            for(int column = 0; column < 3; column++) {
                for(int row = 0; row < 5; row++) {
                    if(!(glyph->column[column] & (1u << row))) continue;
                    SDL_Rect dot = {
                        x + column * CADS_SIM_TEXT_PIXEL,
                        y + row * CADS_SIM_TEXT_PIXEL,
                        CADS_SIM_TEXT_PIXEL,
                        CADS_SIM_TEXT_PIXEL};
                    SDL_RenderFillRect(cads_sim.renderer, &dot);
                }
            }
        }
        x += CADS_SIM_TEXT_STEP;
    }
}

/* --- the I/O panel ---------------------------------------------------------
 *
 * cads_hal_adapter_outputs() drives LEDs on the real adapter board, so it is
 * write-only from the firmware's point of view: without these indicators the
 * only way to know what an application put on OUT0..15 would be a debugger.
 */

static int cads_sim_draw_cells(
    int y,
    const char* label,
    uint32_t bits,
    uint32_t count,
    cads_sim_rgb_t on_color) {
    cads_sim_text(CADS_SIM_SIDE_X, y, label, cads_sim_color_label);
    y += CADS_SIM_TEXT_HEIGHT + 5;

    for(uint32_t i = 0; i < count; i++) {
        int x = CADS_SIM_SIDE_X + (int)i * (CADS_SIM_CELL_W + CADS_SIM_CELL_GAP);
        bool on = (bits & (1u << i)) != 0u;
        cads_sim_fill(
            x, y, CADS_SIM_CELL_W, CADS_SIM_CELL_H, on ? on_color : cads_sim_color_off);
    }
    return y + CADS_SIM_ROW_STEP;
}

static void cads_sim_draw_leds(int y) {
    static const cads_sim_rgb_t led_colors[3] = {
        {0x40, 0xD0, 0x50}, /* CadsLedGreen */
        {0x40, 0x90, 0xF0}, /* CadsLedBlue  */
        {0xF0, 0x40, 0x30}, /* CadsLedRed   */
    };

    cads_sim_text(CADS_SIM_SIDE_X, y, "LED-USER", cads_sim_color_label);
    y += CADS_SIM_TEXT_HEIGHT + 5;

    for(int i = 0; i < 3; i++) {
        int x = CADS_SIM_SIDE_X + i * (CADS_SIM_CELL_W + CADS_SIM_CELL_GAP);
        cads_sim_fill(
            x,
            y,
            CADS_SIM_CELL_W,
            CADS_SIM_CELL_H,
            cads_sim.leds[i] ? led_colors[i] : cads_sim_color_off);
    }

    int user_x = CADS_SIM_SIDE_X + 4 * (CADS_SIM_CELL_W + CADS_SIM_CELL_GAP);
    cads_sim_fill(
        user_x,
        y,
        CADS_SIM_CELL_W,
        CADS_SIM_CELL_H,
        cads_sim.user_button ? cads_sim_color_in : cads_sim_color_off);
}

static void cads_sim_draw_touch(int y) {
    char readout[16];

    cads_sim_text(CADS_SIM_SIDE_X, y, "TOUCH", cads_sim_color_label);
    y += CADS_SIM_TEXT_HEIGHT + 5;

    if(cads_sim.touch.pressed) {
        snprintf(
            readout,
            sizeof(readout),
            "%u.%u",
            (unsigned)cads_sim.touch.x,
            (unsigned)cads_sim.touch.y);
        cads_sim_text(CADS_SIM_SIDE_X, y, readout, cads_sim_color_touch);
    } else {
        cads_sim_text(CADS_SIM_SIDE_X, y, "-", cads_sim_color_off);
    }
}

static void cads_sim_render(void) {
    cads_sim_set_color(cads_sim_color_window);
    SDL_RenderClear(cads_sim.renderer);

    SDL_UpdateTexture(
        cads_sim.panel, NULL, cads_sim.framebuffer, CADS_DISPLAY_WIDTH * (int)sizeof(uint16_t));

    /* The backlight is the one analogue thing on the panel, and an application
     * that forgets to turn it on should see the same black screen it would get
     * on the board rather than a helpfully bright window. */
    uint8_t level = (uint8_t)((255u * cads_sim.backlight) / 100u);
    SDL_SetTextureColorMod(cads_sim.panel, level, level, level);

    SDL_Rect frame = {
        CADS_SIM_PANEL_X - 1,
        CADS_SIM_PANEL_Y - 1,
        CADS_DISPLAY_WIDTH + 2,
        CADS_DISPLAY_HEIGHT + 2};
    cads_sim_set_color(cads_sim_color_border);
    SDL_RenderDrawRect(cads_sim.renderer, &frame);

    SDL_Rect panel = {
        CADS_SIM_PANEL_X, CADS_SIM_PANEL_Y, CADS_DISPLAY_WIDTH, CADS_DISPLAY_HEIGHT};
    SDL_RenderCopy(cads_sim.renderer, cads_sim.panel, NULL, &panel);

    int y = CADS_SIM_PANEL_Y;
    y = cads_sim_draw_cells(y, "OUT 0-7", cads_sim.outputs & 0xFFu, 8u, cads_sim_color_out);
    y = cads_sim_draw_cells(
        y, "OUT 8-15", (uint32_t)(cads_sim.outputs >> 8), 8u, cads_sim_color_out);
    y = cads_sim_draw_cells(y, "IN S0-S7", cads_sim.inputs, 8u, cads_sim_color_in);
    y = cads_sim_draw_cells(y, "INT 0-5", cads_sim.interrupts, 6u, cads_sim_color_int);
    cads_sim_draw_leds(y);
    cads_sim_draw_touch(y + 2 * CADS_SIM_ROW_STEP);

    SDL_RenderPresent(cads_sim.renderer);
}

/* --- screenshot ------------------------------------------------------------ */

static void cads_sim_shutdown(void) {
    if(cads_sim.stdin_patched) {
        /* A non-blocking stdin left behind outlives this process and makes the
         * next command in the same shell read EAGAIN. */
        (void)fcntl(STDIN_FILENO, F_SETFL, cads_sim.stdin_flags);
        cads_sim.stdin_patched = false;
    }
    if(cads_sim.video_up) {
        SDL_Quit();
        cads_sim.video_up = false;
    }
    fflush(stdout);
}

static void cads_sim_write_screenshot(void) {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        cads_sim.framebuffer,
        CADS_DISPLAY_WIDTH,
        CADS_DISPLAY_HEIGHT,
        16,
        CADS_DISPLAY_WIDTH * (int)sizeof(uint16_t),
        SDL_PIXELFORMAT_RGB565);

    if(!surface) {
        fprintf(stderr, "sim: cannot wrap the framebuffer: %s\n", SDL_GetError());
        cads_sim_shutdown();
        exit(2);
    }

    int result = SDL_SaveBMP(surface, cads_sim.options.screenshot_path);
    SDL_FreeSurface(surface);

    if(result != 0) {
        fprintf(
            stderr,
            "sim: cannot write %s: %s\n",
            cads_sim.options.screenshot_path,
            SDL_GetError());
        cads_sim_shutdown();
        exit(2);
    }

    fprintf(stderr, "sim: wrote %s\n", cads_sim.options.screenshot_path);
    cads_sim_shutdown();
    exit(0);
}

/*
 * Shoot the first frame the application stops drawing on.
 *
 * A fixed delay would capture whatever happened to be half drawn at that
 * instant; quiescence is a property of the application instead, so the same
 * build always yields the same image no matter how fast the host is.
 */
static void cads_sim_check_screenshot(void) {
    if(!cads_sim.options.screenshot_path) return;

    uint64_t now = cads_hal_ticks_us();

    if(cads_sim.any_blit &&
       (now - cads_sim.last_blit_us) >= (uint64_t)cads_sim.options.screenshot_idle_ms * 1000u) {
        cads_sim_render();
        cads_sim_write_screenshot();
    }

    if(now >= (uint64_t)cads_sim.options.screenshot_timeout_ms * 1000u) {
        fprintf(
            stderr,
            "sim: nothing reached the panel within %u ms, no screenshot written\n",
            cads_sim.options.screenshot_timeout_ms);
        cads_sim_shutdown();
        exit(2);
    }
}

/* --- event pump ------------------------------------------------------------ */

static void cads_sim_quit(void) {
    cads_sim_shutdown();
    exit(0);
}

static void cads_sim_touch_from_mouse(int window_x, int window_y, bool pressed) {
    int scale = (int)cads_sim.options.scale;
    int x = window_x / scale - CADS_SIM_PANEL_X;
    int y = window_y / scale - CADS_SIM_PANEL_Y;

    if(!pressed) {
        cads_sim.touch.pressed = false;
        cads_sim.touch.pressure = 0u;
        return;
    }

    /* A finger that slides off the glass keeps reporting the edge until it is
     * lifted, which is what the resistive panel does too. */
    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(x >= CADS_DISPLAY_WIDTH) x = CADS_DISPLAY_WIDTH - 1;
    if(y >= CADS_DISPLAY_HEIGHT) y = CADS_DISPLAY_HEIGHT - 1;

    cads_sim.touch.x = (uint16_t)x;
    cads_sim.touch.y = (uint16_t)y;
    cads_sim.touch.pressure = 400u; /* mid scale for the XPT2046's raw range */
    cads_sim.touch.pressed = true;
}

static bool cads_sim_inside_panel(int window_x, int window_y) {
    int scale = (int)cads_sim.options.scale;
    int x = window_x / scale;
    int y = window_y / scale;
    return x >= CADS_SIM_PANEL_X && x < CADS_SIM_PANEL_X + CADS_DISPLAY_WIDTH &&
           y >= CADS_SIM_PANEL_Y && y < CADS_SIM_PANEL_Y + CADS_DISPLAY_HEIGHT;
}

static void cads_sim_key(SDL_Scancode code, bool down) {
    /* Keys 1..8 are the adapter's S0..S7. The wire is active low and the HAL
     * inverts it, so a set bit means pressed on both backends. */
    if(code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_8) {
        uint8_t mask = (uint8_t)(1u << (code - SDL_SCANCODE_1));
        cads_sim.inputs = down ? (uint8_t)(cads_sim.inputs | mask) :
                                 (uint8_t)(cads_sim.inputs & ~mask);
        cads_sim.needs_present = true;
        return;
    }

    if(code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F6) {
        uint8_t mask = (uint8_t)(1u << (code - SDL_SCANCODE_F1));
        cads_sim.interrupts = down ? (uint8_t)(cads_sim.interrupts | mask) :
                                     (uint8_t)(cads_sim.interrupts & ~mask);
        cads_sim.needs_present = true;
        return;
    }

    if(code == SDL_SCANCODE_SPACE) {
        cads_sim.user_button = down;
        cads_sim.needs_present = true;
        return;
    }

    if(down && (code == SDL_SCANCODE_ESCAPE || code == SDL_SCANCODE_Q)) {
        cads_sim_quit();
    }
}

void cads_sim_pump(void) {
    SDL_Event event;

    while(SDL_PollEvent(&event)) {
        switch(event.type) {
        case SDL_QUIT:
            cads_sim_quit();
            break;
        case SDL_KEYDOWN:
            if(!event.key.repeat) cads_sim_key(event.key.keysym.scancode, true);
            break;
        case SDL_KEYUP:
            cads_sim_key(event.key.keysym.scancode, false);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if(event.button.button == SDL_BUTTON_LEFT &&
               cads_sim_inside_panel(event.button.x, event.button.y)) {
                cads_sim_touch_from_mouse(event.button.x, event.button.y, true);
                cads_sim.needs_present = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if(event.button.button == SDL_BUTTON_LEFT) {
                cads_sim_touch_from_mouse(event.button.x, event.button.y, false);
                cads_sim.needs_present = true;
            }
            break;
        case SDL_MOUSEMOTION:
            if((event.motion.state & SDL_BUTTON_LMASK) && cads_sim.touch.pressed) {
                cads_sim_touch_from_mouse(event.motion.x, event.motion.y, true);
                cads_sim.needs_present = true;
            }
            break;
        default:
            break;
        }
    }

    uint64_t now = cads_hal_ticks_us();
    if(cads_sim.needs_present && (now - cads_sim.last_present_us) >= CADS_SIM_FRAME_US) {
        cads_sim.last_present_us = now;
        cads_sim.needs_present = false;
        cads_sim_render();
    }

    cads_sim_check_screenshot();
}

/* --- delays ---------------------------------------------------------------- */

void cads_hal_delay_us(uint32_t us) {
    uint64_t deadline = cads_hal_ticks_us() + us;

    for(;;) {
        cads_sim_pump();

        uint64_t now = cads_hal_ticks_us();
        if(now >= deadline) return;

        /* Sleep in short slices so a one-second delay does not make the window
         * ignore a close request for a second. */
        uint64_t remaining = deadline - now;
        if(remaining > 2000u) remaining = 2000u;

        struct timespec slice = {
            .tv_sec = 0, .tv_nsec = (long)(remaining * 1000u)};
        nanosleep(&slice, NULL);
    }
}

void cads_hal_delay_ms(uint32_t ms) {
    cads_hal_delay_us(ms * 1000u);
}

/* --- console --------------------------------------------------------------- */

void cads_hal_console_init(uint32_t baud) {
    /* There is no wire to set a baud rate on; the argument is accepted so the
     * application's init sequence is identical on both backends. */
    (void)baud;

    if(!cads_sim.stdin_patched) {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if(flags != -1 && fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) != -1) {
            cads_sim.stdin_flags = flags;
            cads_sim.stdin_patched = true;
        }
    }
}

void cads_hal_console_write(const void* data, size_t length) {
    fwrite(data, 1u, length, stdout);
    /* Unbuffered in effect: board_test.py reads this stream live and a TAP line
     * still sitting in a buffer looks exactly like a hung firmware. */
    fflush(stdout);
}

bool cads_hal_console_read(uint8_t* byte) {
    ssize_t got = read(STDIN_FILENO, byte, 1u);
    return got == 1;
}

uint32_t cads_hal_console_dropped(void) {
    return 0u; /* the host's tty buffers for us; there is no ring to overflow */
}

uint32_t cads_hal_console_overruns(void) {
    return 0u;
}

/* --- display --------------------------------------------------------------- */

void cads_hal_display_init(void) {
    if(cads_sim.video_up) return;

    if(cads_sim.options.screenshot_path && !SDL_getenv("SDL_VIDEODRIVER")) {
        /* Screenshot runs are for CI, where opening a window is at best rude
         * and at worst impossible. */
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    }

    if(SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "sim: SDL_Init failed: %s\n", SDL_GetError());
        exit(2);
    }
    cads_sim.video_up = true;

    int scale = (int)cads_sim.options.scale;
    cads_sim.window = SDL_CreateWindow(
        "CaDS Zero simulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        CADS_SIM_WINDOW_W * scale,
        CADS_SIM_WINDOW_H * scale,
        SDL_WINDOW_SHOWN);
    if(!cads_sim.window) cads_hal_panic("SDL_CreateWindow failed");

    /* No SDL_RENDERER_PRESENTVSYNC: presenting happens inside the firmware's
     * delay loops, and blocking there for the next vertical blank would show up
     * as the simulated clock running slow. */
    cads_sim.renderer = SDL_CreateRenderer(cads_sim.window, -1, 0);
    if(!cads_sim.renderer) cads_hal_panic("SDL_CreateRenderer failed");
    SDL_RenderSetScale(cads_sim.renderer, (float)scale, (float)scale);

    cads_sim.panel = SDL_CreateTexture(
        cads_sim.renderer,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        CADS_DISPLAY_WIDTH,
        CADS_DISPLAY_HEIGHT);
    if(!cads_sim.panel) cads_hal_panic("SDL_CreateTexture failed");

    /* The board powers up with the backlight on; an application that never
     * calls cads_hal_display_backlight() must not see a black window. */
    cads_sim.backlight = 100u;
    cads_sim.needs_present = true;
    cads_sim_render();
}

void cads_hal_display_backlight(uint8_t percent) {
    cads_sim.backlight = percent > 100u ? 100u : percent;
    cads_sim.needs_present = true;
}

void cads_hal_display_set_fast_clock(bool fast) {
    /* No bus, no divider. Anything that measures the difference is measuring
     * the host, which is why the bring-up's throughput ratio check cannot pass
     * here - see README.md. */
    (void)fast;
}

void cads_hal_display_blit(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint16_t* pixels) {
    if((uint32_t)x + width > CADS_DISPLAY_WIDTH || (uint32_t)y + height > CADS_DISPLAY_HEIGHT) {
        /* On the board this would scribble outside the addressed window and be
         * invisible until someone noticed a smeared panel. Here it is a fault,
         * which is half the reason for having a simulator. */
        cads_hal_panic("display blit outside the panel");
    }

    /* The palette in gui/canvas.c is pre-swapped to big-endian because the SPI
     * DMA emits bytes in memory order and the ILI9486 wants the high byte
     * first. Read the source as bytes rather than halfwords so the unswapping
     * is a property of the format, not of this host's endianness. */
    const uint8_t* source = (const uint8_t*)pixels;

    for(uint16_t row = 0; row < height; row++) {
        uint16_t* destination = &cads_sim.framebuffer[(uint32_t)(y + row) * CADS_DISPLAY_WIDTH + x];
        for(uint16_t column = 0; column < width; column++) {
            destination[column] = (uint16_t)((source[0] << 8) | source[1]);
            source += 2;
        }
    }

    cads_sim.last_blit_us = cads_hal_ticks_us();
    cads_sim.any_blit = true;
    cads_sim.needs_present = true;
    cads_sim_pump();
}

bool cads_hal_display_busy(void) {
    return false; /* the copy above already finished */
}

void cads_hal_display_wait(void) {
    cads_sim_pump();
}

/* --- touch ----------------------------------------------------------------- */

void cads_hal_touch_read(cads_touch_state_t* state) {
    cads_sim_pump();
    *state = cads_sim.touch;
}

/* --- adapter I/O and indicators -------------------------------------------- */

uint8_t cads_hal_adapter_inputs(void) {
    cads_sim_pump();
    return cads_sim.inputs;
}

uint8_t cads_hal_adapter_interrupts(void) {
    cads_sim_pump();
    return cads_sim.interrupts;
}

void cads_hal_adapter_outputs(uint16_t value) {
    cads_sim.outputs = value;
    cads_sim.needs_present = true;
}

void cads_hal_led_set(cads_led_t led, bool on) {
    if((uint32_t)led > (uint32_t)CadsLedRed) return;
    cads_sim.leds[led] = on;
    cads_sim.needs_present = true;
}

void cads_hal_led_toggle(cads_led_t led) {
    if((uint32_t)led > (uint32_t)CadsLedRed) return;
    cads_hal_led_set(led, !cads_sim.leds[led]);
}

bool cads_hal_user_button(void) {
    cads_sim_pump();
    return cads_sim.user_button;
}

/* --- raw port inspection ---------------------------------------------------
 *
 * The simulator has no GPIO. What it does have is the four ports the adapter
 * uses, so the hardware explorer's port dump and its "watch for changes" mode
 * do something meaningful: pressing 1..8 shows up on PF exactly as pressing S0
 * .. S7 does on the board, active low and all.
 */

static const char cads_sim_port_names[] = "DEFG";
#define CADS_SIM_PORT_COUNT (sizeof(cads_sim_port_names) - 1u)

uint32_t cads_hal_port_count(void) {
    return CADS_SIM_PORT_COUNT;
}

char cads_hal_port_name(uint32_t index) {
    return index < CADS_SIM_PORT_COUNT ? cads_sim_port_names[index] : '?';
}

uint16_t cads_hal_port_read(uint32_t index) {
    cads_sim_pump();

    switch(cads_hal_port_name(index)) {
    case 'D': return (uint16_t)(cads_sim.outputs & 0xFFu);
    case 'E': return (uint16_t)(cads_sim.outputs >> 8);
    /* Pulled up, driven low when pressed - the state an IDR would report. */
    case 'F': return (uint16_t)~(uint16_t)cads_sim.inputs;
    case 'G': return (uint16_t)~(uint16_t)cads_sim.interrupts;
    default: return 0u;
    }
}

bool cads_hal_pin_is_reserved(uint32_t port_index, uint32_t pin) {
    /* Nothing here to protect: there is no SWD to lose, no HSE input to fight
     * and no RMII pair to disturb. Saying "reserved" about a pin that does not
     * exist would teach the reader something false about the board. */
    (void)port_index;
    (void)pin;
    return false;
}

/* --- lifecycle and panic ---------------------------------------------------- */

void cads_sim_configure(const cads_sim_options_t* options) {
    cads_sim.options = *options;
    if(cads_sim.options.scale < 1u) cads_sim.options.scale = 1u;
    if(cads_sim.options.scale > 4u) cads_sim.options.scale = 4u;
}

void cads_hal_early_init(void) {
    /* The hardware uses this for clocks and flash latency before the C runtime
     * is usable. On the host the runtime is already up by the time main() runs,
     * so the only job left is making the clock read zero at boot. */
    cads_sim.epoch_us = cads_sim_monotonic_us();
}

void cads_hal_init(void) {
    if(cads_sim.epoch_us == 0u) cads_hal_early_init();
    if(cads_sim.options.scale == 0u) {
        cads_sim_options_t defaults = {NULL, 250u, 10000u, 1u};
        cads_sim_configure(&defaults);
    }

    cads_hal_console_init(115200u);
    cads_hal_display_init();
}

__attribute__((noreturn)) void cads_hal_panic(const char* reason) {
    fflush(stdout);
    fprintf(stderr, "\n*** CaDS PANIC: %s ***\n", reason ? reason : "(no reason given)");
    fflush(stderr);
    cads_sim_shutdown();
    exit(1);
}
