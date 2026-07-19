#ifndef DASHBOARD_CONFIG_H
#define DASHBOARD_CONFIG_H

/*
 * Compile-time dashboard definition: the rooms and Home Assistant entities the
 * panel renders. This is the ONE file to edit when the home changes — the
 * registry (ha/ha_dash.c), the WebSocket subscription (ha/ha_ws.c) and both
 * dashboard screens are generated from these X-macro tables.
 *
 * >>> ENTITY IDS BELOW ARE PLACEHOLDERS <<<
 * Replace each entity_id with the real one from your Home Assistant instance
 * (Settings -> Devices & Services -> Entities), then rebuild + flash. Ids must
 * be `domain.object_id` using only [a-z0-9_.-] — they are embedded verbatim in
 * WebSocket JSON frames (that charset needs no escaping).
 *
 * Icons come from the generated MDI subset (main/ui/ui_icons.h). To use a new
 * icon, add it to scripts/gen_mdi_font.py and regenerate the fonts.
 */

#include "ui_icons.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tile/page ids. Page 0 is Home; each room is one swipeable page after it
 * (nav tile = page index). Trends stays a separate nav tile after the rooms. */
enum dash_page {
    DASH_PAGE_HOME = 0,
    DASH_PAGE_LOFT,
    DASH_PAGE_GUEST,
    DASH_PAGE_LIVING,
    DASH_PAGE_HALLWAY,
    DASH_PAGE_COUNT,
};

/* Rooms: the Home-screen 2x2 card grid + one page each, in this order.
 * X(sym, name, icon, accent_hex) */
#define DASH_ROOM_LIST \
    X(DASH_ROOM_LOFT,    "Loft",            UI_ICON_HOME_ROOF, UI_HEX_CORAL) \
    X(DASH_ROOM_GUEST,   "Guest Room",      UI_ICON_BED,       UI_HEX_BLUE)  \
    X(DASH_ROOM_LIVING,  "Living Room",     UI_ICON_SOFA,      UI_HEX_GREEN) \
    X(DASH_ROOM_HALLWAY, "Hallway & Other", UI_ICON_IMAGE,     UI_HEX_AMBER)

/* Slot flags. */
#define DASH_F_CONFIRM    (1u << 0) /* ACTION: msgbox confirm before the call  */
#define DASH_F_BRIGHTNESS (1u << 1) /* LIGHT: render a brightness slider       */
#define DASH_F_ROOM_TEMP  (1u << 2) /* SENSOR: mirrored on the Home room card  */

/* Entity slots.
 * X(sym, page, kind, entity_id, label, icon, accent_hex, unit, legacy, flags)
 *
 * kind   SENSOR | TOGGLE | LIGHT | ACTION | MEDIA  (-> DASH_KIND_*)
 *        ACTION slots are scripts/scenes: fire-and-forget, never subscribed.
 * legacy -1, or the legacy display index 0=temp 1=humidity 2=co2 — those
 *        values additionally feed VIEW_EVENT_HA_SENSOR so the history ring
 *        and the Trends chart keep working unchanged.
 */
#define DASH_SLOT_LIST \
    /* ── Home quick actions ─────────────────────────────────────────────── */ \
    X(SLOT_ALL_OFF,       DASH_PAGE_HOME,    ACTION, "script.all_lights_off",        "All Lights Off", UI_ICON_LIGHT_GROUP_OFF, UI_HEX_RED,     "",    -1, DASH_F_CONFIRM)    \
    X(SLOT_XMAS_PORCH,    DASH_PAGE_HOME,    TOGGLE, "switch.christmas_porch",       "Xmas Porch",     UI_ICON_STRING_LIGHTS,   UI_HEX_AMBER,   "",    -1, 0)                 \
    X(SLOT_XMAS_HALL,     DASH_PAGE_HOME,    TOGGLE, "switch.christmas_hall",        "Xmas Hall",      UI_ICON_STRING_LIGHTS,   UI_HEX_AMBER,   "",    -1, 0)                 \
    /* ── Loft ───────────────────────────────────────────────────────────── */ \
    X(SLOT_LOFT_TEMP,     DASH_PAGE_LOFT,    SENSOR, "sensor.loft_temperature",      "Temperature",    UI_ICON_THERMOMETER,     UI_HEX_AMBER,   "°F",   0, DASH_F_ROOM_TEMP)  \
    X(SLOT_LOFT_HUM,      DASH_PAGE_LOFT,    SENSOR, "sensor.loft_humidity",         "Humidity",       UI_ICON_WATER_PCT,       UI_HEX_BLUE,    "%",    1, 0)                 \
    X(SLOT_LOFT_CO2,      DASH_PAGE_LOFT,    SENSOR, "sensor.loft_co2",              "CO2",            UI_ICON_MOLECULE_CO2,    UI_HEX_TEXT,    "ppm",  2, 0)                 \
    X(SLOT_LOFT_LED,      DASH_PAGE_LOFT,    LIGHT,  "light.loft_led_strip",         "LED Strip",      UI_ICON_LED_STRIP,       UI_HEX_GREEN,   "",    -1, DASH_F_BRIGHTNESS) \
    /* ── Guest Room ─────────────────────────────────────────────────────── */ \
    X(SLOT_GUEST_TEMP,    DASH_PAGE_GUEST,   SENSOR, "sensor.guest_temperature",     "Temperature",    UI_ICON_THERMOMETER,     UI_HEX_AMBER,   "°F",  -1, DASH_F_ROOM_TEMP)  \
    /* ── Living Room ────────────────────────────────────────────────────── */ \
    X(SLOT_LIVING_TEMP,   DASH_PAGE_LIVING,  SENSOR, "sensor.living_temperature",    "Temperature",    UI_ICON_THERMOMETER,     UI_HEX_AMBER,   "°F",  -1, DASH_F_ROOM_TEMP)  \
    X(SLOT_LIVING_MEDIA,  DASH_PAGE_LIVING,  MEDIA,  "media_player.living_room",     "Living Room",    UI_ICON_SPEAKER,         UI_HEX_PRIMARY, "",    -1, 0)                 \
    X(SLOT_PRESET_CHILL,  DASH_PAGE_LIVING,  ACTION, "script.playlist_chill",        "Chill",          UI_ICON_SNOWFLAKE,       UI_HEX_BLUE,    "",    -1, 0)                 \
    X(SLOT_PRESET_OLDIES, DASH_PAGE_LIVING,  ACTION, "script.playlist_oldies",       "Oldies",         UI_ICON_ALBUM,           UI_HEX_AMBER,   "",    -1, 0)                 \
    X(SLOT_PRESET_HIPHOP, DASH_PAGE_LIVING,  ACTION, "script.playlist_90s_hiphop",   "90s Hiphop",     UI_ICON_MIC,             UI_HEX_GREEN,   "",    -1, 0)                 \
    /* ── Hallway & Other ────────────────────────────────────────────────── */ \
    X(SLOT_HALL_TEMP,     DASH_PAGE_HALLWAY, SENSOR, "sensor.hallway_temperature",   "Temperature",    UI_ICON_THERMOMETER,     UI_HEX_AMBER,   "°F",  -1, DASH_F_ROOM_TEMP)  \
    X(SLOT_PROXMOX,       DASH_PAGE_HALLWAY, TOGGLE, "switch.proxmox",               "Proxmox",        UI_ICON_SERVER,          UI_HEX_PRIMARY, "",    -1, 0)

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* DASHBOARD_CONFIG_H */
