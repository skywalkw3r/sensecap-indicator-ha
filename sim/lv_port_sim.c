/**
 * LVGL SDL2 port — macOS simulator.
 *
 * Single-thread design:
 *   lv_sdl_window_create()  — calls SDL_Init + sets lv_tick_set_cb(SDL_GetTicks)
 *   lv_timer_handler()      — drives LVGL + polls SDL events (mouse, keyboard, quit)
 *   lv_port_sem_take/give   — no-ops; safe because this is the only thread
 *
 * On SDL_QUIT (window closed), LVGL's SDL driver calls exit(0) directly.
 */
#include "lv_port_sim.h"
#include "lvgl.h"          /* pulls in lv_sdl_window.h, lv_sdl_mouse.h, etc. */
#include "mock/mock_sensors.h"
#include <SDL2/SDL.h>
#include <stdio.h>

#define SIM_WIDTH  480
#define SIM_HEIGHT 800
#define SIM_TICK_MS 5      /* lv_timer_handler period in ms */

/* ── lv_port.h stubs — no-ops in single-thread sim ─────────────────────── */
void lv_port_sem_take(void) {}
void lv_port_sem_give(void) {}
void lv_port_init(void)     { lv_port_sim_init(); }

/* ── Init ────────────────────────────────────────────────────────────────── */
void lv_port_sim_init(void) {
    /* lv_sdl_window_create internally calls SDL_Init(SDL_INIT_VIDEO)
     * and registers lv_tick_set_cb(SDL_GetTicks).
     * No separate SDL_Init call needed. */
    lv_display_t *disp = lv_sdl_window_create(SIM_WIDTH, SIM_HEIGHT);
    if (!disp) {
        fprintf(stderr, "[sim] lv_sdl_window_create failed\n");
        return;
    }
    lv_sdl_window_set_title(disp, "SenseCap Indicator — Simulator");

    /* Wire up input devices */
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_sdl_keyboard_create();

    printf("[sim] window %dx%d created\n", SIM_WIDTH, SIM_HEIGHT);
}

/* ── Main loop ───────────────────────────────────────────────────────────── */
void lv_port_sim_run(void) {
    /* lv_timer_handler() calls lv_sdl_*_read() which polls SDL events.
     * SDL_QUIT is handled inside LVGL's driver (calls exit(0)).
     * lv_timer_handler() returns the time until the next timer fires (ms). */
    while (1) {
        mock_sensors_tick();        /* fire sensor/wifi events on schedule */
        uint32_t delay = lv_timer_handler();
        if (delay > SIM_TICK_MS) delay = SIM_TICK_MS;
        SDL_Delay(delay);
    }
}
