#include "ha_ws_status_screen.h"

#include <stdio.h>

#include "esp_log.h"
#include "ha_config.h"
#include "ha_ws.h"
#include "lv_port.h"
#include "ui_components.h"
#include "ui_theme.h"
#include "view_data.h"

static const char *TAG = "ha-ws-screen";

static lv_obj_t *s_modal = NULL;
static lv_obj_t *s_status_value = NULL;
static lv_obj_t *s_mqtt_value = NULL;
static lv_obj_t *s_url_value = NULL;
static lv_obj_t *s_entity_value[HA_WS_ENTITY_NUM] = {NULL, NULL, NULL};

/* View-local mirror of the model's status meaning: text + accent per state.
 * Values follow ha_ws_status_t (ha_ws.h). */
static const char *_status_text(ha_ws_status_t status)
{
    /* ASCII only: montserrat_20 has no em-dash glyph. */
    switch (status) {
        case HA_WS_STATUS_DISABLED:       return "Disabled (enable via 'setha')";
        case HA_WS_STATUS_UNCONFIGURED:   return "Not configured (see 'mqtthelp')";
        case HA_WS_STATUS_CONNECTING:     return "Connecting...";
        case HA_WS_STATUS_AUTHENTICATING: return "Authenticating...";
        case HA_WS_STATUS_SUBSCRIBED:     return "Connected, live";
        case HA_WS_STATUS_AUTH_FAILED:    return "Auth failed, check token";
        default:                          return "Unknown";
    }
}

static lv_color_t _status_color(ha_ws_status_t status)
{
    switch (status) {
        case HA_WS_STATUS_SUBSCRIBED:     return UI_COLOR_GREEN;
        case HA_WS_STATUS_CONNECTING:
        case HA_WS_STATUS_AUTHENTICATING: return UI_COLOR_AMBER;
        case HA_WS_STATUS_AUTH_FAILED:    return UI_COLOR_RED;
        default:                          return UI_COLOR_TEXT_MUTED;
    }
}

static void _refresh(void)
{
    if (!s_modal) {
        return;
    }

    ha_ws_status_snapshot_t snap;
    ha_ws_status_get(&snap);

    lv_label_set_text(s_status_value, _status_text(snap.status));
    lv_obj_set_style_text_color(s_status_value, _status_color(snap.status),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    /* The two transports run one-at-a-time; show the MQTT side so a paused
     * switch page is explained where the user will look first. */
    bool mqtt_on = ha_mqtt_enabled_get();
    lv_label_set_text(s_mqtt_value, mqtt_on ? "Enabled" : "Disabled (WebSocket active)");
    lv_obj_set_style_text_color(s_mqtt_value, mqtt_on ? UI_COLOR_GREEN : UI_COLOR_TEXT_MUTED,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_url_value, snap.url[0] ? snap.url : "(not set)");
    for (int i = 0; i < HA_WS_ENTITY_NUM; i++) {
        lv_label_set_text(s_entity_value[i],
                          snap.entity_id[i][0] ? snap.entity_id[i] : "(not set)");
    }
}

static void _on_back(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && s_modal) {
        ui_modal_anim_out(s_modal);
    }
}

/* One "label above value" block; returns the value label for refresh. */
static lv_obj_t *_add_row(lv_obj_t *parent, const char *title)
{
    lv_obj_t *title_label = ui_label(parent, title, UI_FONT_BODY, UI_COLOR_TEXT_MUTED);
    (void)title_label;
    lv_obj_t *value = ui_label(parent, "-", UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_width(value, LV_PCT(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    return value;
}

static void _ensure_modal(void)
{
    if (s_modal) {
        return;
    }

    s_modal = ui_modal_create();
    ui_modal_header(s_modal, "Home Assistant", _on_back, NULL);

    /* 480x480 panel: single content-sized card; the console hint flows as the
     * last flex row so nothing can overlap when entity ids wrap. */
    lv_obj_t *card = lv_obj_create(s_modal);
    lv_obj_set_size(card, 440, LV_SIZE_CONTENT);
    lv_obj_set_align(card, LV_ALIGN_TOP_MID);
    lv_obj_set_y(card, 95);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    ui_apply_card(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(card, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_status_value = _add_row(card, "WebSocket status");
    s_mqtt_value = _add_row(card, "MQTT client");
    s_url_value = _add_row(card, "Server");
    s_entity_value[0] = _add_row(card, "Temperature entity");
    s_entity_value[1] = _add_row(card, "Humidity entity");
    s_entity_value[2] = _add_row(card, "CO2 entity");

    lv_obj_t *hint = ui_label(card, "Console setup: 'setha' (see 'mqtthelp')",
                              UI_FONT_BODY, UI_COLOR_TEXT_MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(hint, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void _show(void)
{
    _ensure_modal();
    _refresh();
    lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_modal);
    ui_modal_anim_in(s_modal);
}

static void view_event_handler(void *handler_args, esp_event_base_t base, int32_t id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;

    lv_port_sem_take();
    switch (id) {
        case VIEW_EVENT_SCREEN_START: {
            if (event_data && *(uint8_t *)event_data == SCREEN_HA_WS_STATUS) {
                _show();
            }
            break;
        }
        case VIEW_EVENT_HA_WS_STATUS:
            /* Payload only says "changed"; repaint from the full snapshot.
             * Cheap, and a no-op while the modal has never been opened. */
            _refresh();
            break;
        default:
            ESP_LOGW(TAG, "Unhandled event: %ld", (long)id);
            break;
    }
    lv_port_sem_give();
}

void ha_ws_status_screen_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, view_event_handler,
        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_WS_STATUS, view_event_handler,
        NULL, NULL));
}
