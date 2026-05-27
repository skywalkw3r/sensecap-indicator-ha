#ifndef VIEW_DATA_H
#define VIEW_DATA_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"

#include <bsp_board.h>
#include "esp_event.h"
#include "esp_event_base.h"

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(VIEW_EVENT_BASE);
extern esp_event_loop_handle_t view_event_handle;

enum start_screen
{
	SCREEN_SENSECAP_LOG, // todo
	SCREEN_WIFI_CONFIG,
};

#define WIFI_SCAN_LIST_SIZE 15

struct view_data_wifi_st
{
	bool is_connected;
	bool is_connecting;
	bool is_network; // is connect network
	char ssid[32];
	int8_t rssi;
};

struct view_data_wifi_config
{
	char ssid[32];
	uint8_t password[64];
	bool have_password;
};

struct view_data_wifi_item
{
	char ssid[32];
	bool auth_mode;
	int8_t rssi;
};

struct view_data_wifi_list
{
	bool is_connect;
	struct view_data_wifi_item connect;
	uint16_t cnt;
	struct view_data_wifi_item aps[WIFI_SCAN_LIST_SIZE];
};

struct view_data_wifi_connet_ret_msg
{
	uint8_t ret; // 0:successfull , 1: failure
	char msg[64];
};

struct view_data_display
{
	int brightness; // 0~100
	bool sleep_mode_en; // Turn Off Screen
	int sleep_mode_time_min;
};

struct view_data_time_cfg
{
	bool time_format_24;

	bool auto_update; // time auto update
	time_t time; // use when auto_update is true
	bool set_time;

	bool auto_update_zone; // use when  auto_update  is true
	int8_t zone; // use when auto_update_zone is true

	bool daylight; // use when auto_update is true
} __attribute__((packed));

struct sensor_data_average
{
	float data; // Average over the past hour
	time_t timestamp;
	bool valid;
};

struct sensor_data_minmax
{
	float max;
	float min;
	time_t timestamp;
	bool valid;
};

/**
 * @brief all of the sensors that you might have
 * (enum, string)
 */
#define SENSOR_TYPE_LIST                       \
	X(SCD41_SENSOR_CO2, "SCD41_CO2")           \
	X(SGP40_SENSOR_TVOC, "SGP40_TVOC")         \
	X(SHT41_SENSOR_TEMP, "SHT41_TEMP")         \
	X(SHT41_SENSOR_HUMIDITY, "SHT41_HUMIDITY") \

#define X(type, str) type,
enum sensor_data_type
{
	SENSOR_TYPE_LIST ENUM_SENSOR_ALL
}; // Define sensor types
#undef X

struct view_data_sensor_data
{
	enum sensor_data_type sensor_type;
	float value;
};

struct view_data_sensor_history_data
{
	enum sensor_data_type sensor_type;
	struct sensor_data_average data_day[24];
	struct sensor_data_minmax data_week[7];
	uint8_t resolution;

	float day_min;
	float day_max;

	float week_min;
	float week_max;
};

struct view_data_ha_sensor_data
{
	uint8_t index;
	char value[32];
};

struct view_data_ha_switch_data
{
	uint8_t index;
	int value;
};

/*
 * Event manifest for view_event_handle (VIEW_EVENT_BASE).
 *
 * Each entry documents:
 *   P  — producer(s): file that calls esp_event_post_to(..., VIEW_EVENT_BASE, <event>, ...)
 *   C  — consumer(s): file that registers an event handler for this event
 *   Payload — data type posted with the event (NULL = no data)
 *   LVGL    — "lock required" means the consumer must hold lv_port semaphore when touching widgets
 *
 * Bus: view_event_handle, defined in main.c, declared extern here.
 * Other buses in this system: mqtt_app_event_handle, ha_cfg_event_handle, cmd_cfg_event_handle.
 */
enum
{
	/* P: indicator_wifi_model.c
	 * C: indicator_wifi_view.c
	 * Payload: uint8_t (enum start_screen)
	 * LVGL: lock required */
	VIEW_EVENT_SCREEN_START = 0, // uint8_t (enum start_screen)

	/* P: indicator_display_model.c  (TODO: verify)
	 * C: indicator_display_view.c   (TODO: verify)
	 * Payload: bool time_format_24 */
	VIEW_EVENT_TIME, // bool time_format_24

	/* P: indicator_wifi_model.c
	 * C: indicator_mqtt.c (updates mqtt_net_flag, no LVGL),
	 *    indicator_wifi_view.c (UI update, LVGL lock required),
	 *    ha/ha_mqtt.c (triggers MQTT_APP_START, no LVGL)
	 * Payload: struct view_data_wifi_st */
	VIEW_EVENT_WIFI_ST, // struct view_data_wifi_st

	/* P: (TODO: not yet identified)
	 * C: (TODO: not yet identified)
	 * Payload: char city[32], max display 24 chars */
	VIEW_EVENT_CITY, // char city[32]

	/* P: app/indicator_sensor_model.c
	 * C: app/indicator_sensor_view.c (display, LVGL lock required),
	 *    ha/ha_sensor.c (publish built-in sensor readings to MQTT, no LVGL)
	 * Payload: struct view_data_sensor_data */
	VIEW_EVENT_SENSOR_DATA, // struct view_data_sensor_data

	/* P: (TODO: not yet identified — history aggregation)
	 * C: (TODO: not yet identified)
	 * Payload: none (trigger only) */
	VIEW_EVENT_SENSOR_TEMP_HISTORY, // NULL (trigger)
	VIEW_EVENT_SENSOR_HUMIDITY_HISTORY, // NULL (trigger)
	VIEW_EVENT_SENSOR_TVOC_HISTORY, // NULL (trigger)
	VIEW_EVENT_SENSOR_CO2_HISTORY,

	/* P: (TODO: not yet identified)
	 * C: (TODO: not yet identified)
	 * Payload: struct view_data_sensor_history_data */
	VIEW_EVENT_SENSOR_DATA_HISTORY, // struct view_data_sensor_history_data

	/* P: indicator_wifi_model.c (scan results), indicator_wifi_view.c (local repost), ui/ui_events.c
	 * C: indicator_wifi_view.c (renders list, LVGL lock required)
	 * Payload: struct view_data_wifi_list */
	VIEW_EVENT_WIFI_LIST, // struct view_data_wifi_list

	/* P: indicator_wifi_model.c (scan started)
	 * C: indicator_wifi_view.c (shows spinner, LVGL lock required)
	 * Payload: NULL */
	VIEW_EVENT_WIFI_LIST_START, // NULL

	/* P: ui/ui_events.c (user taps "scan" button)
	 * C: indicator_wifi_model.c (starts scan, no LVGL)
	 * Payload: NULL */
	VIEW_EVENT_WIFI_LIST_REQ, // NULL

	/* P: ui/ui_events.c (user submits credentials)
	 * C: indicator_wifi_model.c (initiates connection, no LVGL)
	 * Payload: struct view_data_wifi_config */
	VIEW_EVENT_WIFI_CONNECT, // struct view_data_wifi_config

	/* P: indicator_wifi_model.c (connection result)
	 * C: indicator_wifi_view.c (shows result message, LVGL lock required)
	 * Payload: struct view_data_wifi_connet_ret_msg */
	VIEW_EVENT_WIFI_CONNECT_RET, // struct view_data_wifi_connet_ret_msg

	/* P: ui/ui_events.c (user taps "forget network")
	 * C: indicator_wifi_model.c (clears NVS, no LVGL)
	 * Payload: NULL */
	VIEW_EVENT_WIFI_CFG_DELETE, // NULL

	/* P: (TODO: not yet identified)
	 * C: (TODO: not yet identified)
	 * Payload: struct view_data_time_cfg */
	VIEW_EVENT_TIME_CFG_UPDATE, // struct view_data_time_cfg
	VIEW_EVENT_TIME_CFG_APPLY, // struct view_data_time_cfg

	/* P: indicator_display_model.c, indicator_display_view.c
	 * C: (TODO: verify consumers)
	 * Payload: struct view_data_display */
	VIEW_EVENT_DISPLAY_CFG, // struct view_data_display

	/* P: (TODO: not yet identified)
	 * C: (TODO: not yet identified)
	 * Payload: uint8_t brightness (0–100) */
	VIEW_EVENT_BRIGHTNESS_UPDATE, // uint8_t brightness

	/* P: (TODO: not yet identified)
	 * C: indicator_display_model.c (applies and saves, no LVGL)
	 * Payload: struct view_data_display */
	VIEW_EVENT_DISPLAY_CFG_APPLY, // struct view_data_display

	/* P: indicator_btn.c (long press)
	 * C: indicator_wifi_model.c (disconnect, no LVGL),
	 *    app/esp32_rp2040.c (power off RP2040, no LVGL)
	 * Payload: NULL */
	VIEW_EVENT_SHUTDOWN, // NULL

	/* P: (TODO: not yet identified — likely indicator_btn.c)
	 * C: indicator_wifi_model.c (clears NVS + reboots, no LVGL)
	 * Payload: NULL */
	VIEW_EVENT_FACTORY_RESET, // NULL

	/* P: indicator_btn.c (screen wake/sleep), indicator_display_model.c
	 * C: (TODO: verify — likely lv_port or display driver)
	 * Payload: bool (0 = off, 1 = on) */
	VIEW_EVENT_SCREEN_CTRL, // bool (0=off, 1=on)

	/* P: ui/ui_events.c (user submits new broker IP in textarea)
	 * C: ha/ha_config.c (validates, saves NVS, triggers reconnect — reads textarea, no payload)
	 * Payload: NULL (consumer reads lv_textarea directly)
	 * LVGL: producer runs in LVGL task; consumer reads widget — must hold LVGL lock */
	VIEW_EVENT_MQTT_ADDR_CHANGED, // NULL (consumer reads lv_textarea directly)

	/* P: ha/ha_mqtt.c (on init, to populate IP field from NVS)
	 * C: ha/ha_config.c (updates ui_textarea_ip_0, LVGL lock required)
	 * Payload: NULL (consumer calls ha_cfg_get internally) or char* broker_url */
	VIEW_EVENT_HA_ADDR_DISPLAY, // NULL or char* broker_url

	/* P: ha/ha_sensor.c (external HA sensor value received via MQTT)
	 * C: *** NO CONSUMER — handler registration is commented out ***
	 *    (Feature incomplete: HA sensor data screen not yet wired up)
	 * Payload: struct view_data_ha_sensor_data */
	VIEW_EVENT_HA_SENSOR, // struct view_data_ha_sensor_data (DEAD: no consumer)

	/* P: ui/ui_events.c (user toggles a switch widget → LV_EVENT_VALUE_CHANGED callback)
	 * C: ha/ha_switch.c (publishes state to MQTT, saves NVS — no LVGL)
	 * Payload: struct view_data_ha_switch_data */
	VIEW_EVENT_HA_SWITCH_ST, // struct view_data_ha_switch_data

	/* P: ha/ha_switch.c (MQTT command received or NVS restore on connect)
	 * C: ha/ha_switch.c (updates switch widget via ha_switch_screen_update, LVGL lock required)
	 * Payload: struct view_data_ha_switch_data
	 * Note: producer and consumer are the same file — internal feedback loop */
	VIEW_EVENT_HA_SWITCH_SET, // struct view_data_ha_switch_data

	VIEW_EVENT_ALL,
};

#ifdef __cplusplus
}
#endif

#endif
