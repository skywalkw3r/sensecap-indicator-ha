#ifndef HA_WS_CONFIG_H
#define HA_WS_CONFIG_H

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Home Assistant WebSocket API client configuration.
 *
 * Stored under its own NVS key, never inside ha_cfg_interface: that struct's
 * size is an NVS compatibility contract (see ha_tls.h), and this one follows
 * the same rule — sizeof(ha_ws_cfg_t) is the stored-blob contract. Extend via
 * new NVS keys, not by growing this struct. */

#define HA_WS_CFG_STORAGE "ha-ws"

/* Display slots, index-aligned with the view contract (0=temp 1=humidity
 * 2=co2) shared with view_data_ha_sensor_data.index. */
#define HA_WS_ENTITY_NUM 3

typedef struct {
    char    url[128];                       /* normalized ws(s)://host:port/api/websocket */
    char    token[256];                     /* HA long-lived access token (~183 chars typical) */
    char    entity_id[HA_WS_ENTITY_NUM][64];/* HA entity per display slot; empty = unused */
    uint8_t enabled;                        /* 0 = off (default): client never starts */
} ha_ws_cfg_t;

/* Returns ESP_OK with the stored config, or a zeroed struct plus
 * ESP_ERR_NVS_NOT_FOUND (unconfigured) / ESP_ERR_INVALID_SIZE (stale blob). */
esp_err_t ha_ws_cfg_get(ha_ws_cfg_t *cfg);
esp_err_t ha_ws_cfg_set(ha_ws_cfg_t *cfg);

/* Normalize user input into "ws(s)://host:port/api/websocket".
 * Accepts a bare host/IP, host:port, or a ws://, wss://, http://, https://
 * prefix (http maps to ws, https to wss). Any path is discarded. Default port:
 * 8123 for plaintext (local HA), 443 for TLS (reverse proxy / Nabu Casa).
 * Returns false on empty/oversized host or invalid port. */
bool ha_ws_url_normalize(const char *input, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* HA_WS_CONFIG_H */
