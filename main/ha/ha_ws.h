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
 * long-lived token and subscribes to every subscribable dashboard slot
 * (dashboard_config.h via ha/ha_dash.h). Entity states are posted as
 * VIEW_EVENT_HA_ENTITY (media players as VIEW_EVENT_HA_MEDIA); slots with a
 * legacy display index additionally post VIEW_EVENT_HA_SENSOR so the history
 * ring / Trends chart consume them exactly like the MQTT indicator/display/set
 * path. While the client is enabled, ha_sensor.c ignores indicator/display/set
 * to keep the two live-data sources from double-feeding the history buffer.
 *
 * The write path is ha_ws_call(): dashboard screens queue HA service calls
 * (light.turn_on, script.turn_on, media_player.media_play_pause, ...) which
 * are framed and sent from ha_event_task. State echoes come back through the
 * entity subscription, reconciling the optimistic UI. */

/* Lifecycle events, dispatched on ha_cfg_event_handle (ha_event_task) — the
 * only task allowed to create/stop/destroy the client (same race discipline
 * as the MQTT restart path, see ha_mqtt.c). Own event base so ha_mqtt.c's
 * HA_CFG_EVENT_BASE handler never sees these. */
ESP_EVENT_DECLARE_BASE(HA_WS_EVENT_BASE);

enum {
    HA_WS_CFG_CHANGED,   /* config saved: clear auth latch, rebuild client */
    HA_WS_NET_UP,        /* station got an IP: start the client if enabled+idle */
    HA_WS_AUTH_FAIL,     /* auth_invalid from HA: teardown + terminal latch */
    HA_WS_TX_CALL,       /* queued service call; payload ha_ws_call_req_t */
    HA_WS_STALE_HANDSHAKE, /* watchdog: not SUBSCRIBED in time — rebuild */
};

/* Service-call request (payload of HA_WS_TX_CALL; esp_event copies it whole).
 * extra is "" or a complete JSON object used verbatim as service_data. */
typedef struct {
    char domain[24];
    char service[32];
    char entity_id[64];
    char extra[64];
} ha_ws_call_req_t;

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
} ha_ws_status_snapshot_t;

/* Called from indicator_ha_model_init() after ha_cfg_event_handle exists. */
int ha_ws_init(void);

/* True when the stored config has enabled=1. Cheap cross-task read; used by
 * ha_sensor.c to gate the MQTT display/set path. */
bool ha_ws_is_enabled(void);

/* Thread-safe copy of the current status + configured target (for the
 * settings status card; token is deliberately not exposed). */
void ha_ws_status_get(ha_ws_status_snapshot_t *out);

/* Queue a Home Assistant service call. Safe from any task: the request is
 * posted to ha_event_task (bounded 100 ms) which owns the client lifetime,
 * frames the JSON and sends it. Dropped with a warn log unless the client is
 * SUBSCRIBED. `extra` is NULL or a complete JSON object for service_data,
 * e.g. "{\"brightness_pct\":40}". */
esp_err_t ha_ws_call(const char *domain, const char *service,
                     const char *entity_id, const char *extra);

#ifdef __cplusplus
}
#endif

#endif /* HA_WS_H */
