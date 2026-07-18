#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Screen / navigation ──────────────────────────────────────────────────── */

enum start_screen {
    SCREEN_SENSECAP_LOG = 0,
    SCREEN_WIFI_CONFIG = 1,
    SCREEN_DISPLAY_MODAL = 2,
    SCREEN_BROKER_MODAL = 3,
};

/* ── WiFi ─────────────────────────────────────────────────────────────────── */

#define WIFI_SCAN_LIST_SIZE 15

/* SSIDs are up to 32 bytes; ssid[33] keeps room for the NUL terminator so a
 * full-length 32-char SSID from a wifi_ap_record_t / wifi_event_sta_connected_t
 * copies in without a one-byte overflow. These structs are event payloads
 * (VIEW_EVENT_WIFI_ST, VIEW_EVENT_WIFI_LIST); producers/consumers use sizeof(). */
struct view_data_wifi_st {
    bool    is_connected;   /* station associated to the AP (drives the status icon) */
    bool    is_connecting;  /* association in progress */
    bool    is_network;     /* station has an IP (set on IP_EVENT_STA_GOT_IP);
                             * this is the MQTT start gate — NOT internet
                             * reachability. The diagnostic gateway ping never
                             * writes this field. */
    char    ssid[33];
    int8_t  rssi;
};

struct view_data_wifi_config {
    char    ssid[32];
    uint8_t password[64];
    bool    have_password;
};

struct view_data_wifi_item {
    char    ssid[33];
    bool    auth_mode;
    int8_t  rssi;
};

struct view_data_wifi_list {
    bool                      is_connect;
    struct view_data_wifi_item connect;
    uint16_t                  cnt;
    struct view_data_wifi_item aps[WIFI_SCAN_LIST_SIZE];
};

struct view_data_wifi_connet_ret_msg {
    uint8_t ret; /* 0 = success, 1 = failure */
    char    msg[64];
};

/* ── Display ──────────────────────────────────────────────────────────────── */

struct view_data_display {
    int  brightness;        /* 0–100 */
    bool sleep_mode_en;
    int  sleep_mode_time_min;
};

/* ── Time ─────────────────────────────────────────────────────────────────── */

struct view_data_time_cfg {
    bool   time_format_24;
    bool   auto_update;
    time_t time;
    bool   set_time;
    bool   auto_update_zone;
    int8_t zone;
    bool   daylight;
} __attribute__((packed));

/* ── Sensors ──────────────────────────────────────────────────────────────── */

struct sensor_data_average {
    float  data;
    time_t timestamp;
    bool   valid;
};

struct sensor_data_minmax {
    float  max;
    float  min;
    time_t timestamp;
    bool   valid;
};

#define SENSOR_TYPE_LIST                           \
    X(SCD41_SENSOR_CO2,      "SCD41_CO2")         \
    X(SGP40_SENSOR_TVOC,     "SGP40_TVOC")        \
    X(SHT41_SENSOR_TEMP,     "SHT41_TEMP")        \
    X(SHT41_SENSOR_HUMIDITY, "SHT41_HUMIDITY")

#define X(type, str) type,
enum sensor_data_type {
    SENSOR_TYPE_LIST ENUM_SENSOR_ALL
};
#undef X

struct view_data_sensor_data {
    enum sensor_data_type sensor_type;
    float                 value;
};

struct view_data_sensor_history_data {
    enum sensor_data_type     sensor_type;
    struct sensor_data_average data_day[24];
    struct sensor_data_minmax  data_week[7];
    uint8_t                   resolution;
    float                     day_min;
    float                     day_max;
    float                     week_min;
    float                     week_max;
};

/* ── Home Assistant ───────────────────────────────────────────────────────── */

struct view_data_ha_sensor_data {
    uint8_t index;
    char    value[32];
};

struct view_data_ha_switch_data {
    uint8_t index;
    int     value;
};

/* ── Event IDs (VIEW_EVENT_BASE) ─────────────────────────────────────────── */

/*
 * Event manifest for view_event_handle (VIEW_EVENT_BASE).
 *
 * Each entry documents:
 *   P  — producer(s)
 *   C  — consumer(s)
 *   Payload — data type posted with the event (NULL = no data)
 *   LVGL    — "lock required" = consumer must hold lv_port semaphore
 *
 * Bus: view_event_handle (main.c). Other buses: mqtt_app_event_handle,
 * ha_cfg_event_handle.
 */
enum {
    /* P: wifi/wifi_model.c  C: wifi/wifi_view.c  Payload: uint8_t (enum start_screen) */
    VIEW_EVENT_SCREEN_START = 0,

    /* P: wifi/wifi_model.c (STA connect + IP_EVENT_STA_GOT_IP)
     * C: mqtt/mqtt.c, wifi/wifi_view.c, ha/ha_mqtt.c
     * Payload: struct view_data_wifi_st. is_network=true ("has IP") gates the
     * MQTT start; the diagnostic gateway ping never posts this event. */
    VIEW_EVENT_WIFI_ST,

    /* P: rp2040/rp2040.c  C: sensor/sensor_view.c, ha/ha_sensor.c */
    VIEW_EVENT_SENSOR_DATA,         /* struct view_data_sensor_data */

    /* P: wifi/wifi_model.c  C: wifi/wifi_view.c  Payload: struct view_data_wifi_list */
    VIEW_EVENT_WIFI_LIST,

    VIEW_EVENT_WIFI_LIST_START,     /* NULL — scan started */
    VIEW_EVENT_WIFI_LIST_REQ,       /* NULL — user requests scan */

    /* P: wifi/wifi_view.c  C: wifi/wifi_model.c  Payload: struct view_data_wifi_config */
    VIEW_EVENT_WIFI_CONNECT,

    /* P: wifi/wifi_model.c  C: wifi/wifi_view.c  Payload: struct view_data_wifi_connet_ret_msg */
    VIEW_EVENT_WIFI_CONNECT_RET,

    VIEW_EVENT_WIFI_CFG_DELETE,     /* NULL */

    /* P: display/display_model.c, display/display_view.c  Payload: struct view_data_display */
    VIEW_EVENT_DISPLAY_CFG,

    VIEW_EVENT_BRIGHTNESS_UPDATE,   /* uint8_t brightness (0–100) */

    /* C: display/display_model.c  Payload: struct view_data_display */
    VIEW_EVENT_DISPLAY_CFG_APPLY,

    /* P: btn/btn.c  C: wifi/wifi_model.c, rp2040/rp2040.c  Payload: NULL */
    VIEW_EVENT_SHUTDOWN,

    /* P: btn/btn.c  C: *** NO CONSUMER — posted but not yet handled ***  Payload: NULL
     * (wifi_model.c does not currently register a handler for this event) */
    VIEW_EVENT_FACTORY_RESET,

    /* P: btn/btn.c, display/display_model.c (sleep timer + LVGL-port touch wake)
     * C: display/display_model.c (single actuator: backlight on/off + timer,
     *    and syncs the lv_port sleep flag)
     * Payload: bool (0=off, 1=on) */
    VIEW_EVENT_SCREEN_CTRL,

    /* P: ha/ha_config.c  C: ha/ha_config.c  Payload: NULL (reads textarea directly) */
    VIEW_EVENT_MQTT_ADDR_CHANGED,

    /* P: ha/ha_mqtt.c  C: ha/ha_config.c  Payload: NULL */
    VIEW_EVENT_HA_ADDR_DISPLAY,

    /* P: ha/ha_sensor.c (HA→device values on indicator/display/set)
     * C: ha/ha_switch.c (updates the Bedroom/Loft temp card, LVGL lock held)
     * Payload: struct view_data_ha_sensor_data */
    VIEW_EVENT_HA_SENSOR,

    /* P: ha/ha_switch_screen.c  C: ha/ha_switch.c  Payload: struct view_data_ha_switch_data */
    VIEW_EVENT_HA_SWITCH_ST,

    /* P: ha/ha_switch.c  C: ha/ha_switch.c (updates screen, publishes state echo + persists)  Payload: struct view_data_ha_switch_data */
    VIEW_EVENT_HA_SWITCH_SET,

    /* P: rp2040/rp2040.c  C: sensor/sensor_view.c  Payload: NULL
     * Posted once when the RP2040 UART link is silent for >15s (co-processor
     * down / unplugged). Consumer blanks all sensor cards to "N/A". Link
     * recovery is implicit: the next VIEW_EVENT_SENSOR_DATA repaints values. */
    VIEW_EVENT_RP2040_STALE,

    VIEW_EVENT_ALL,
};

#ifdef __cplusplus
}
#endif
