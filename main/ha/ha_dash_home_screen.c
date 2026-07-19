#include "ha_dash_home_screen.h"

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

static const char *TAG = "dash-home";

/* The nav tile map (nav.h literals) must track the room table. */
_Static_assert(NAV_TILE_HA_TREND == NAV_TILE_ROOM_FIRST + DASH_ROOM_COUNT,
               "nav.h tile map out of sync with DASH_ROOM_LIST");
_Static_assert(NAV_TILE_COUNT == NAV_TILE_ROOM_FIRST + DASH_ROOM_COUNT + 1,
               "NAV_TILE_COUNT out of sync with DASH_ROOM_LIST");

/* ── Geometry (480x480, corners owned by the gear + WiFi buttons) ────────── */
#define PAD          16
#define PILL_Y       22
#define CHIP_Y       76
#define CHIP_W       141
#define CHIP_H       100
#define CHIP_GAP     12
#define GRID_Y       188
#define CARD_W       218
#define CARD_H       134
#define CARD_GAP     12
#define BADGE_D      110

/* ── Widget registry ─────────────────────────────────────────────────────── */
static lv_obj_t *s_status_pill = NULL;
static lv_obj_t *s_chip[DASH_SLOT_COUNT];      /* HOME-page chips by slot */
static lv_obj_t *s_chip_icon[DASH_SLOT_COUNT]; /* their glyph labels */
static lv_obj_t *s_room_temp[DASH_ROOM_COUNT]; /* room-card temp labels */
static lv_obj_t *s_confirm_mbox = NULL;
static int       s_confirm_slot = -1;

/* ── Service-call helpers (LVGL task context — no lock, bounded posts) ───── */

static void _call_toggle(int slot, bool on)
{
    ha_ws_call("homeassistant", on ? "turn_on" : "turn_off",
               dash_slots[slot].entity_id, NULL);
}

/* Optimistic chip paint: checked state + icon tint, applied silently (no
 * VALUE_CHANGED re-entry); the subscription echo reconciles the real state. */
static void _apply_chip_state(int slot, bool on)
{
    if (s_chip[slot] == NULL) {
        return;
    }
    if (on) {
        lv_obj_add_state(s_chip[slot], LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_chip[slot], LV_STATE_CHECKED);
    }
    lv_obj_set_style_text_color(s_chip_icon[slot],
                                on ? lv_color_hex(dash_slots[slot].accent) : UI_COLOR_TEXT,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void _toggle_chip_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int  slot = (int)(intptr_t)lv_event_get_user_data(e);
    bool on   = !lv_obj_has_state(s_chip[slot], LV_STATE_CHECKED);
    _apply_chip_state(slot, on);
    _call_toggle(slot, on);
}

/* ── All-off confirm dialog (ported from the retired General Controls) ───── */

static void _confirm_close(void)
{
    if (s_confirm_mbox != NULL) {
        lv_msgbox_close(s_confirm_mbox);
        s_confirm_mbox = NULL;
        s_confirm_slot = -1;
    }
}

static void _confirm_ok_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_confirm_slot >= 0) {
        ha_ws_call("script", "turn_on", dash_slots[s_confirm_slot].entity_id, NULL);
        ESP_LOGI(TAG, "action: %s", dash_slots[s_confirm_slot].entity_id);
    }
    _confirm_close();
}

static void _confirm_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _confirm_close();
    }
}

static void _action_chip_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int slot = (int)(intptr_t)lv_event_get_user_data(e);

    if (!(dash_slots[slot].flags & DASH_F_CONFIRM)) {
        ha_ws_call("script", "turn_on", dash_slots[slot].entity_id, NULL);
        return;
    }
    if (s_confirm_mbox != NULL) {
        return;
    }

    lv_obj_t *mbox = lv_msgbox_create(NULL);
    s_confirm_mbox = mbox;
    s_confirm_slot = slot;
    lv_obj_set_style_text_font(mbox, UI_FONT_LABEL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(mbox, UI_COLOR_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(mbox, UI_RADIUS_CARD, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_msgbox_add_title(mbox, dash_slots[slot].label);
    lv_msgbox_add_text(mbox, "Turn off all lights?");

    lv_obj_t *ok = lv_msgbox_add_footer_button(mbox, "Turn Off");
    lv_obj_set_style_bg_color(ok, UI_COLOR_RED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ok, _confirm_ok_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_add_event_cb(cancel, _confirm_cancel_cb, LV_EVENT_CLICKED, NULL);

    /* Subtle fade + rise as the confirm dialog appears (~250 ms ease-out). */
    ui_modal_anim_in(mbox);
}

/* ── Room cards ──────────────────────────────────────────────────────────── */

static void _room_card_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int room = (int)(intptr_t)lv_event_get_user_data(e);
    nav_go_tile(NAV_TILE_ROOM_FIRST + room);
}

static void _create_room_card(lv_obj_t *tile, int room, int x, int y)
{
    const dash_room_t *def    = &dash_rooms[room];
    lv_color_t         accent = lv_color_hex(def->accent);

    lv_obj_t *card = lv_obj_create(tile);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_pos(card, x, y);
    ui_apply_card(card);
    ui_make_pressable(card);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(card, _room_card_cb, LV_EVENT_CLICKED, (void *)(intptr_t)room);

    /* Tinted icon disc, deliberately overflowing the bottom-left corner; the
     * card's rounded clip crops it into the accent wedge of the HA design. */
    lv_obj_t *badge = ui_icon_badge(card, def->icon, &ui_font_mdi_48, accent, BADGE_D);
    lv_obj_set_align(badge, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_pos(badge, -22, 30);

    lv_obj_t *name = ui_label(card, def->name, UI_FONT_BODY, accent);
    lv_obj_set_pos(name, PAD, 14);

    int slot = dash_room_temp_slot(room);
    s_room_temp[room] = ui_label(card, "--", UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
    lv_obj_set_pos(s_room_temp[room], PAD, 44);
    (void)slot; /* slot→label mapping happens in the event handler */
}

/* ── Status pill ─────────────────────────────────────────────────────────── */

static void _apply_status(uint8_t status)
{
    if (s_status_pill == NULL) {
        return;
    }
    const char *text;
    lv_color_t  color;
    if (!ha_ws_is_enabled()) {
        text  = "MQTT (read-only)";
        color = UI_COLOR_TEXT_MUTED;
    } else {
        switch ((ha_ws_status_t)status) {
            case HA_WS_STATUS_SUBSCRIBED:  text = "Live";            color = UI_COLOR_GREEN;      break;
            case HA_WS_STATUS_CONNECTING:
            case HA_WS_STATUS_AUTHENTICATING:
                                           text = "Connecting...";   color = UI_COLOR_AMBER;      break;
            case HA_WS_STATUS_AUTH_FAILED: text = "Auth failed";     color = UI_COLOR_RED;        break;
            case HA_WS_STATUS_UNCONFIGURED:text = "Set up HA in Settings"; color = UI_COLOR_TEXT_MUTED; break;
            default:                       text = "Offline";         color = UI_COLOR_TEXT_MUTED; break;
        }
    }
    lv_label_set_text(s_status_pill, text);
    lv_obj_set_style_text_color(s_status_pill, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* ── Live state (view_event_task → LVGL lock) ────────────────────────────── */

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
    const dash_slot_t *def = &dash_slots[data->slot];

    lv_port_sem_take();
    if (s_chip[data->slot] != NULL && data->state[0] != '\0') {
        /* Quick-action toggle chip: only real on/off states repaint; anything
         * else ("unavailable") drops the chip to unchecked. */
        _apply_chip_state(data->slot, strcmp(data->state, "on") == 0);
    }
    if (def->flags & DASH_F_ROOM_TEMP) {
        int room = def->page - (DASH_PAGE_HOME + 1);
        if (room >= 0 && room < DASH_ROOM_COUNT && s_room_temp[room] != NULL) {
            char *end = NULL;
            (void)strtod(data->state, &end);
            bool numeric = end != data->state && end != NULL && *end == '\0';
            if (numeric) {
                lv_label_set_text_fmt(s_room_temp[room], "%s %s", data->state, def->unit);
            } else {
                lv_label_set_text(s_room_temp[room], "--");
            }
        }
    }
    lv_port_sem_give();
}

static void _status_event_handler(void *handler_args, esp_event_base_t base, int32_t id,
                                  void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)id;
    const struct view_data_ha_ws_status *st = (const struct view_data_ha_ws_status *)event_data;
    if (st == NULL) {
        return;
    }
    lv_port_sem_take();
    _apply_status(st->status);
    lv_port_sem_give();
}

/* ── Build ───────────────────────────────────────────────────────────────── */

void ha_dash_home_screen_init(void)
{
    lv_port_sem_take();
    lv_obj_t *tile = nav_get_tile(NAV_TILE_HOME);

    /* Connection pill between the gear (top-left) and WiFi (top-right). */
    s_status_pill = ui_label(tile, "", LV_FONT_DEFAULT, UI_COLOR_TEXT_MUTED);
    lv_obj_set_align(s_status_pill, LV_ALIGN_TOP_MID);
    lv_obj_set_y(s_status_pill, PILL_Y);
    ha_ws_status_snapshot_t snap;
    ha_ws_status_get(&snap);
    _apply_status((uint8_t)snap.status);

    /* Quick-action chips: every HOME-page slot, in table order. */
    int x = PAD;
    for (int i = 0; i < DASH_SLOT_COUNT; i++) {
        const dash_slot_t *def = &dash_slots[i];
        if (def->page != DASH_PAGE_HOME) {
            continue;
        }
        lv_obj_t *chip = ui_chip(tile, def->icon, &ui_font_mdi_32, def->label);
        lv_obj_set_size(chip, CHIP_W, CHIP_H);
        lv_obj_set_pos(chip, x, CHIP_Y);
        s_chip[i]      = chip;
        s_chip_icon[i] = lv_obj_get_child(chip, 0);

        if (def->kind == DASH_KIND_TOGGLE) {
            ui_make_checkable(chip, lv_color_hex(def->accent));
            lv_obj_add_event_cb(chip, _toggle_chip_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        } else {
            lv_obj_add_event_cb(chip, _action_chip_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
        x += CHIP_W + CHIP_GAP;
    }

    /* 2x2 room grid. */
    for (int room = 0; room < DASH_ROOM_COUNT; room++) {
        int col = room % 2;
        int row = room / 2;
        _create_room_card(tile, room, PAD + col * (CARD_W + CARD_GAP),
                          GRID_Y + row * (CARD_H + CARD_GAP));
    }
    lv_port_sem_give();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ENTITY, _entity_event_handler,
        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_WS_STATUS, _status_event_handler,
        NULL, NULL));
}
