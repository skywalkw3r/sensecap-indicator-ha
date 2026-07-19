/**
 * Mock sensor + WiFi + Home-Assistant data layer.
 *
 * Injects events via the synchronous esp_event stub so the UI renders
 * without physical hardware. All values oscillate slowly so you can
 * visually verify the UI updates correctly.
 *
 * Event delivery:
 *   VIEW_EVENT_SENSOR_DATA  — 1 Hz, all 4 built-in sensor types
 *   VIEW_EVENT_WIFI_ST      — once at startup (connected)
 *   VIEW_EVENT_HA_SENSOR    — legacy loft temp/humidity/CO2 display values:
 *                             a batch seeded at startup (so the trends chart is
 *                             pre-populated), then 1 Hz thereafter (this is the
 *                             sim stand-in for indicator/display/set).
 *   VIEW_EVENT_HA_ENTITY    — dashboard sensor slots (room temps + loft
 *                             hum/CO2), 1 Hz, WS mode only (ha_ws_stub).
 *                             Toggle/light/media state + command echo live in
 *                             stubs/ha_ws_stub.c (seeded + ticked from here).
 */
#include "mock_sensors.h"
#include "view_data.h"
#include "view_data_types.h"
#include "sensor_model_stub.h"
#include "lv_port.h"
#include "ha_dash.h"
#include "ha_ws.h"
#include "ha_ws_stub.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Oscillation helpers ─────────────────────────────────────────────────── */

/* t in seconds (float), returns value oscillating between lo and hi */
static float osc(float t, float lo, float hi, float period) {
    return lo + (hi - lo) * 0.5f * (1.0f + sinf(2.0f * 3.14159265f * t / period));
}

/* ── State ───────────────────────────────────────────────────────────────── */

static uint32_t s_last_sensor_tick = 0;
static bool     s_wifi_posted      = false;
static bool     s_ha_enabled       = true; /* SIM_NO_HA_MOCK=1 disables the HA feed */
static int      s_ha_sample_n      = 0;   /* virtual seconds, continuous seed→live */

/* Fill most of the 120-point window at startup so the chart reads as a rich
 * trend in a headless screenshot rather than a single fresh point. */
#define HA_SEED_SAMPLES (HA_HISTORY_MAX_SAMPLES - 10)

/* ── WiFi ────────────────────────────────────────────────────────────────── */

static void post_wifi_status(void) {
    struct view_data_wifi_st st = {
        .is_connected = true,
        .is_connecting = false,
        .is_network = true,
        .rssi = -55,
    };
    strncpy(st.ssid, "SimWiFi-5G", sizeof(st.ssid) - 1);

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                      &st, sizeof(st), portMAX_DELAY);
    printf("[mock] wifi: connected (ssid=%s rssi=%d)\n", st.ssid, st.rssi);
}

/* ── Sensors ─────────────────────────────────────────────────────────────── */

static void post_sensor(enum sensor_data_type type, float value) {
    struct view_data_sensor_data d = { .sensor_type = type, .value = value };
    sim_sensor_set_value(type, value);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA,
                      &d, sizeof(d), portMAX_DELAY);
}

static void tick_sensors(void) {
    float t = SDL_GetTicks() / 1000.0f;   /* seconds since init */

    post_sensor(SCD41_SENSOR_CO2,      osc(t, 400.0f,  1200.0f, 30.0f));
    post_sensor(SGP40_SENSOR_TVOC,     osc(t,   0.0f,   300.0f, 45.0f));
    post_sensor(SHT41_SENSOR_TEMP,     osc(t,  18.0f,    32.0f, 60.0f));
    post_sensor(SHT41_SENSOR_HUMIDITY, osc(t,  30.0f,    80.0f, 50.0f));
}

/* ── Home Assistant display values (loft temp / humidity / CO2) ───────────── */

static void post_ha_display(int index, const char *value) {
    struct view_data_ha_sensor_data d = { .index = (uint8_t)index };
    strncpy(d.value, value, sizeof(d.value) - 1);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SENSOR,
                      &d, sizeof(d), portMAX_DELAY);
}

/* Generate one plausible drifting sample per series at virtual time `t` and
 * post them, formatted the way Home Assistant templates would (temp 1 dp,
 * humidity/CO2 integers). In MQTT mode (SIM_MQTT_MODE=1) ha_dash.c's legacy
 * bridge re-posts these onto the Loft dashboard slots — same as on-device. */
static void tick_ha_display(float t) {
    char buf[32];

    snprintf(buf, sizeof(buf), "%.1f", osc(t, 66.0f, 82.0f, 90.0f));   /* °F */
    post_ha_display(0, buf);
    snprintf(buf, sizeof(buf), "%d", (int)(osc(t, 35.0f, 65.0f, 70.0f) + 0.5f)); /* % */
    post_ha_display(1, buf);
    snprintf(buf, sizeof(buf), "%d", (int)(osc(t, 480.0f, 1050.0f, 50.0f) + 0.5f)); /* ppm */
    post_ha_display(2, buf);
}

/* ── Dashboard sensor slots (WS mode) ─────────────────────────────────────── */

static void post_dash_sensor(int slot, const char *value) {
    struct view_data_ha_entity d = { .slot = (uint8_t)slot, .brightness = -1 };
    strncpy(d.state, value, sizeof(d.state) - 1);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ENTITY,
                      &d, sizeof(d), portMAX_DELAY);
}

/* Mirrors what ha_ws.c posts for SENSOR slots when subscribed: per-room
 * temperatures (each room drifts around its own comfort point) plus the loft
 * humidity/CO2 stat rows. */
static void tick_dash_sensors(float t) {
    char buf[32];

    snprintf(buf, sizeof(buf), "%.1f", osc(t, 66.0f, 82.0f, 90.0f));
    post_dash_sensor(SLOT_LOFT_TEMP, buf);
    snprintf(buf, sizeof(buf), "%.1f", osc(t, 73.5f, 78.5f, 110.0f));
    post_dash_sensor(SLOT_GUEST_TEMP, buf);
    snprintf(buf, sizeof(buf), "%.1f", osc(t, 78.0f, 86.0f, 80.0f));
    post_dash_sensor(SLOT_LIVING_TEMP, buf);
    snprintf(buf, sizeof(buf), "%.1f", osc(t, 73.0f, 79.0f, 95.0f));
    post_dash_sensor(SLOT_HALL_TEMP, buf);
    snprintf(buf, sizeof(buf), "%d", (int)(osc(t, 35.0f, 65.0f, 70.0f) + 0.5f));
    post_dash_sensor(SLOT_LOFT_HUM, buf);
    snprintf(buf, sizeof(buf), "%d", (int)(osc(t, 480.0f, 1050.0f, 50.0f) + 0.5f));
    post_dash_sensor(SLOT_LOFT_CO2, buf);
    post_dash_sensor(SLOT_HALL_SERVER, "on"); /* Supermicro binary_sensor */
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void mock_sensors_start(void) {
    printf("[mock] sensors: starting (1 Hz sensor events, wifi at startup)\n");
    /* First tick fires immediately */
    s_last_sensor_tick = 0;
    s_wifi_posted = false;

    /* SIM_NO_HA_MOCK=1 leaves the HA feed silent — handy for previewing the
     * trends empty state and the Loft cards' "--" placeholders. */
    const char *no_ha = getenv("SIM_NO_HA_MOCK");
    s_ha_enabled = !(no_ha && *no_ha);
    if (!s_ha_enabled) {
        printf("[mock] ha: disabled (SIM_NO_HA_MOCK)\n");
        return;
    }

    printf("[mock] ha: seeding %d history samples\n", HA_SEED_SAMPLES);
    for (int i = 0; i < HA_SEED_SAMPLES; i++) {
        tick_ha_display((float)s_ha_sample_n);
        s_ha_sample_n++;
    }

    /* Dashboard: initial toggle/light/media states from the interactive stub,
     * plus a first sensor pass so the room cards render populated. */
    ha_ws_stub_seed();
    if (ha_ws_is_enabled()) {
        tick_dash_sensors((float)s_ha_sample_n);
    }
}

void mock_sensors_tick(void) {
    uint32_t now = SDL_GetTicks();

    if (!s_wifi_posted) {
        post_wifi_status();
        s_wifi_posted = true;
    }

    if (now - s_last_sensor_tick >= 1000) {
        tick_sensors();
        if (s_ha_enabled) {
            tick_ha_display((float)s_ha_sample_n);
            if (ha_ws_is_enabled()) {
                tick_dash_sensors((float)s_ha_sample_n);
            }
            s_ha_sample_n++;
        }
        s_last_sensor_tick = now;
    }

    if (s_ha_enabled) {
        ha_ws_stub_tick(now);
    }
}
