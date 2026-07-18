#ifndef HA_HISTORY_H
#define HA_HISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sensor-history model for the trends chart.
 *
 * Consumes VIEW_EVENT_HA_SENSOR (HA-pushed display values on
 * indicator/display/set — display indices 0=temp 1=humidity 2=co2), parses the
 * string value to a float, keeps a per-series ring buffer of the most recent
 * HA_HISTORY_MAX_SAMPLES points, and re-publishes VIEW_EVENT_HA_HISTORY with a
 * whole-series snapshot so the trends view can redraw.
 *
 * Pure model: no LVGL/widget ownership, no MQTT/NVS. Call once from the HA
 * domain model init path (indicator_ha_model_init), the same place
 * ha_switch_init() is wired.
 */
void ha_history_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_HISTORY_H */
