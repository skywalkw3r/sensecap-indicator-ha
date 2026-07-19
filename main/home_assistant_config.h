/**
 * @file home_assistant_config.h
 * @date  23 July 2024

 * @author Spencer Yan
 *
 * @note 
 *
 * @copyright © 2024, Seeed Studio
 */

#ifndef HOME_ASSISTANT_CONFIG_H
#define HOME_ASSISTANT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif


/* Default MQTT config is intentionally EMPTY: a device ships unconfigured and
 * must be pointed at a broker via the touchscreen MQTT page or `setmqtt`.
 * Never fall back to a public internet broker (e.g. broker.emqx.io) — that
 * would silently repoint a configured device onto the public internet. */
#define CONFIG_BROKER_URL                        ""
/* The following are examples only and are NOT auto-applied as defaults; the
 * runtime client id falls back to "indicator" + MAC suffix when unset. */
#define CONFIG_MQTT_CLIENT_ID                    ""
#define CONFIG_MQTT_USERNAME                     ""
#define CONFIG_MQTT_PASSWORD                     ""

#define CONFIG_HA_SENSOR_ENTITY_NUM              6

// topic
#define CONFIG_TOPIC_SENSOR_DATA                 "indicator/sensor"
#define CONFIG_TOPIC_SENSOR_DATA_QOS             0

/* Buzzer as an HA MQTT siren entity (ha_siren.c). */
#define CONFIG_TOPIC_SIREN_STATE                 "indicator/siren/state"
#define CONFIG_TOPIC_SIREN_SET                   "indicator/siren/set"
#define CONFIG_TOPIC_SIREN_QOS                   1

/* HA -> device display values (this is NOT the device's own sensor publish
 * topic, so there is no publish/subscribe echo loop). Home Assistant pushes
 * e.g. {"loft_temp": 72.4, "loft_humidity": 45, "loft_co2": 620} here — any
 * subset per message — and the panel renders them read-only.
 * Display indices (view_data_ha_sensor_data.index): 0=temp 1=humidity 2=co2. */
#define CONFIG_TOPIC_DISPLAY_SET                 "indicator/display/set"
#define CONFIG_HA_DISPLAY_VALUE_NUM              3
#define CONFIG_HA_TEMP_VALUE_KEY                 "loft_temp"
#define CONFIG_HA_HUMIDITY_VALUE_KEY             "loft_humidity"
#define CONFIG_HA_CO2_VALUE_KEY                  "loft_co2"
#define CONFIG_HA_TEMP_UI_NAME                   "Temperature"
#define CONFIG_HA_TEMP_UI_UNIT                   "°F"
#define CONFIG_HA_HUMIDITY_UI_NAME               "Humidity"
#define CONFIG_HA_HUMIDITY_UI_UNIT               "%"
#define CONFIG_HA_CO2_UI_NAME                    "CO2"
#define CONFIG_HA_CO2_UI_UNIT                    "ppm"

// buildin sensor
#define CONFIG_SENSOR_BUILDIN_CO2_VALUE_KEY      "co2"
#define CONFIG_SENSOR_BUILDIN_TVOC_VALUE_KEY     "tvoc"
#define CONFIG_SENSOR_BUILDIN_TEMP_VALUE_KEY     "temp"
#define CONFIG_SENSOR_BUILDIN_HUMIDITY_VALUE_KEY "humidity"
#define CONFIG_SENSOR_BUILDIN_TOPIC_DATA         CONFIG_TOPIC_SENSOR_DATA

// sensor1
#define CONFIG_SENSOR1_VALUE_KEY                 "temp"
#define CONFIG_SENSOR1_UI_UNIT                   "°C"
#define CONFIG_SENSOR1_UI_NAME                   "Temp"
#define CONFIG_SENSOR1_TOPIC_DATA                CONFIG_TOPIC_SENSOR_DATA

// sensor2
#define CONFIG_SENSOR2_VALUE_KEY                 "humidity"
#define CONFIG_SENSOR2_UI_UNIT                   "%"
#define CONFIG_SENSOR2_UI_NAME                   "Humidity"
#define CONFIG_SENSOR2_TOPIC_DATA                CONFIG_TOPIC_SENSOR_DATA

// sensor3
#define CONFIG_SENSOR3_VALUE_KEY                 "tvoc"
#define CONFIG_SENSOR3_UI_UNIT                   ""
#define CONFIG_SENSOR3_UI_NAME                   "tVOC"
#define CONFIG_SENSOR3_TOPIC_DATA                CONFIG_TOPIC_SENSOR_DATA

// sensor4
#define CONFIG_SENSOR4_VALUE_KEY                 "co2"
#define CONFIG_SENSOR4_UI_UNIT                   "ppm"
#define CONFIG_SENSOR4_UI_NAME                   "CO2"
#define CONFIG_SENSOR4_TOPIC_DATA                CONFIG_TOPIC_SENSOR_DATA

// sensor5
#define CONFIG_SENSOR5_VALUE_KEY                 "temp"
#define CONFIG_SENSOR5_UI_UNIT                   "°C"
#define CONFIG_SENSOR5_UI_NAME                   "Temp"
#define CONFIG_SENSOR5_TOPIC_DATA                CONFIG_TOPIC_SENSOR_DATA

// sensor6
#define CONFIG_SENSOR6_VALUE_KEY                 "humidity"
#define CONFIG_SENSOR6_UI_UNIT                   "%"
#define CONFIG_SENSOR6_UI_NAME                   "Humidity"
#define CONFIG_SENSOR6_TOPIC_DATA                CONFIG_TOPIC_SENSOR_DATA

/* The switch1..8 MQTT topic bridge was deliberately retired on this branch:
 * dashboard controls (dashboard_config.h) call HA services directly over the
 * WebSocket client, which also syncs true entity state back to the panel. */

// New arrays for simplified entity initialization
#define CONFIG_SENSOR_VALUE_KEYS { \
    CONFIG_SENSOR1_VALUE_KEY, CONFIG_SENSOR2_VALUE_KEY, CONFIG_SENSOR3_VALUE_KEY, \
    CONFIG_SENSOR4_VALUE_KEY, CONFIG_SENSOR5_VALUE_KEY, CONFIG_SENSOR6_VALUE_KEY \
}

#define CONFIG_SENSOR_TOPICS { \
    CONFIG_SENSOR1_TOPIC_DATA, CONFIG_SENSOR2_TOPIC_DATA, CONFIG_SENSOR3_TOPIC_DATA, \
    CONFIG_SENSOR4_TOPIC_DATA, CONFIG_SENSOR5_TOPIC_DATA, CONFIG_SENSOR6_TOPIC_DATA \
}

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*HOME_ASSISTANT_CONFIG_H*/