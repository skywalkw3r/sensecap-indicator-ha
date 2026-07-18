#include "ha_ws.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ha_mqtt.h"
#include "ha_tls.h"
#include "mqtt.h"
#include "view_data.h"

static const char *TAG = "ha-ws";

ESP_EVENT_DEFINE_BASE(HA_WS_EVENT_BASE);

/* Incoming messages are reassembled here before parsing. subscribe_entities'
 * initial snapshot for 3 entities (states + attributes) can exceed the 4 KB
 * transport buffer; 8 KB covers it with headroom. Larger messages are dropped
 * whole and logged — the next entity diff repaints the affected tile. */
#define HA_WS_RX_BUF_MAX   8192
#define HA_WS_TX_BUF_MAX   384
#define HA_WS_BUFFER_SIZE  4096
#define HA_WS_TASK_STACK   6144

#define WS_OPCODE_CONT 0x00
#define WS_OPCODE_TEXT 0x01

static esp_websocket_client_handle_t s_client = NULL;
static ha_ws_cfg_t                   s_cfg;
static volatile bool                 s_enabled = false;

/* CA PEM for the active client. esp_websocket_client stores the certificate
 * POINTER (not a copy), so this must outlive the client; freed and reloaded
 * on every rebuild — only after the client was destroyed. */
static char *s_tls_ca = NULL;

/* Reassembly state. Touched only by the WS client task (event handler) while
 * the client is alive; _ws_teardown() destroys the client (joining that task)
 * before anything else resets it. */
static char  *s_rx_buf = NULL;
static size_t s_rx_len = 0;
static bool   s_rx_drop = false;

/* Protocol scratch: auth is the largest frame (token[256] + framing < 384).
 * Sends happen sequentially inside the WS task, so one buffer serves both. */
static char s_tx_buf[HA_WS_TX_BUF_MAX];
static int  s_msg_id = 1;

/* Status snapshot for the settings card, guarded for cross-task reads (LVGL
 * task) against writes from ha_event_task and the WS task. */
static ha_ws_status_snapshot_t s_snapshot;
static portMUX_TYPE            s_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;

static void _ws_apply_config(void);

static ha_ws_status_t _status_get(void)
{
    taskENTER_CRITICAL(&s_snapshot_mux);
    ha_ws_status_t st = s_snapshot.status;
    taskEXIT_CRITICAL(&s_snapshot_mux);
    return st;
}

static void _status_set(ha_ws_status_t status)
{
    taskENTER_CRITICAL(&s_snapshot_mux);
    s_snapshot.status = status;
    taskEXIT_CRITICAL(&s_snapshot_mux);

    /* Always announced, even without a transition: the status modal re-reads
     * companion state (the MQTT toggle) on this event, so a same-status
     * rebuild must still refresh it. Low rate — worst case one per reconnect.
     * Posted from ha_event_task or the WS task, never the view task: bound the
     * post and tolerate a drop (the status card re-reads the snapshot on open). */
    struct view_data_ha_ws_status data = {.status = (uint8_t)status};
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_WS_STATUS,
                                      &data, sizeof(data), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drop VIEW_EVENT_HA_WS_STATUS: %s", esp_err_to_name(err));
    }
}

/* Mirror url + entity ids into the snapshot (config mutations happen only in
 * ha_event_task while the client is torn down). */
static void _snapshot_sync_cfg(void)
{
    taskENTER_CRITICAL(&s_snapshot_mux);
    memcpy(s_snapshot.url, s_cfg.url, sizeof(s_snapshot.url));
    memcpy(s_snapshot.entity_id, s_cfg.entity_id, sizeof(s_snapshot.entity_id));
    taskEXIT_CRITICAL(&s_snapshot_mux);
}

void ha_ws_status_get(ha_ws_status_snapshot_t *out)
{
    taskENTER_CRITICAL(&s_snapshot_mux);
    *out = s_snapshot;
    taskEXIT_CRITICAL(&s_snapshot_mux);
}

bool ha_ws_is_enabled(void)
{
    return s_enabled;
}

static bool _cfg_is_complete(const ha_ws_cfg_t *cfg)
{
    if (cfg->url[0] == '\0' || cfg->token[0] == '\0') {
        return false;
    }
    for (int i = 0; i < HA_WS_ENTITY_NUM; i++) {
        if (cfg->entity_id[i][0] != '\0') {
            return true;
        }
    }
    return false;
}

/* ── Display value formatting ─────────────────────────────────────────────── */

/* Same rendering rules as the MQTT display/set path (ha_sensor.c): whole
 * numbers without a trailing .0, one decimal otherwise. HA WS states are
 * strings ("72.41666") and often unrounded, so numeric strings are re-formatted
 * too; non-numeric states ("unavailable") pass through verbatim. */
static void _format_number(double value, char *out, size_t out_size)
{
    if (value == (double)(long)value) {
        snprintf(out, out_size, "%ld", (long)value);
    } else {
        snprintf(out, out_size, "%.1f", value);
    }
}

static void _format_state_value(const cJSON *state, char *out, size_t out_size)
{
    out[0] = '\0';
    if (cJSON_IsNumber(state)) {
        _format_number(state->valuedouble, out, out_size);
    } else if (cJSON_IsString(state) && state->valuestring != NULL) {
        const char *s = state->valuestring;
        char *end = NULL;
        double v = strtod(s, &end);
        if (end != s && *end == '\0') {
            _format_number(v, out, out_size);
        } else {
            strncpy(out, s, out_size - 1);
            out[out_size - 1] = '\0';
        }
    }
}

static void _post_sensor_value(int index, const cJSON *state)
{
    struct view_data_ha_sensor_data data = {.index = (uint8_t)index};
    _format_state_value(state, data.value, sizeof(data.value));
    if (data.value[0] == '\0') {
        return;
    }

    ESP_LOGI(TAG, "%s = %s -> index %d", s_cfg.entity_id[index], data.value, index);
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SENSOR,
                                      &data, sizeof(data), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drop VIEW_EVENT_HA_SENSOR: %s", esp_err_to_name(err));
    }
}

/* ── Protocol sends (WS task context) ─────────────────────────────────────── */

static void _ws_send(const char *buf, int len)
{
    if (esp_websocket_client_send_text(s_client, buf, len, pdMS_TO_TICKS(2000)) < 0) {
        /* Send failures surface as transport errors next; the reconnect loop
         * redoes the whole handshake, so just log here. */
        ESP_LOGW(TAG, "WS send failed");
    }
}

static void _send_auth(void)
{
    int len = snprintf(s_tx_buf, sizeof(s_tx_buf),
                       "{\"type\":\"auth\",\"access_token\":\"%s\"}", s_cfg.token);
    if (len <= 0 || len >= (int)sizeof(s_tx_buf)) {
        ESP_LOGE(TAG, "auth frame overflow");
        return;
    }
    _ws_send(s_tx_buf, len);
}

static void _send_subscribe(void)
{
    int len = snprintf(s_tx_buf, sizeof(s_tx_buf),
                       "{\"id\":%d,\"type\":\"subscribe_entities\",\"entity_ids\":[", s_msg_id++);
    bool first = true;
    for (int i = 0; i < HA_WS_ENTITY_NUM; i++) {
        if (s_cfg.entity_id[i][0] == '\0') {
            continue;
        }
        len += snprintf(s_tx_buf + len, sizeof(s_tx_buf) - len, "%s\"%s\"",
                        first ? "" : ",", s_cfg.entity_id[i]);
        first = false;
    }
    len += snprintf(s_tx_buf + len, sizeof(s_tx_buf) - len, "]}");
    if (len >= (int)sizeof(s_tx_buf)) {
        ESP_LOGE(TAG, "subscribe frame overflow");
        return;
    }
    _ws_send(s_tx_buf, len);
}

/* ── Incoming message handling (WS task context) ──────────────────────────── */

/* subscribe_entities event payload (compressed-state format):
 *   {"a": {"<entity>": {"s": "72.4", ...}}}          initial snapshot
 *   {"c": {"<entity>": {"+": {"s": "72.6", ...}}}}   diff (s absent when only
 *                                                    attributes changed)
 *   {"r": ["<entity>"]}                              entity removed           */
static void _handle_entities_event(const cJSON *event)
{
    const cJSON *add = cJSON_GetObjectItem(event, "a");
    const cJSON *chg = cJSON_GetObjectItem(event, "c");
    const cJSON *rem = cJSON_GetObjectItem(event, "r");

    for (int i = 0; i < HA_WS_ENTITY_NUM; i++) {
        if (s_cfg.entity_id[i][0] == '\0') {
            continue;
        }
        if (add != NULL) {
            const cJSON *ent = cJSON_GetObjectItem(add, s_cfg.entity_id[i]);
            if (ent != NULL) {
                _post_sensor_value(i, cJSON_GetObjectItem(ent, "s"));
            }
        }
        if (chg != NULL) {
            const cJSON *ent = cJSON_GetObjectItem(chg, s_cfg.entity_id[i]);
            if (ent != NULL) {
                const cJSON *plus = cJSON_GetObjectItem(ent, "+");
                if (plus != NULL) {
                    _post_sensor_value(i, cJSON_GetObjectItem(plus, "s"));
                }
            }
        }
    }

    if (cJSON_IsArray(rem) && cJSON_GetArraySize(rem) > 0) {
        ESP_LOGW(TAG, "HA removed %d subscribed entit(y/ies) — check entity ids in 'haconfig'",
                 cJSON_GetArraySize(rem));
    }
}

static void _handle_message(const char *buf, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(buf, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "unparseable WS message (%u bytes)", (unsigned)len);
        return;
    }

    const cJSON *type_item = cJSON_GetObjectItem(root, "type");
    const char *type = cJSON_IsString(type_item) ? type_item->valuestring : "";

    if (strcmp(type, "auth_required") == 0) {
        ESP_LOGI(TAG, "auth_required -> sending token");
        _send_auth();
    } else if (strcmp(type, "auth_ok") == 0) {
        const cJSON *ver = cJSON_GetObjectItem(root, "ha_version");
        ESP_LOGI(TAG, "auth_ok (HA %s) -> subscribing",
                 cJSON_IsString(ver) ? ver->valuestring : "?");
        _send_subscribe();
    } else if (strcmp(type, "auth_invalid") == 0) {
        const cJSON *msg = cJSON_GetObjectItem(root, "message");
        ESP_LOGE(TAG, "auth_invalid: %s", cJSON_IsString(msg) ? msg->valuestring : "(no message)");
        /* Client stop/destroy is forbidden from this task — hop to
         * ha_event_task, which tears down and latches AUTH_FAILED. */
        esp_event_post_to(ha_cfg_event_handle, HA_WS_EVENT_BASE, HA_WS_AUTH_FAIL,
                          NULL, 0, pdMS_TO_TICKS(100));
    } else if (strcmp(type, "result") == 0) {
        if (cJSON_IsTrue(cJSON_GetObjectItem(root, "success"))) {
            ESP_LOGI(TAG, "subscribed — live entity updates streaming");
            _status_set(HA_WS_STATUS_SUBSCRIBED);
        } else {
            /* Healthy connection, bad command (e.g. malformed entity id).
             * Stay idle instead of hammering HA with retries. */
            char *err = cJSON_PrintUnformatted(cJSON_GetObjectItem(root, "error"));
            ESP_LOGE(TAG, "subscribe rejected: %s — fix entity ids via 'setha'",
                     err ? err : "(unknown)");
            free(err);
        }
    } else if (strcmp(type, "event") == 0) {
        const cJSON *event = cJSON_GetObjectItem(root, "event");
        if (event != NULL) {
            _handle_entities_event(event);
        }
    } else {
        ESP_LOGD(TAG, "ignoring WS message type '%s'", type);
    }

    cJSON_Delete(root);
}

/* Reassemble WEBSOCKET_EVENT_DATA chunks into whole messages. Two layers of
 * splitting can occur: a frame larger than the transport buffer arrives as
 * several events with growing payload_offset, and a message may span multiple
 * WS frames (fin=0 text frame + continuation frames). The cursor accumulates
 * across both; a message is complete at the end of a frame carrying fin. */
static void _on_ws_data(const esp_websocket_event_data_t *ev)
{
    if (ev->op_code != WS_OPCODE_TEXT && ev->op_code != WS_OPCODE_CONT) {
        return; /* ping/pong/close are handled by the library */
    }

    bool frame_end = (ev->payload_offset + ev->data_len >= ev->payload_len);

    if (ev->op_code == WS_OPCODE_TEXT && ev->payload_offset == 0) {
        /* Fresh message: recover from any previous drop/partial state. */
        s_rx_len = 0;
        s_rx_drop = false;
    }

    if (!s_rx_drop) {
        if (s_rx_len + (size_t)ev->data_len > HA_WS_RX_BUF_MAX) {
            ESP_LOGW(TAG, "oversized WS message dropped (> %d bytes)", HA_WS_RX_BUF_MAX);
            s_rx_len = 0;
            s_rx_drop = true;
        } else {
            memcpy(s_rx_buf + s_rx_len, ev->data_ptr, (size_t)ev->data_len);
            s_rx_len += (size_t)ev->data_len;
        }
    }

    if (frame_end && ev->fin) {
        if (!s_rx_drop && s_rx_len > 0) {
            _handle_message(s_rx_buf, s_rx_len);
        }
        s_rx_len = 0;
        s_rx_drop = false;
    }
}

static void _ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                              void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *ev = (esp_websocket_event_data_t *)event_data;

    switch ((esp_websocket_event_id_t)event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "transport connected — waiting for auth_required");
            s_msg_id = 1;
            s_rx_len = 0;
            s_rx_drop = false;
            _status_set(HA_WS_STATUS_AUTHENTICATING);
            break;
        case WEBSOCKET_EVENT_DATA:
            _on_ws_data(ev);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(TAG, "%s", event_id == WEBSOCKET_EVENT_ERROR ? "WEBSOCKET_EVENT_ERROR"
                                                                  : "WEBSOCKET_EVENT_DISCONNECTED");
            log_error_if_nonzero("reported from esp-tls", ev->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", ev->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",
                                 ev->error_handle.esp_transport_sock_errno);
            s_rx_len = 0;
            s_rx_drop = false;
            /* The library auto-reconnects; auth+subscribe rerun on CONNECTED.
             * Keep the AUTH_FAILED latch (teardown is already in flight). */
            if (_status_get() == HA_WS_STATUS_AUTHENTICATING ||
                _status_get() == HA_WS_STATUS_SUBSCRIBED) {
                _status_set(HA_WS_STATUS_CONNECTING);
            }
            break;
        default:
            break;
    }
}

/* ── Lifecycle (ha_event_task context only) ───────────────────────────────── */

static void _ws_teardown(void)
{
    if (s_client != NULL) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    /* Safe to release only after the client referencing it was destroyed. */
    if (s_tls_ca != NULL) {
        free(s_tls_ca);
        s_tls_ca = NULL;
    }
    s_rx_len = 0;
    s_rx_drop = false;
}

static void _ws_apply_config(void)
{
    _ws_teardown();

    ha_ws_cfg_get(&s_cfg);
    s_enabled = (s_cfg.enabled != 0);
    _snapshot_sync_cfg();

    if (!s_enabled) {
        ESP_LOGI(TAG, "WS idle: disabled");
        free(s_rx_buf);
        s_rx_buf = NULL;
        _status_set(HA_WS_STATUS_DISABLED);
        return;
    }
    if (!_cfg_is_complete(&s_cfg)) {
        ESP_LOGI(TAG, "WS idle: not configured (need addr, token and an entity — see 'mqtthelp')");
        _status_set(HA_WS_STATUS_UNCONFIGURED);
        return;
    }

    /* Waiting for an IP is not an error: HA_WS_NET_UP retries this once the
     * station is up (same start gate as MQTT, see mqtt.c). */
    _status_set(HA_WS_STATUS_CONNECTING);
    if (!get_mqtt_net_flag()) {
        ESP_LOGI(TAG, "WS waiting for network");
        return;
    }

    if (s_rx_buf == NULL) {
        /* Explicit PSRAM: SPIRAM_MALLOC_ALWAYSINTERNAL would otherwise place
         * this 8 KB buffer in internal RAM. */
        s_rx_buf = heap_caps_malloc(HA_WS_RX_BUF_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_rx_buf == NULL) {
            s_rx_buf = malloc(HA_WS_RX_BUF_MAX);
        }
        if (s_rx_buf == NULL) {
            ESP_LOGE(TAG, "no memory for WS receive buffer");
            return;
        }
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = s_cfg.url,
        .buffer_size = HA_WS_BUFFER_SIZE,
        .task_stack = HA_WS_TASK_STACK,
        .reconnect_timeout_ms = 10000,
        .network_timeout_ms = 10000,
        .ping_interval_sec = 10,
    };

    /* TLS trust wiring — wss:// only; shares the MQTT trust settings
     * (mqtt-tls-mode / mqtt-tls-ca NVS keys) since both target the same HA
     * host in practice. Mirrors _mqtt_ha_start (ha_mqtt.c). */
    bool tls = (strncmp(s_cfg.url, "wss://", 6) == 0);
    ha_tls_mode_t tls_mode = HA_TLS_MODE_VERIFY;
    if (tls) {
        tls_mode = ha_tls_mode_get();
        if (tls_mode == HA_TLS_MODE_INSECURE) {
            ESP_LOGW(TAG, "TLS INSECURE mode: server certificate is NOT verified");
        } else {
            s_tls_ca = ha_tls_ca_load(NULL);
            if (s_tls_ca != NULL) {
                ws_cfg.cert_pem = s_tls_ca;
            } else {
                ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;
            }
        }
    }

    ESP_LOGI(TAG, "| HA WebSocket URL             | %-40s |", s_cfg.url);
    ESP_LOGI(TAG, "| TLS                          | %-40s |",
             !tls                               ? "off (ws://)"
             : tls_mode == HA_TLS_MODE_INSECURE ? "on, INSECURE (no verification)"
             : s_tls_ca                         ? "on, verify: stored CA"
                                                : "on, verify: public CA bundle");
    ESP_LOGI(TAG, "| Token                        | %-40s |", "****");
    for (int i = 0; i < HA_WS_ENTITY_NUM; i++) {
        static const char *slot[HA_WS_ENTITY_NUM] = {"temp", "humidity", "co2"};
        ESP_LOGI(TAG, "| Entity (%-8s)            | %-40s |", slot[i],
                 s_cfg.entity_id[i][0] ? s_cfg.entity_id[i] : "(not set)");
    }

    s_client = esp_websocket_client_init(&ws_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "failed to init WebSocket client");
        return;
    }
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, _ws_event_handler, NULL);
    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start WebSocket client: %s", esp_err_to_name(err));
        _ws_teardown();
        return;
    }
    ESP_LOGI(TAG, "WebSocket client started");
}

static void _lifecycle_event_handler(void *handler_args, esp_event_base_t base, int32_t id,
                                     void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch (id) {
        case HA_WS_CFG_CHANGED:
            /* Also clears an AUTH_FAILED latch: the rebuild re-reads the just
             * saved config and starts from a fresh state. */
            ESP_LOGI(TAG, "event: HA_WS_CFG_CHANGED");
            _ws_apply_config();
            break;
        case HA_WS_NET_UP:
            if (s_client == NULL && s_enabled && _status_get() != HA_WS_STATUS_AUTH_FAILED) {
                _ws_apply_config();
            }
            break;
        case HA_WS_AUTH_FAIL:
            _ws_teardown();
            _status_set(HA_WS_STATUS_AUTH_FAILED);
            ESP_LOGE(TAG, "token rejected by HA — WS stopped; set a valid token with "
                          "'setha -t <token>' (HA Profile -> Security -> Long-lived access tokens)");
            break;
        default:
            break;
    }
}

static void _view_event_handler(void *handler_args, esp_event_base_t base, int32_t id,
                                void *event_data)
{
    (void)handler_args;
    (void)base;

    if (id != VIEW_EVENT_WIFI_ST || event_data == NULL || !s_enabled) {
        return;
    }
    const struct view_data_wifi_st *st = (const struct view_data_wifi_st *)event_data;
    if (st->is_network) {
        /* Cross-loop post from view_event_task; the lifecycle handler ignores
         * it when a client already exists (its own reconnect loop recovers). */
        esp_event_post_to(ha_cfg_event_handle, HA_WS_EVENT_BASE, HA_WS_NET_UP,
                          NULL, 0, portMAX_DELAY);
    }
}

int ha_ws_init(void)
{
    ha_ws_cfg_get(&s_cfg);
    s_enabled = (s_cfg.enabled != 0);
    _snapshot_sync_cfg();
    ha_ws_status_t initial = !s_enabled                  ? HA_WS_STATUS_DISABLED
                             : !_cfg_is_complete(&s_cfg) ? HA_WS_STATUS_UNCONFIGURED
                                                         : HA_WS_STATUS_CONNECTING;
    taskENTER_CRITICAL(&s_snapshot_mux);
    s_snapshot.status = initial;
    taskEXIT_CRITICAL(&s_snapshot_mux);

    /* Lifecycle runs on ha_event_task (loop created by indicator_ha_model_init
     * before this call); the client starts once the station has an IP. */
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        ha_cfg_event_handle, HA_WS_EVENT_BASE, ESP_EVENT_ANY_ID, _lifecycle_event_handler,
        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, _view_event_handler,
        NULL, NULL));
    return 0;
}
