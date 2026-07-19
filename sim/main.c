/**
 * PC simulator entry point — macOS SDL2.
 *
 * Architecture:
 *  - Single thread: lv_timer_handler() and SDL event loop run here.
 *  - lv_port_sem_take/give are no-ops (see lv_port_sim.c).
 *  - esp_event_post_to dispatches synchronously (see stubs/esp_event_stub.c).
 *  - Mock data layer fires timer-based events to drive the UI (mock/).
 *
 * Step 4 (SDL2 main loop) will fill in the display/input init.
 * Step 5 (mock data) will call mock_sensors_start() / mock_ha_start().
 */
#include "lvgl.h"
#include "lv_port_sim.h"
#include "view_data.h"

/* View init functions — called directly instead of via indicator_view.c */
#include "nav/nav.h"
#include "display/display_view.h"
#include "sensor/sensor_view.h"
#include "settings/settings_view.h"
#include "ha_config.h"
#include "wifi/wifi_view.h"
#include "ha/ha_dash.h"
#include "ha/ha_dash_home_screen.h"
#include "ha/ha_dash_room_screen.h"
#include "ha/ha_trend_screen.h"
#include "ha/ha_history.h"
#include "ha/ha_ws_status_screen.h"

#include "mock/mock_sensors.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("[sim] sensecap-indicator simulator starting\n");

    /* 1. Init LVGL + SDL2 display + input */
    lv_init();
    lv_port_sim_init();   /* creates SDL2 window 480×480, wires mouse input */

    /* 2. Init view layer (hardware-free UI domains) */
    nav_init();
    indicator_display_view_init();
    view_sensor_init();
    indicator_wifi_view_init();
    ha_dash_home_screen_init();
    ha_dash_room_screens_init();
    ha_trend_screen_init();
    settings_view_init();
    ha_config_view_init();   /* broker modal (VIEW_EVENT_SCREEN_START handler) */
    ha_ws_status_screen_init(); /* HA WebSocket status modal (stubbed model) */

    /* HA models: dashboard registry (MQTT-mode bridge) + history ring. Must
     * run before the mock seeds so early samples are captured. */
    ha_dash_init();
    ha_history_init();

    const char *open_settings = getenv("SIM_OPEN_SETTINGS");
    if (open_settings && *open_settings) {
        settings_view_show();
    }

    /* SIM_OPEN_BROKER=1: open the MQTT broker modal through the real event
     * path (ha_config.c's VIEW_EVENT_SCREEN_START handler). Capture with
     * SIM_SCREENSHOT_LAYER=top since the modal lives on lv_layer_top(). */
    const char *open_broker = getenv("SIM_OPEN_BROKER");
    if (open_broker && *open_broker) {
        uint8_t screen = SCREEN_BROKER_MODAL;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                          VIEW_EVENT_SCREEN_START, &screen, sizeof(screen), 0);
    }

    /* SIM_OPEN_HA_WS=1: open the HA WebSocket status modal the same way. */
    const char *open_ha_ws = getenv("SIM_OPEN_HA_WS");
    if (open_ha_ws && *open_ha_ws) {
        uint8_t screen = SCREEN_HA_WS_STATUS;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                          VIEW_EVENT_SCREEN_START, &screen, sizeof(screen), 0);
    }

    /* SIM_OPEN_DISPLAY=1: open the Display settings modal the same way. */
    const char *open_display = getenv("SIM_OPEN_DISPLAY");
    if (open_display && *open_display) {
        uint8_t screen = SCREEN_DISPLAY_MODAL;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                          VIEW_EVENT_SCREEN_START, &screen, sizeof(screen), 0);
    }

    /* SIM_START_TILE=<n>: jump to nav tile n before the screenshot warm-up,
     * so headless captures can preview any page. */
    const char *start_tile = getenv("SIM_START_TILE");
    if (start_tile && *start_tile) {
        nav_go_tile(atoi(start_tile));
    }

    /* 3. Start mock data injection (Step 5) */
    mock_sensors_start();

    printf("[sim] entering main loop\n");

    /* 4. Main loop — single thread, no FreeRTOS */
    lv_port_sim_run();   /* blocks; returns on window close */

    lv_deinit();
    printf("[sim] exiting\n");
    return 0;
}
