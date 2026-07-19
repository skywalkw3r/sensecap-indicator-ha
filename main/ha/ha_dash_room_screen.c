#include "ha_dash_room_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "ha_dash.h"
#include "ha_ws.h"
#include "lv_port.h"
#include "nav.h"
#include "ui_components.h"
#include "ui_theme.h"
#include "view_data.h"

/* ── Geometry (480x480, ui_header on top) ────────────────────────────────── */
#define PAD          16
#define CONTENT_W    448
#define CONTENT_Y    96
#define ROW_GAP      10
#define HERO_H       96
#define STAT_H       64
#define TOGGLE_H     72
#define LIGHT_H      116
#define MEDIA_H      140
#define ACTION_W     141
#define ACTION_H     96
#define ACTION_GAP   12

#define TOGGLE_TRACK_HEX 0x4F4F4F

/* ── Widget registry (indexed by slot; NULL where not built) ─────────────── */
static struct {
    lv_obj_t *value;   /* SENSOR: value label */
    lv_obj_t *toggle;  /* TOGGLE/LIGHT: lv_switch */
    lv_obj_t *slider;  /* LIGHT: brightness slider */
    lv_obj_t *title;   /* MEDIA: track title */
    lv_obj_t *artist;  /* MEDIA: artist line */
    lv_obj_t *play;    /* MEDIA: play/pause glyph label */
} s_w[DASH_SLOT_COUNT];

/* ── Input callbacks (LVGL task context — no lock) ───────────────────────── */

static void _toggle_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    int  slot = (int)(intptr_t)lv_event_get_user_data(e);
    bool on   = lv_obj_has_state(s_w[slot].toggle, LV_STATE_CHECKED);
    ha_ws_call("homeassistant", on ? "turn_on" : "turn_off", dash_slots[slot].entity_id, NULL);
}

/* Post once on release (not every drag tick), same discipline as the retired
 * brightness slider. brightness_pct also turns the light on in HA. */
static void _slider_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    int  slot = (int)(intptr_t)lv_event_get_user_data(e);
    char extra[40];
    snprintf(extra, sizeof(extra), "{\"brightness_pct\":%d}",
             (int)lv_slider_get_value(s_w[slot].slider));
    ha_ws_call("light", "turn_on", dash_slots[slot].entity_id, extra);
}

static void _play_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    ha_ws_call("media_player", "media_play_pause", dash_slots[slot].entity_id, NULL);
}

static void _action_confirmed(void *user_data)
{
    int slot = (int)(intptr_t)user_data;
    ha_ws_call("script", "turn_on", dash_slots[slot].entity_id, NULL);
}

static void _preset_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (dash_slots[slot].flags & DASH_F_CONFIRM) {
        char text[64];
        snprintf(text, sizeof(text), "Run \"%s\"?", dash_slots[slot].label);
        ui_confirm_msgbox("Confirm", text, "Run", UI_COLOR_RED,
                          _action_confirmed, (void *)(intptr_t)slot);
        return;
    }
    ha_ws_call("script", "turn_on", dash_slots[slot].entity_id, NULL);
}

/* ── Card builders (LVGL lock held by init) ──────────────────────────────── */

static lv_obj_t *_card(lv_obj_t *tile, int y, int h)
{
    lv_obj_t *card = lv_obj_create(tile);
    lv_obj_set_size(card, CONTENT_W, h);
    lv_obj_set_pos(card, PAD, y);
    ui_apply_card(card);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    return card;
}

/* Hero temperature: big value + accent badge, the room's headline number. */
static void _build_hero(lv_obj_t *tile, int slot, int y)
{
    const dash_slot_t *def    = &dash_slots[slot];
    lv_color_t         accent = lv_color_hex(def->accent);
    lv_obj_t          *card   = _card(tile, y, HERO_H);

    lv_obj_t *label = ui_label(card, def->label, UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
    lv_obj_set_pos(label, PAD, 12);

    s_w[slot].value = ui_label(card, "--", UI_FONT_VALUE, accent);
    lv_obj_set_pos(s_w[slot].value, PAD, 34);

    lv_obj_t *badge = ui_icon_badge(card, def->icon, &ui_font_mdi_48, accent, 72);
    lv_obj_set_align(badge, LV_ALIGN_RIGHT_MID);
    lv_obj_set_x(badge, -PAD);
}

static void _build_stat(lv_obj_t *tile, int slot, int y)
{
    const dash_slot_t *def    = &dash_slots[slot];
    lv_color_t         accent = lv_color_hex(def->accent);
    lv_obj_t          *card   = _card(tile, y, STAT_H);

    lv_obj_t *badge = ui_icon_badge(card, def->icon, &ui_font_mdi_32, accent, 44);
    lv_obj_set_align(badge, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(badge, 12);

    lv_obj_t *label = ui_label(card, def->label, UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_align(label, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(label, 68);

    s_w[slot].value = ui_label(card, "--", &lv_font_montserrat_24, accent);
    lv_obj_set_align(s_w[slot].value, LV_ALIGN_RIGHT_MID);
    lv_obj_set_x(s_w[slot].value, -PAD);
}

/* Embedded switch styling ported from the retired control tiles (grey track,
 * green checked, right-aligned so long labels can't collide with it). */
static lv_obj_t *_make_switch(lv_obj_t *card, int slot, int y)
{
    lv_obj_t *toggle = lv_switch_create(card);
    lv_obj_set_size(toggle, 64, 32);
    lv_obj_set_align(toggle, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(toggle, -PAD, y);
    lv_obj_set_style_radius(toggle, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(toggle, 40, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(TOGGLE_TRACK_HEX),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(toggle, UI_COLOR_GREEN,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(toggle, _toggle_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)slot);
    return toggle;
}

static void _build_toggle(lv_obj_t *tile, int slot, int y)
{
    const dash_slot_t *def  = &dash_slots[slot];
    lv_obj_t          *card = _card(tile, y, TOGGLE_H);

    lv_obj_t *badge = ui_icon_badge(card, def->icon, &ui_font_mdi_32, lv_color_hex(def->accent), 48);
    lv_obj_set_align(badge, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(badge, 12);

    lv_obj_t *label = ui_label(card, def->label, UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_align(label, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(label, 72);

    s_w[slot].toggle = _make_switch(card, slot, (TOGGLE_H - 32) / 2);
}

static void _build_light(lv_obj_t *tile, int slot, int y)
{
    const dash_slot_t *def    = &dash_slots[slot];
    lv_color_t         accent = lv_color_hex(def->accent);
    lv_obj_t          *card   = _card(tile, y, LIGHT_H);

    lv_obj_t *badge = ui_icon_badge(card, def->icon, &ui_font_mdi_32, accent, 48);
    lv_obj_set_pos(badge, 12, 12);

    lv_obj_t *label = ui_label(card, def->label, UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_pos(label, 72, 22);

    s_w[slot].toggle = _make_switch(card, slot, 20);

    lv_obj_t *minus = ui_label(card, LV_SYMBOL_MINUS, UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
    lv_obj_set_pos(minus, 22, 76);
    lv_obj_t *plus = ui_label(card, LV_SYMBOL_PLUS, UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
    lv_obj_set_align(plus, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(plus, -22, 76);

    /* Centered between the − / + glyphs. The knob overhangs the track ends by
     * ~15 px, so the track needs that much clearance on top of the glyph
     * widths or the knob covers them at the range extremes. */
    lv_obj_t *slider = lv_slider_create(card);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_set_size(slider, 290, 20);
    lv_obj_set_align(slider, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(slider, 0, 74);
    lv_obj_set_style_radius(slider, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, UI_COLOR_SURFACE_PRESSED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, accent, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, UI_COLOR_TEXT, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(slider, _slider_cb, LV_EVENT_RELEASED, (void *)(intptr_t)slot);
    s_w[slot].slider = slider;
}

static void _build_media(lv_obj_t *tile, int slot, int y)
{
    const dash_slot_t *def    = &dash_slots[slot];
    lv_color_t         accent = lv_color_hex(def->accent);
    lv_obj_t          *card   = _card(tile, y, MEDIA_H);

    lv_obj_t *badge = ui_icon_badge(card, def->icon, &ui_font_mdi_32, accent, 56);
    lv_obj_set_pos(badge, 16, 16);

    lv_obj_t *name = ui_label(card, def->label, UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
    lv_obj_set_pos(name, 88, 16);

    s_w[slot].title = ui_label(card, "Nothing playing", UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_pos(s_w[slot].title, 88, 46);
    lv_obj_set_width(s_w[slot].title, 240);
    lv_label_set_long_mode(s_w[slot].title, LV_LABEL_LONG_SCROLL_CIRCULAR);

    s_w[slot].artist = ui_label(card, "", UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
    lv_obj_set_pos(s_w[slot].artist, 88, 78);
    lv_obj_set_width(s_w[slot].artist, 240);
    lv_label_set_long_mode(s_w[slot].artist, LV_LABEL_LONG_DOT);

    /* Play/pause: accent disc button, glyph swapped by playback state. */
    lv_obj_t *button = lv_button_create(card);
    lv_obj_set_size(button, 72, 72);
    lv_obj_set_align(button, LV_ALIGN_RIGHT_MID);
    lv_obj_set_x(button, -16);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(button, _play_cb, LV_EVENT_CLICKED, (void *)(intptr_t)slot);

    s_w[slot].play = ui_label(button, UI_ICON_PLAY, &ui_font_mdi_48, accent);
    lv_obj_center(s_w[slot].play);
}

static lv_obj_t *_build_action_chip(lv_obj_t *tile, int slot, int x, int y)
{
    const dash_slot_t *def  = &dash_slots[slot];
    lv_obj_t          *chip = ui_chip(tile, def->icon, &ui_font_mdi_32, def->label);
    lv_obj_set_size(chip, ACTION_W, ACTION_H);
    lv_obj_set_pos(chip, x, y);
    lv_obj_add_event_cb(chip, _preset_cb, LV_EVENT_CLICKED, (void *)(intptr_t)slot);
    return chip;
}

/* ── Live state (view_event_task → LVGL lock) ────────────────────────────── */

static void _apply_sensor(int slot, const char *state)
{
    if (state[0] == '\0' || strcmp(state, "unavailable") == 0 ||
        strcmp(state, "unknown") == 0 || strcmp(state, "none") == 0) {
        lv_label_set_text(s_w[slot].value, "--");
        return;
    }
    char *end = NULL;
    (void)strtod(state, &end);
    bool numeric = end != state && end != NULL && *end == '\0';
    if (numeric) {
        lv_label_set_text_fmt(s_w[slot].value, "%s %s", state, dash_slots[slot].unit);
    } else {
        /* Textual states render verbatim — e.g. a binary_sensor's "on"/"off"
         * (the Supermicro power row). */
        lv_label_set_text(s_w[slot].value, state);
    }
}

static void _apply_toggle(int slot, const char *state)
{
    /* Silent apply: lv_obj_add/remove_state never fires VALUE_CHANGED, so the
     * subscription echo can't bounce back into another service call. */
    if (strcmp(state, "on") == 0) {
        lv_obj_add_state(s_w[slot].toggle, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_w[slot].toggle, LV_STATE_CHECKED);
    }
}

static void _entity_event_handler(void *handler_args, esp_event_base_t base, int32_t id,
                                  void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)id;
    const struct view_data_ha_entity *data = (const struct view_data_ha_entity *)event_data;
    if (data == NULL || data->slot >= DASH_SLOT_COUNT) {
        return;
    }

    lv_port_sem_take();
    if (s_w[data->slot].value != NULL && data->state[0] != '\0') {
        _apply_sensor(data->slot, data->state);
    }
    if (s_w[data->slot].toggle != NULL && data->state[0] != '\0') {
        _apply_toggle(data->slot, data->state);
    }
    if (s_w[data->slot].slider != NULL && data->brightness >= 0) {
        lv_slider_set_value(s_w[data->slot].slider, (data->brightness * 100 + 127) / 255,
                            LV_ANIM_OFF);
    }
    lv_port_sem_give();
}

static void _media_event_handler(void *handler_args, esp_event_base_t base, int32_t id,
                                 void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)id;
    const struct view_data_ha_media *data = (const struct view_data_ha_media *)event_data;
    if (data == NULL || data->slot >= DASH_SLOT_COUNT || s_w[data->slot].title == NULL) {
        return;
    }

    lv_port_sem_take();
    lv_label_set_text(s_w[data->slot].title,
                      data->title[0] != '\0' ? data->title : "Nothing playing");
    lv_label_set_text(s_w[data->slot].artist, data->artist);
    lv_label_set_text(s_w[data->slot].play,
                      strcmp(data->state, "playing") == 0 ? UI_ICON_PAUSE : UI_ICON_PLAY);
    lv_port_sem_give();
}

/* ── Build ───────────────────────────────────────────────────────────────── */

static void _build_room_page(int room)
{
    lv_obj_t *tile = nav_get_tile(NAV_TILE_ROOM_FIRST + room);
    ui_header(tile, dash_rooms[room].name);

    uint8_t page      = (uint8_t)(DASH_PAGE_HOME + 1 + room);
    int     y         = CONTENT_Y;
    int     action_x  = PAD;
    int     action_y  = -1; /* first ACTION slot opens the chip row */

    for (int i = 0; i < DASH_SLOT_COUNT; i++) {
        const dash_slot_t *def = &dash_slots[i];
        if (def->page != page) {
            continue;
        }
        switch ((dash_kind_t)def->kind) {
            case DASH_KIND_SENSOR:
                if (def->flags & DASH_F_ROOM_TEMP) {
                    _build_hero(tile, i, y);
                    y += HERO_H + ROW_GAP;
                } else {
                    _build_stat(tile, i, y);
                    y += STAT_H + ROW_GAP;
                }
                break;
            case DASH_KIND_TOGGLE:
                _build_toggle(tile, i, y);
                y += TOGGLE_H + ROW_GAP;
                break;
            case DASH_KIND_LIGHT:
                _build_light(tile, i, y);
                y += LIGHT_H + ROW_GAP;
                break;
            case DASH_KIND_MEDIA:
                _build_media(tile, i, y);
                y += MEDIA_H + ROW_GAP;
                break;
            case DASH_KIND_ACTION:
                if (action_y < 0) {
                    action_y = y;
                    y += ACTION_H + ROW_GAP;
                }
                _build_action_chip(tile, i, action_x, action_y);
                action_x += ACTION_W + ACTION_GAP;
                break;
        }
    }
}

void ha_dash_room_screens_init(void)
{
    lv_port_sem_take();
    for (int room = 0; room < DASH_ROOM_COUNT; room++) {
        _build_room_page(room);
    }
    lv_port_sem_give();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ENTITY, _entity_event_handler,
        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_MEDIA, _media_event_handler,
        NULL, NULL));
}
