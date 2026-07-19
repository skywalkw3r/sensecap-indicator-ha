#ifndef HA_DASH_H
#define HA_DASH_H

/*
 * Dashboard registry: const room/slot tables expanded from the X-macro lists
 * in dashboard_config.h, plus the lookups shared by the WebSocket client and
 * the dashboard screens.
 *
 * Model-side file — data and esp_event plumbing only, no LVGL. The `icon`
 * strings are UTF-8 glyph literals (ui_icons.h) and `accent` is a raw 24-bit
 * hex; only view code turns them into fonts/colours.
 */

#include <stdbool.h>
#include <stdint.h>

#include "dashboard_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DASH_KIND_SENSOR, /* read-only value (temperature, humidity, ...) */
    DASH_KIND_TOGGLE, /* on/off entity: switch, input_boolean, plain light */
    DASH_KIND_LIGHT,  /* light with extras (DASH_F_BRIGHTNESS slider) */
    DASH_KIND_ACTION, /* script/scene: fire-and-forget, not subscribed */
    DASH_KIND_MEDIA,  /* media_player: state + title/artist + play-pause */
} dash_kind_t;

typedef struct {
    const char *entity_id;
    const char *label;
    const char *action_entity; /* "" = none. SENSOR rows with an action become
                                * pressable and fire it (e.g. a server status
                                * row whose tap runs the power-toggle script). */
    const char *icon;   /* UI_ICON_* UTF-8 glyph (render with ui_font_mdi_*) */
    const char *unit;   /* value suffix for SENSOR slots ("°F", "%", ...) */
    uint32_t    accent; /* UI_HEX_* raw 24-bit colour */
    uint8_t     page;   /* enum dash_page */
    uint8_t     kind;   /* dash_kind_t */
    int8_t      legacy; /* -1, or legacy display index 0=temp 1=hum 2=co2 */
    uint8_t     flags;  /* DASH_F_* */
} dash_slot_t;

typedef struct {
    const char *name;
    const char *icon;
    uint32_t    accent;
} dash_room_t;

/* Slot / room ids straight from the config tables (SLOT_*, DASH_ROOM_*). */
enum {
#define X(sym, page, kind, entity, label, icon, accent, unit, legacy, flags, action) sym,
    DASH_SLOT_LIST
#undef X
        DASH_SLOT_COUNT
};

enum {
#define X(sym, name, icon, accent) sym,
    DASH_ROOM_LIST
#undef X
        DASH_ROOM_COUNT
};

extern const dash_slot_t dash_slots[DASH_SLOT_COUNT];
extern const dash_room_t dash_rooms[DASH_ROOM_COUNT];

/* Slot index for an incoming entity id; -1 when the id isn't on the dashboard. */
int dash_slot_by_entity(const char *entity_id);

/* ACTION slots are commands, not state — everything else gets subscribed. */
bool dash_slot_subscribable(int slot);

/* The DASH_F_ROOM_TEMP sensor shown on `room`'s Home card; -1 if none. */
int dash_room_temp_slot(int room);

/* Fire a slot's action with the domain-appropriate service: automations are
 * *triggered* (turn_on would merely re-enable them); scripts/scenes run via
 * turn_on. ACTION slots fire their entity_id; other kinds fire their
 * action_entity (no-op when empty). Safe from any task (queues through
 * ha_ws_call). */
void dash_action_call(int slot);

/* Register the MQTT-mode bridge (see ha_dash.c). Called from
 * indicator_ha_model_init() after the view event loop exists. */
void ha_dash_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_DASH_H */
