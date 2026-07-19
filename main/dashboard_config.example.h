#ifndef DASHBOARD_CONFIG_H
#define DASHBOARD_CONFIG_H

/*
 * Compile-time dashboard definition: the rooms and Home Assistant entities the
 * panel renders. The registry (ha/ha_dash.c), the WebSocket subscription
 * (ha/ha_ws.c) and both dashboard screens are generated from these X-macro
 * tables.
 *
 * >>> THIS IS THE TEMPLATE <<<
 * The real config lives in main/dashboard_config.h, which is .gitignore'd so
 * your home's entity ids never land in the repo. Both builds copy this file
 * to main/dashboard_config.h automatically on first configure; edit that copy
 * with your entity ids (HA -> Settings -> Devices & Services -> Entities),
 * then rebuild + flash. Ids must be `domain.object_id` using only [a-z0-9_.-]
 * — they are embedded verbatim in WebSocket JSON frames.
 *
 * Notes mirrored from the reference layout:
 *  - Slots may share an entity id (e.g. two rooms showing one temperature
 *    sensor); subscribe dedupes the id, routing fans updates out to every
 *    matching slot.
 *  - A TOGGLE/LIGHT id may be a comma-separated group ("light.a,light.b",
 *    no spaces): the toggle and slider drive every member in one service
 *    call and the row's state follows the most recently updated member.
 *    Keep the list under the 96-byte ha_ws_call_req_t entity_id budget;
 *    SENSOR/MEDIA slots stay single-entity.
 *  - A server with no switch entity: give its binary_sensor SENSOR row an
 *    action entity — the row shows live on/off and taps run the toggle
 *    script (confirm-gated). One card, two entities (Hallway page).
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
    DASH_PAGE_OFFICE,
    DASH_PAGE_LIVING,
    DASH_PAGE_HALLWAY,
    DASH_PAGE_COUNT,
};

/* Rooms: the Home-screen 2x2 card grid + one page each, in this order.
 * X(sym, name, icon, accent_hex) */
#define DASH_ROOM_LIST \
    X(DASH_ROOM_LOFT,    "Loft",            UI_ICON_HOME_ROOF, UI_HEX_CORAL) \
    X(DASH_ROOM_OFFICE,  "Office",          UI_ICON_CHAIR,     UI_HEX_BLUE)  \
    X(DASH_ROOM_LIVING,  "Living Room",     UI_ICON_SOFA,      UI_HEX_GREEN) \
    X(DASH_ROOM_HALLWAY, "Hallway & Other", UI_ICON_IMAGE,     UI_HEX_AMBER)

/* POSIX TZ for the on-panel clock (localtime of SNTP-synced UTC).
 * America/Denver — set yours. */
#define DASH_TIMEZONE "MST7MDT,M3.2.0,M11.1.0"

/* Slot flags. */
#define DASH_F_CONFIRM    (1u << 0) /* ACTION: msgbox confirm before the call  */
#define DASH_F_BRIGHTNESS (1u << 1) /* LIGHT: render a brightness slider       */
#define DASH_F_ROOM_TEMP  (1u << 2) /* SENSOR: mirrored on the Home room card  */
#define DASH_F_COMPACT    (1u << 3) /* SENSOR: 3-up cell in a shared env row
                                     * instead of a hero/full-width stat row   */

/* Entity slots.
 * X(sym, page, kind, entity_id, label, icon, accent_hex, unit, legacy, flags, action)
 *
 * action "" for most slots. A SENSOR row with an action entity renders
 *        pressable and fires it on tap (DASH_F_CONFIRM applies) — used to
 *        merge a status entity and its control script into one card.
 *
 * kind   SENSOR | TOGGLE | LIGHT | ACTION | MEDIA  (-> DASH_KIND_*)
 *        ACTION slots are scripts/scenes: fire-and-forget, never subscribed.
 * legacy -1, or the legacy display index 0=temp 1=humidity 2=co2 — those
 *        values additionally feed VIEW_EVENT_HA_SENSOR so the history ring
 *        and the Trends chart keep working unchanged.
 */
#define DASH_SLOT_LIST \
    /* ── Home quick actions ─────────────────────────────────────────────── */ \
    X(SLOT_ALL_OFF,       DASH_PAGE_HOME,    ACTION, "script.all_lights_off",       "All Lights Off", UI_ICON_LIGHT_GROUP_OFF, UI_HEX_RED,     "",    -1, DASH_F_CONFIRM, "") \
    X(SLOT_XMAS_PORCH,    DASH_PAGE_HOME,    TOGGLE, "switch.xmas_porch",           "Xmas Porch",     UI_ICON_STRING_LIGHTS,   UI_HEX_AMBER,   "",    -1, 0, "") \
    X(SLOT_XMAS_HALL,     DASH_PAGE_HOME,    TOGGLE, "switch.xmas_hall",            "Xmas Hall",      UI_ICON_STRING_LIGHTS,   UI_HEX_AMBER,   "",    -1, 0, "") \
    /* ── Loft ───────────────────────────────────────────────────────────── */ \
    X(SLOT_LOFT_TEMP,     DASH_PAGE_LOFT,    SENSOR, "sensor.loft_temperature",     "Temperature",    UI_ICON_THERMOMETER,     UI_HEX_AMBER,   "°F",   0, DASH_F_ROOM_TEMP | DASH_F_COMPACT, "") \
    X(SLOT_LOFT_HUM,      DASH_PAGE_LOFT,    SENSOR, "sensor.loft_humidity",        "Humidity",       UI_ICON_WATER_PCT,       UI_HEX_BLUE,    "%",    1, DASH_F_COMPACT, "") \
    X(SLOT_LOFT_CO2,      DASH_PAGE_LOFT,    SENSOR, "sensor.loft_co2",             "CO2",            UI_ICON_MOLECULE_CO2,    UI_HEX_TEXT,    "ppm",  2, DASH_F_COMPACT, "") \
    X(SLOT_LOFT_LED,      DASH_PAGE_LOFT,    LIGHT,  "light.loft_led_strip",        "LED Strip",      UI_ICON_LED_STRIP,       UI_HEX_GREEN,   "",    -1, DASH_F_BRIGHTNESS, "") \
    X(SLOT_LOFT_NORTH,    DASH_PAGE_LOFT,    LIGHT,  "light.loft_floor_lamp",       "Floor Lamp",     UI_ICON_FLOOR_LAMP,      UI_HEX_CORAL,   "",    -1, DASH_F_BRIGHTNESS, "") \
    X(SLOT_LOFT_FUTURE,   DASH_PAGE_LOFT,    LIGHT,  "light.loft_table_lamp",       "Table Lamp",     UI_ICON_LIGHTBULB,       UI_HEX_CORAL,   "",    -1, DASH_F_BRIGHTNESS, "") \
    X(SLOT_LOFT_CHILL,    DASH_PAGE_LOFT,    ACTION, "automation.loft_chill_mode",  "Chill Mode",     UI_ICON_BRIGHTNESS,      UI_HEX_CORAL,   "",    -1, 0, "") \
    /* ── Office ─────────────────────────────────────────────────────────── */ \
    X(SLOT_OFFICE_TEMP,   DASH_PAGE_OFFICE,  SENSOR, "sensor.office_temperature",   "Temperature",    UI_ICON_THERMOMETER,     UI_HEX_AMBER,   "°F",  -1, DASH_F_ROOM_TEMP, "") \
    X(SLOT_OFFICE_MAIN,   DASH_PAGE_OFFICE,  LIGHT,  "light.office_floor_lamp",     "Floor Lamp",     UI_ICON_FLOOR_LAMP,      UI_HEX_BLUE,    "",    -1, DASH_F_BRIGHTNESS, "") \
    X(SLOT_OFFICE_READ,   DASH_PAGE_OFFICE,  LIGHT,  "light.office_reading_lamp",   "Reading Lamp",   UI_ICON_DESK_LAMP,       UI_HEX_BLUE,    "",    -1, DASH_F_BRIGHTNESS, "") \
    X(SLOT_OFFICE_STRIP,  DASH_PAGE_OFFICE,  LIGHT,  "light.office_light_strip",    "LED Strip",      UI_ICON_LED_STRIP,       UI_HEX_BLUE,    "",    -1, DASH_F_BRIGHTNESS, "") \
    /* ── Living Room ────────────────────────────────────────────────────── */ \
    X(SLOT_LIVING_TEMP,   DASH_PAGE_LIVING,  SENSOR, "sensor.living_temperature",   "Temperature",    UI_ICON_THERMOMETER,     UI_HEX_AMBER,   "°F",  -1, DASH_F_ROOM_TEMP, "") \
    X(SLOT_LIVING_MEDIA,  DASH_PAGE_LIVING,  MEDIA,  "media_player.living_room",    "Living Room",    UI_ICON_SPEAKER,         UI_HEX_PRIMARY, "",    -1, 0, "") \
    X(SLOT_PRESET_CHILL,  DASH_PAGE_LIVING,  ACTION, "script.playlist_chill",       "Chill",          UI_ICON_SNOWFLAKE,       UI_HEX_BLUE,    "",    -1, 0, "") \
    X(SLOT_PRESET_OLDIES, DASH_PAGE_LIVING,  ACTION, "script.playlist_oldies",      "Oldies",         UI_ICON_ALBUM,           UI_HEX_AMBER,   "",    -1, 0, "") \
    X(SLOT_PRESET_HIPHOP, DASH_PAGE_LIVING,  ACTION, "script.playlist_90s_hiphop",  "90s Hiphop",     UI_ICON_MIC,             UI_HEX_GREEN,   "",    -1, 0, "") \
    /* ── Hallway & Other ────────────────────────────────────────────────── */ \
    X(SLOT_HALL_TEMP,     DASH_PAGE_HALLWAY, SENSOR, "sensor.hallway_temperature",  "Temperature",    UI_ICON_THERMOMETER,     UI_HEX_AMBER,   "°F",  -1, DASH_F_ROOM_TEMP, "") \
    X(SLOT_HALL_SERVER,   DASH_PAGE_HALLWAY, SENSOR, "binary_sensor.server_power",  "Server",         UI_ICON_SERVER,          UI_HEX_PRIMARY, "",    -1, DASH_F_CONFIRM, "script.server_power_toggle") \
    X(SLOT_HALL_SCONCE,   DASH_PAGE_HALLWAY, LIGHT,  "light.hallway_sconce",        "Hallway Sconce", UI_ICON_WALL_SCONCE,     UI_HEX_AMBER,   "",    -1, DASH_F_BRIGHTNESS, "") \
    X(SLOT_HALL_ENTRY,    DASH_PAGE_HALLWAY, LIGHT,  "light.entrance_sconce",       "Entrance Sconce", UI_ICON_WALL_SCONCE,    UI_HEX_AMBER,   "",    -1, DASH_F_BRIGHTNESS, "") \
    X(SLOT_HALL_BATH,     DASH_PAGE_HALLWAY, LIGHT,  "light.bathroom",              "Bathroom",       UI_ICON_LIGHTBULB,       UI_HEX_AMBER,   "",    -1, DASH_F_BRIGHTNESS, "") \

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* DASHBOARD_CONFIG_H */
