#ifndef HA_TREND_SCREEN_H
#define HA_TREND_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Trends tile (NAV_TILE_HA_TREND).
 *
 * An lv_chart with three line series — temperature, humidity and CO2 — fed by
 * the whole-series snapshots ha_history publishes over VIEW_EVENT_HA_HISTORY.
 * Temperature and humidity share the primary Y axis; CO2 uses the secondary Y
 * axis. Shows a "waiting for data" placeholder until the first sample arrives.
 *
 * Builds its widgets under the LVGL lock and registers its own bus handler.
 * Call once from the HA view init path (indicator_ha_view_init) and, for the
 * simulator, directly from sim/main.c.
 */
void ha_trend_screen_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_TREND_SCREEN_H */
