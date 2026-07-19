#include "ha_ws.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ha_dash.h"
#include "ha_mqtt.h"
#include "ha_tls.h"
#include "mqtt.h"
#include "view_data.h"

static const char *TAG = "ha-ws";

ESP_EVENT_DEFINE_BASE(HA_WS_EVENT_BASE);

/* Incoming messages are reassembled here before parsing. subscribe_entities'
 * initial snapshot carries state + full attributes for every subscribed
 * dashboard slot; media players are the fat tail (source_list & friends can
 * run to kilobytes on their own). 32 KB in PSRAM covers a full snapshot with
 * headroom. Larger messages are dropped whole and logged — the next per-entity
 * diff repaints the affected card. */
#define HA_WS_RX_BUF_MAX   32768
/* TX scratch (WS task only: auth + subscribe). The subscribe frame lists every
 * subscribable slot id (64 B each + framing) — 2 KB fits ~28 entities. */
#define HA_WS_TX_BUF_MAX   2048
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

/* Protocol scratch for the WS task's own frames (auth + subscribe). Service
 * calls are framed on ha_event_task into their own buffer (s_call_buf) so the
 * two senders never share bytes; esp_websocket_client_send_text itself is
 * tx-locked, making concurrent sends safe. */
static char s_tx_buf[HA_WS_TX_BUF_MAX];
static char s_call_buf[320]; /* ha_event_task only: id + domain/service/entity/extra */

/* Message-id counter is shared by both senders — allocate under the spinlock. */
static int s_msg_id = 1;
/* The pending/active subscribe_entities command id: only ITS result flips the
 * status to SUBSCRIBED (service-call results would otherwise fake it). */
static int s_subscribe_id = -1;

/* Per-slot media state, merged from snapshot + partial attribute diffs so a
 * consistent whole travels in each VIEW_EVENT_HA_MEDIA. WS task context only;
 * invalidated on (re)connect so the fresh snapshot always reposts. */
typedef struct {
    bool valid;
    char state[16];
    char title[64];
    char artist[48];
} media_cache_t;
static media_cache_t s_media[DASH_SLOT_COUNT];

/* Status snapshot for the settings card, guarded for cross-task reads (LVGL
 * task) against writes from ha_event_task and the WS task. */
static ha_ws_status_snapshot_t s_snapshot;
static portMUX_TYPE            s_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;

/* Handshake watchdog. The first connection after boot has been observed to
 * wedge in AUTHENTICATING with the transport healthy (pings answered) but
 * HA's auth reply never arriving — so neither the pong deadline nor the
 * library's reconnect can save it. If SUBSCRIBED isn't reached within the
 * deadline, hop to ha_event_task and rebuild the client from scratch; the
 * warm retry has always connected in well under a second. */
#define HA_WS_HANDSHAKE_DEADLINE_US (20 * 1000 * 1000)
static esp_timer_handle_t s_handshake_timer = NULL;

static void _handshake_timer_cb(void *arg)
{
    (void)arg;
    esp_event_post_to(ha_cfg_event_handle, HA_WS_EVENT_BASE, HA_WS_STALE_HANDSHAKE,
                      NULL, 0, pdMS_TO_TICKS(100));
}

static void _handshake_watchdog_arm(void)
{
    if (s_handshake_timer != NULL) {
        esp_timer_stop(s_handshake_timer); /* no-op if not running */
        esp_timer_start_once(s_handshake_timer, HA_WS_HANDSHAKE_DEADLINE_US);
    }
}

static void _handshake_watchdog_disarm(void)
{
    if (s_handshake_timer != NULL) {
        esp_timer_stop(s_handshake_timer);
    }
}

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

/* Mirror the url into the snapshot (config mutations happen only in
 * ha_event_task while the client is torn down). */
static void _snapshot_sync_cfg(void)
{
    taskENTER_CRITICAL(&s_snapshot_mux);
    memcpy(s_snapshot.url, s_cfg.url, sizeof(s_snapshot.url));
    taskEXIT_CRITICAL(&s_snapshot_mux);
}

/* Ids are handed out to the WS task (subscribe) and ha_event_task (service
 * calls); reuse the snapshot spinlock rather than growing the lock zoo. */
static int _next_msg_id(void)
{
    taskENTER_CRITICAL(&s_snapshot_mux);
    int id = s_msg_id++;
    taskEXIT_CRITICAL(&s_snapshot_mux);
    return id;
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

/* Entities come from the compile-time dashboard table now; the runtime config
 * only has to provide the connection (the legacy per-slot entity ids in
 * ha_ws_cfg_t stay dormant to preserve the NVS blob layout). */
static bool _cfg_is_complete(const ha_ws_cfg_t *cfg)
{
    return cfg->url[0] != '\0' && cfg->token[0] != '\0';
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

/* Legacy display feed (index 0=temp 1=humidity 2=co2): keeps the history ring
 * and the Trends chart running unchanged next to the dashboard events. */
static void _post_legacy_value(int index, const char *value)
{
    struct view_data_ha_sensor_data data = {.index = (uint8_t)index};
    strncpy(data.value, value, sizeof(data.value) - 1);
    data.value[sizeof(data.value) - 1] = '\0';

    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SENSOR,
                                      &data, sizeof(data), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drop VIEW_EVENT_HA_SENSOR: %s", esp_err_to_name(err));
    }
}

static void _post_entity(const struct view_data_ha_entity *data)
{
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ENTITY,
                                      data, sizeof(*data), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drop VIEW_EVENT_HA_ENTITY: %s", esp_err_to_name(err));
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

/* Subscribe to every subscribable dashboard slot (ACTION slots are commands,
 * not state). Duplicate entity ids across slots are sent once — the routing
 * side fans one update out to every matching slot. */
static void _send_subscribe(void)
{
    int id = _next_msg_id();
    int len = snprintf(s_tx_buf, sizeof(s_tx_buf),
                       "{\"id\":%d,\"type\":\"subscribe_entities\",\"entity_ids\":[", id);
    bool first = true;
    for (int i = 0; i < DASH_SLOT_COUNT && len < (int)sizeof(s_tx_buf); i++) {
        if (!dash_slot_subscribable(i)) {
            continue;
        }
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (dash_slot_subscribable(j) &&
                strcmp(dash_slots[j].entity_id, dash_slots[i].entity_id) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        len += snprintf(s_tx_buf + len, sizeof(s_tx_buf) - len, "%s\"%s\"",
                        first ? "" : ",", dash_slots[i].entity_id);
        first = false;
    }
    if (len < (int)sizeof(s_tx_buf)) {
        len += snprintf(s_tx_buf + len, sizeof(s_tx_buf) - len, "]}");
    }
    if (len >= (int)sizeof(s_tx_buf)) {
        ESP_LOGE(TAG, "subscribe frame overflow — trim DASH_SLOT_LIST or grow HA_WS_TX_BUF_MAX");
        return;
    }
    s_subscribe_id = id;
    _ws_send(s_tx_buf, len);
}

/* ── Incoming message handling (WS task context) ──────────────────────────── */

/* Copy a JSON string value; absent/non-string clears the destination. */
static void _copy_json_string(char *dst, size_t dst_size, const cJSON *item)
{
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(dst, item->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}

/* Merge one media_player update into the per-slot cache and repost on change.
 * Snapshot adds are authoritative (absent attribute = cleared); diffs only
 * carry changed keys, and removed attributes arrive in minus["a"]. */
static void _media_merge(int slot, const cJSON *state, const cJSON *attrs,
                         const cJSON *minus, bool diff)
{
    media_cache_t next = s_media[slot];
    if (!diff) {
        memset(&next, 0, sizeof(next));
    }

    if (state != NULL) {
        _copy_json_string(next.state, sizeof(next.state), state);
    }
    if (attrs != NULL) {
        const cJSON *title = cJSON_GetObjectItem(attrs, "media_title");
        if (title != NULL || !diff) {
            _copy_json_string(next.title, sizeof(next.title), title);
        }
        const cJSON *artist = cJSON_GetObjectItem(attrs, "media_artist");
        if (artist != NULL || !diff) {
            _copy_json_string(next.artist, sizeof(next.artist), artist);
        }
    }
    if (minus != NULL) {
        const cJSON *gone = cJSON_GetObjectItem(minus, "a");
        const cJSON *key;
        cJSON_ArrayForEach(key, gone) {
            if (!cJSON_IsString(key) || key->valuestring == NULL) {
                continue;
            }
            if (strcmp(key->valuestring, "media_title") == 0) {
                next.title[0] = '\0';
            } else if (strcmp(key->valuestring, "media_artist") == 0) {
                next.artist[0] = '\0';
            }
        }
    }
    next.valid = true;

    /* Some integrations diff media_position every few seconds; only a change
     * in what we actually render is worth a repost. */
    if (s_media[slot].valid && memcmp(&next, &s_media[slot], sizeof(next)) == 0) {
        return;
    }
    s_media[slot] = next;

    struct view_data_ha_media data = {.slot = (uint8_t)slot};
    memcpy(data.state, next.state, sizeof(data.state));
    memcpy(data.title, next.title, sizeof(data.title));
    memcpy(data.artist, next.artist, sizeof(data.artist));
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_MEDIA,
                                      &data, sizeof(data), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drop VIEW_EVENT_HA_MEDIA: %s", esp_err_to_name(err));
    }
}

/* Route one entity's snapshot entry ("a") or diff ("+"/"-") to every matching
 * dashboard slot (duplicate ids across slots are legal — subscribe dedupes,
 * routing fans out). */
static void _apply_entity(const char *entity_id, const cJSON *plus, const cJSON *minus,
                          bool diff)
{
    const cJSON *state = cJSON_GetObjectItem(plus, "s"); /* NULL on attr-only diff */
    const cJSON *attrs = cJSON_GetObjectItem(plus, "a"); /* full on add, partial on diff */

    for (int slot = 0; slot < DASH_SLOT_COUNT; slot++) {
        const dash_slot_t *def = &dash_slots[slot];
        if (def->kind == DASH_KIND_ACTION || strcmp(def->entity_id, entity_id) != 0) {
            continue;
        }

        if (def->kind == DASH_KIND_MEDIA) {
            _media_merge(slot, state, attrs, minus, diff);
            continue;
        }

        struct view_data_ha_entity data = {.slot = (uint8_t)slot, .brightness = -1};

        if (def->kind == DASH_KIND_SENSOR) {
            if (state == NULL) {
                continue; /* attribute-only diff — nothing rendered changes */
            }
            _format_state_value(state, data.state, sizeof(data.state));
            if (data.state[0] == '\0') {
                continue;
            }
            if (def->legacy >= 0) {
                _post_legacy_value(def->legacy, data.state);
            }
        } else { /* TOGGLE / LIGHT: raw state string ("on"/"off"/"unavailable") */
            if (cJSON_IsString(state) && state->valuestring != NULL) {
                strncpy(data.state, state->valuestring, sizeof(data.state) - 1);
            }
            if (def->kind == DASH_KIND_LIGHT && attrs != NULL) {
                const cJSON *brightness = cJSON_GetObjectItem(attrs, "brightness");
                if (cJSON_IsNumber(brightness)) {
                    data.brightness = (int16_t)brightness->valuedouble;
                }
            }
            if (data.state[0] == '\0' && data.brightness < 0) {
                continue; /* diff carried neither state nor brightness */
            }
        }

        ESP_LOGD(TAG, "%s -> slot %d (state '%s' br %d)", entity_id, slot, data.state,
                 data.brightness);
        _post_entity(&data);
    }
}

/* subscribe_entities event payload (compressed-state format):
 *   {"a": {"<entity>": {"s": "72.4", "a": {attrs}, ...}}}   initial snapshot
 *   {"c": {"<entity>": {"+": {"s": "72.6", "a": {changed}},
 *                       "-": {"a": ["removed_attr"]}}}}     diff ("s"/"a"
 *                                                           absent = unchanged)
 *   {"r": ["<entity>"]}                                     entity removed    */
static void _handle_entities_event(const cJSON *event)
{
    const cJSON *add = cJSON_GetObjectItem(event, "a");
    const cJSON *chg = cJSON_GetObjectItem(event, "c");
    const cJSON *rem = cJSON_GetObjectItem(event, "r");
    const cJSON *ent = NULL;

    cJSON_ArrayForEach(ent, add) {
        if (ent->string != NULL) {
            _apply_entity(ent->string, ent, NULL, false);
        }
    }
    cJSON_ArrayForEach(ent, chg) {
        if (ent->string == NULL) {
            continue;
        }
        const cJSON *plus  = cJSON_GetObjectItem(ent, "+");
        const cJSON *minus = cJSON_GetObjectItem(ent, "-");
        if (plus != NULL || minus != NULL) {
            _apply_entity(ent->string, plus, minus, true);
        }
    }

    if (cJSON_IsArray(rem) && cJSON_GetArraySize(rem) > 0) {
        ESP_LOGW(TAG,
                 "HA removed %d subscribed entit(y/ies) — check entity ids in "
                 "dashboard_config.h",
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
        const cJSON *id_item = cJSON_GetObjectItem(root, "id");
        int  id      = cJSON_IsNumber(id_item) ? (int)id_item->valuedouble : -1;
        bool success = cJSON_IsTrue(cJSON_GetObjectItem(root, "success"));

        if (id == s_subscribe_id) {
            if (success) {
                ESP_LOGI(TAG, "subscribed — live entity updates streaming");
                _handshake_watchdog_disarm();
                _status_set(HA_WS_STATUS_SUBSCRIBED);
            } else {
                /* Healthy connection, bad command (e.g. malformed entity id).
                 * Stay idle instead of hammering HA with retries. */
                char *err = cJSON_PrintUnformatted(cJSON_GetObjectItem(root, "error"));
                ESP_LOGE(TAG, "subscribe rejected: %s — fix entity ids in dashboard_config.h",
                         err ? err : "(unknown)");
                free(err);
            }
        } else if (!success) {
            /* Service-call rejections are log-only: the optimistic UI is
             * reconciled by the (absent) state echo, and HA's error message
             * names the offending entity/service. */
            char *err = cJSON_PrintUnformatted(cJSON_GetObjectItem(root, "error"));
            ESP_LOGE(TAG, "service call (id %d) rejected: %s", id, err ? err : "(unknown)");
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
            taskENTER_CRITICAL(&s_snapshot_mux);
            s_msg_id = 1;
            taskEXIT_CRITICAL(&s_snapshot_mux);
            s_subscribe_id = -1;
            s_rx_len = 0;
            s_rx_drop = false;
            /* Fresh subscribe = fresh snapshot: invalidate the media cache so
             * an identical post-reconnect state still reposts to the UI. */
            memset(s_media, 0, sizeof(s_media));
            _handshake_watchdog_arm();
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
    _handshake_watchdog_disarm();
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
        /* Without a pong deadline a half-open link (peer gone, no FIN seen)
         * wedges forever in AUTHENTICATING: pings keep flowing into the void
         * and no DISCONNECTED event ever fires. Two missed pongs = teardown,
         * which hands recovery to the library's auto-reconnect. */
        .pingpong_timeout_sec = 20,
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
    int subscribable = 0;
    for (int i = 0; i < DASH_SLOT_COUNT; i++) {
        if (dash_slot_subscribable(i)) {
            subscribable++;
        }
    }
    char entities_row[48];
    snprintf(entities_row, sizeof(entities_row), "%d dashboard slots (compile-time)",
             subscribable);
    ESP_LOGI(TAG, "| Entities                     | %-40s |", entities_row);

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

/* Frame + send one queued service call. ha_event_task context: the client
 * can't be torn down underneath us (teardown runs on this same task), and
 * s_call_buf is this task's private scratch. */
static void _send_service_call(const ha_ws_call_req_t *req)
{
    if (s_client == NULL || _status_get() != HA_WS_STATUS_SUBSCRIBED) {
        ESP_LOGW(TAG, "drop service call %s.%s (%s): not subscribed",
                 req->domain, req->service, req->entity_id);
        return;
    }

    int id  = _next_msg_id();
    int len = snprintf(s_call_buf, sizeof(s_call_buf),
                       "{\"id\":%d,\"type\":\"call_service\",\"domain\":\"%s\","
                       "\"service\":\"%s\",\"service_data\":%s,"
                       "\"target\":{\"entity_id\":\"%s\"}}",
                       id, req->domain, req->service,
                       req->extra[0] != '\0' ? req->extra : "{}", req->entity_id);
    if (len <= 0 || len >= (int)sizeof(s_call_buf)) {
        ESP_LOGE(TAG, "service call frame overflow (%s.%s)", req->domain, req->service);
        return;
    }

    if (esp_websocket_client_send_text(s_client, s_call_buf, len, pdMS_TO_TICKS(2000)) < 0) {
        ESP_LOGW(TAG, "service call send failed (%s.%s)", req->domain, req->service);
    } else {
        ESP_LOGI(TAG, "call_service id=%d %s.%s -> %s", id, req->domain, req->service,
                 req->entity_id);
    }
}

esp_err_t ha_ws_call(const char *domain, const char *service,
                     const char *entity_id, const char *extra)
{
    if (domain == NULL || service == NULL || entity_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ha_ws_call_req_t req = {0};
    snprintf(req.domain, sizeof(req.domain), "%s", domain);
    snprintf(req.service, sizeof(req.service), "%s", service);
    snprintf(req.entity_id, sizeof(req.entity_id), "%s", entity_id);
    if (extra != NULL) {
        snprintf(req.extra, sizeof(req.extra), "%s", extra);
    }

    /* Bounded post from whatever task the caller runs in (LVGL input included):
     * a full queue drops the tap rather than blocking the UI. */
    esp_err_t err = esp_event_post_to(ha_cfg_event_handle, HA_WS_EVENT_BASE, HA_WS_TX_CALL,
                                      &req, sizeof(req), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drop service call %s.%s: %s", domain, service, esp_err_to_name(err));
    }
    return err;
}

static void _lifecycle_event_handler(void *handler_args, esp_event_base_t base, int32_t id,
                                     void *event_data)
{
    (void)handler_args;
    (void)base;

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
        case HA_WS_TX_CALL:
            if (event_data != NULL) {
                _send_service_call((const ha_ws_call_req_t *)event_data);
            }
            break;
        case HA_WS_STALE_HANDSHAKE:
            if (s_client != NULL && s_enabled && _status_get() != HA_WS_STATUS_SUBSCRIBED) {
                ESP_LOGW(TAG, "handshake stalled (not subscribed after %d s) — rebuilding client",
                         (int)(HA_WS_HANDSHAKE_DEADLINE_US / 1000000));
                _ws_apply_config();
            }
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

    const esp_timer_create_args_t watchdog_args = {
        .callback = _handshake_timer_cb,
        .name = "ha-ws-handshake",
    };
    ESP_ERROR_CHECK(esp_timer_create(&watchdog_args, &s_handshake_timer));

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
