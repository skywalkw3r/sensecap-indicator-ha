#include "ha_dash.h"

#include <string.h>

#include "esp_event.h"
#include "freertos/FreeRTOS.h"

#include "ha_ws.h"
#include "view_data.h"

/* Registry arrays expanded from dashboard_config.h. */

const dash_slot_t dash_slots[DASH_SLOT_COUNT] = {
#define X(sym, page_, kind_, entity_, label_, icon_, accent_, unit_, legacy_, flags_) \
    [sym] = {.entity_id = entity_,                                                     \
             .label     = label_,                                                      \
             .icon      = icon_,                                                       \
             .unit      = unit_,                                                       \
             .accent    = accent_,                                                     \
             .page      = page_,                                                       \
             .kind      = DASH_KIND_##kind_,                                           \
             .legacy    = legacy_,                                                     \
             .flags     = flags_},
    DASH_SLOT_LIST
#undef X
};

const dash_room_t dash_rooms[DASH_ROOM_COUNT] = {
#define X(sym, name_, icon_, accent_) [sym] = {.name = name_, .icon = icon_, .accent = accent_},
    DASH_ROOM_LIST
#undef X
};

/* Home + one page per room; nav additionally appends the Trends tile. */
_Static_assert(DASH_PAGE_COUNT == DASH_ROOM_COUNT + 1,
               "dash_page enum out of sync with DASH_ROOM_LIST");

int dash_slot_by_entity(const char *entity_id)
{
    if (entity_id == NULL) {
        return -1;
    }
    for (int i = 0; i < DASH_SLOT_COUNT; i++) {
        if (strcmp(dash_slots[i].entity_id, entity_id) == 0) {
            return i;
        }
    }
    return -1;
}

bool dash_slot_subscribable(int slot)
{
    return slot >= 0 && slot < DASH_SLOT_COUNT && dash_slots[slot].kind != DASH_KIND_ACTION;
}

int dash_room_temp_slot(int room)
{
    if (room < 0 || room >= DASH_ROOM_COUNT) {
        return -1;
    }
    uint8_t page = (uint8_t)(DASH_PAGE_HOME + 1 + room); /* rooms follow Home */
    for (int i = 0; i < DASH_SLOT_COUNT; i++) {
        if (dash_slots[i].page == page && (dash_slots[i].flags & DASH_F_ROOM_TEMP)) {
            return i;
        }
    }
    return -1;
}

void dash_action_call(int slot)
{
    if (slot < 0 || slot >= DASH_SLOT_COUNT || dash_slots[slot].kind != DASH_KIND_ACTION) {
        return;
    }
    const char *id  = dash_slots[slot].entity_id;
    const char *dot = strchr(id, '.');

    char   domain[24] = "script";
    size_t dlen       = (dot != NULL) ? (size_t)(dot - id) : 0;
    if (dlen > 0 && dlen < sizeof(domain)) {
        memcpy(domain, id, dlen);
        domain[dlen] = '\0';
    }
    /* automation.trigger runs the automation's actions now; turn_on would only
     * (re-)enable it. Everything else (script, scene, ...) runs via turn_on. */
    const char *service = (strcmp(domain, "automation") == 0) ? "trigger" : "turn_on";
    ha_ws_call(domain, service, id, NULL);
}

/* ── MQTT-mode legacy bridge ──────────────────────────────────────────────
 *
 * With the WebSocket client disabled, HA can still push the three Loft display
 * values over MQTT (indicator/display/set -> ha_sensor.c -> VIEW_EVENT_HA_SENSOR
 * by legacy index). Dashboard screens consume only VIEW_EVENT_HA_ENTITY, so this
 * bridge re-posts those values onto the slot whose `legacy` field matches —
 * the panel degrades to read-only Loft values instead of going dark.
 *
 * In WS mode ha_ws.c posts VIEW_EVENT_HA_ENTITY itself (and HA_SENSOR for the
 * legacy slots, feeding the history ring); the bridge stays silent to avoid
 * double-painting. Runs in view_event_task; the bounded re-post may drop under
 * pressure, which the next value refresh repairs. */
static void _legacy_bridge(void *handler_args, esp_event_base_t base, int32_t id,
                           void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)id;

    if (event_data == NULL || ha_ws_is_enabled()) {
        return;
    }
    const struct view_data_ha_sensor_data *in = (const struct view_data_ha_sensor_data *)event_data;

    for (int i = 0; i < DASH_SLOT_COUNT; i++) {
        if (dash_slots[i].legacy != (int8_t)in->index) {
            continue;
        }
        struct view_data_ha_entity out = {.slot = (uint8_t)i, .brightness = -1};
        strncpy(out.state, in->value, sizeof(out.state) - 1);
        out.state[sizeof(out.state) - 1] = '\0';
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ENTITY,
                          &out, sizeof(out), pdMS_TO_TICKS(100));
    }
}

void ha_dash_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SENSOR, _legacy_bridge, NULL, NULL));
}
