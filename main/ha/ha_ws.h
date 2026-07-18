#ifndef HA_WS_H
#define HA_WS_H

#include <stdbool.h>

#include "esp_event.h"
#include "ha_ws_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Home Assistant WebSocket API client (model only — posts view events, never
 * touches LVGL).
 *
 * Connects out to ws(s)://<ha>:8123/api/websocket, authenticates with a
 * long-lived token and subscribes to the configured entities. State changes
 * are posted as VIEW_EVENT_HA_SENSOR, so the display tiles and trends history
 * consume them exactly like the MQTT indicator/display/set path. While the
 * client is enabled, ha_sensor.c ignores indicator/display/set to keep the
 * two live-data sources from double-feeding the history buffer; MQTT switch
 * traffic is unaffected. */

/* Lifecycle events, dispatched on ha_cfg_event_handle (ha_event_task) — the
 * only task allowed to create/stop/destroy the client (same race discipline
 * as the MQTT restart path, see ha_mqtt.c). Own event base so ha_mqtt.c's
 * HA_CFG_EVENT_BASE handler never sees these. */
ESP_EVENT_DECLARE_BASE(HA_WS_EVENT_BASE);

enum {
    HA_WS_CFG_CHANGED, /* config saved: clear auth latch, rebuild client */
    HA_WS_NET_UP,      /* station got an IP: start the client if enabled+idle */
    HA_WS_AUTH_FAIL,   /* auth_invalid from HA: teardown + terminal latch */
};

/* Connection status for the settings status card. Also the payload value of
 * VIEW_EVENT_HA_WS_STATUS (as uint8_t in view_data_ha_ws_status). */
typedef enum {
    HA_WS_STATUS_DISABLED = 0,   /* enabled=0 in config (default) */
    HA_WS_STATUS_UNCONFIGURED,   /* enabled but url/token/entities missing */
    HA_WS_STATUS_CONNECTING,     /* waiting for network or transport */
    HA_WS_STATUS_AUTHENTICATING, /* transport up, auth handshake in flight */
    HA_WS_STATUS_SUBSCRIBED,     /* live: entity updates streaming */
    HA_WS_STATUS_AUTH_FAILED,    /* token rejected — latched until reconfig */
} ha_ws_status_t;

typedef struct {
    ha_ws_status_t status;
    char           url[128];
    char           entity_id[HA_WS_ENTITY_NUM][64];
} ha_ws_status_snapshot_t;

/* Called from indicator_ha_model_init() after ha_cfg_event_handle exists. */
int ha_ws_init(void);

/* True when the stored config has enabled=1. Cheap cross-task read; used by
 * ha_sensor.c to gate the MQTT display/set path. */
bool ha_ws_is_enabled(void);

/* Thread-safe copy of the current status + configured targets (for the
 * settings status card; token is deliberately not exposed). */
void ha_ws_status_get(ha_ws_status_snapshot_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HA_WS_H */
