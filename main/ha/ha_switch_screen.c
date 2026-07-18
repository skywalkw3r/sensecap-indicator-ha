#include "ha_switch_screen.h"

#include "esp_log.h"
#include "home_assistant_config.h"
#include "lv_port.h"
#include "nav.h"
#include "view_data.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SWITCH_SLOT_COUNT 8

#define TILE_WIDTH 480
#define HEADER_HEIGHT 85

#define CARD_WIDTH 214
#define CARD_HEIGHT 164
#define ROW_HEIGHT 78
#define CARD_RADIUS 12
#define CARD_BG 0x282828
#define CARD_BG_CHECKED 0x292829
#define TEXT_MUTED 0x9E9E9E
#define SWITCH_GREEN 0x529D53
#define SWITCH_TRACK 0x4F4F4F
#define ARC_TRACK 0x1C1C1C

LV_IMAGE_DECLARE(ui_img_ic_temp_png);
LV_IMAGE_DECLARE(ui_img_ic_hum_png);
LV_IMAGE_DECLARE(ui_img_ic_co2_png);
LV_IMAGE_DECLARE(ui_img_ic_switch1_on_png);
LV_IMAGE_DECLARE(ui_img_ic_switch1_off_png);
LV_FONT_DECLARE(ui_font_font0);
LV_FONT_DECLARE(ui_font_font2);

static const char *TAG = "ha-switch-screen";

typedef void (*widget_update_fn)(lv_obj_t *widget, int value, void *aux);

typedef struct {
    int index;
    lv_obj_t *widget;
    lv_obj_t *icon;
    const lv_image_dsc_t *icon_on;
    const lv_image_dsc_t *icon_off;
    void *aux;
    widget_update_fn update;
} switch_slot_t;

struct ha_switch_screen {
    switch_slot_t slots[SWITCH_SLOT_COUNT];
    bool created;
};

static ha_switch_screen_t _instance;

static const char *const switch_names[SWITCH_SLOT_COUNT] = {
    CONFIG_SWITCH1_UI_NAME,
    CONFIG_SWITCH2_UI_NAME,
    CONFIG_SWITCH3_UI_NAME,
    CONFIG_SWITCH4_UI_NAME,
    CONFIG_SWITCH5_UI_NAME,
    CONFIG_SWITCH6_UI_NAME,
    CONFIG_SWITCH7_UI_NAME,
    CONFIG_SWITCH8_UI_NAME,
};

static void _post_switch_value(int index, int value)
{
    struct view_data_ha_switch_data switch_data = {
        .index = index,
        .value = value,
    };

    ESP_LOGI(TAG, "switch%d: %d", index + 1, value);
    /*
     * Runs in the LVGL task on real user input. Bound the post so a full view
     * queue can never block the UI thread; a dropped ST tick just skips one
     * MQTT echo/save and the next input re-posts.
     */
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SWITCH_ST,
                                      &switch_data, sizeof(switch_data), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drop VIEW_EVENT_HA_SWITCH_ST (index %d): %s", index, esp_err_to_name(err));
    }
}

static void _set_toggle_icon(switch_slot_t *slot, bool checked)
{
    if (!slot || !slot->icon || !slot->icon_on || !slot->icon_off) {
        return;
    }

    lv_image_set_src(slot->icon, checked ? slot->icon_on : slot->icon_off);
}

static void _switch_toggle_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    switch_slot_t *slot = (switch_slot_t *)lv_event_get_user_data(e);
    if (!slot || !slot->widget) {
        return;
    }

    bool checked = lv_obj_has_state(slot->widget, LV_STATE_CHECKED);
    _set_toggle_icon(slot, checked);
    _post_switch_value(slot->index, checked ? 1 : 0);
}

static void _slider_event_cb(lv_event_t *e)
{
    /* Post once on release, not on every VALUE_CHANGED tick during a drag. */
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }

    switch_slot_t *slot = (switch_slot_t *)lv_event_get_user_data(e);
    if (!slot || !slot->widget) {
        return;
    }

    _post_switch_value(slot->index, lv_slider_get_value(slot->widget));
}

static void _update_toggle(lv_obj_t *w, int value, void *aux)
{
    (void)aux;

    if (value) {
        lv_obj_add_state(w, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(w, LV_STATE_CHECKED);
    }
    /*
     * Apply state silently. Firing VALUE_CHANGED here would re-enter the switch
     * callback and post VIEW_EVENT_HA_SWITCH_ST back into the view loop that is
     * driving this update -- the self-deadlock. The controller publishes the
     * echo directly instead (see ha_switch.c VIEW_EVENT_HA_SWITCH_SET).
     */
}

static void _update_momentary(lv_obj_t *w, int value, void *aux)
{
    (void)w;
    (void)value;
    (void)aux;
    /* Momentary/read-only slots: MQTT set/restore messages carry no visual
     * state to apply (All Lights pulses; the temp card is fed by
     * VIEW_EVENT_HA_SENSOR, not by the switch protocol). */
}

static void _update_slider(lv_obj_t *w, int value, void *aux)
{
    (void)aux;

    lv_slider_set_value(w, value, LV_ANIM_ON);
    /* Silent apply: no event re-entry into the view loop. */
}

static void _style_panel(lv_obj_t *obj, int32_t width, int32_t height, int32_t x, int32_t y)
{
    lv_obj_set_size(obj, width, height);
    lv_obj_set_pos(obj, x, y);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, CARD_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(CARD_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void _style_checkable_card(lv_obj_t *btn, int32_t width, int32_t height, int32_t x, int32_t y)
{
    _style_panel(btn, width, height, x, y);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_color(btn, lv_color_hex(CARD_BG_CHECKED), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
}

static lv_obj_t *_create_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    return label;
}

static void _create_header(lv_obj_t *tile, const char *title)
{
    lv_obj_t *header = lv_obj_create(tile);
    lv_obj_set_size(header, TILE_WIDTH, HEADER_HEIGHT);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* montserrat, not the custom font0: font0's subset doesn't cover these
     * titles reliably (rendered blank in the simulator). */
    lv_obj_t *label = _create_label(header, title, &lv_font_montserrat_24, 0xFFFFFF);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
}

static lv_obj_t *_create_row_toggle(lv_obj_t *tile, switch_slot_t *slot, int32_t x, int32_t y,
                                    const lv_image_dsc_t *icon_on, const lv_image_dsc_t *icon_off)
{
    lv_obj_t *btn = lv_button_create(tile);
    _style_checkable_card(btn, CARD_WIDTH, ROW_HEIGHT, x, y);

    lv_obj_t *icon = lv_image_create(btn);
    lv_image_set_src(icon, icon_off);
    /* Right-aligned so longer labels ("Xmas Lights") don't crowd the icon. */
    lv_obj_set_pos(icon, 142, 2);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = _create_label(btn, switch_names[slot->index], &lv_font_montserrat_18, TEXT_MUTED);
    lv_obj_set_pos(label, 16, 22);

    slot->widget = btn;
    slot->icon = icon;
    slot->icon_on = icon_on;
    slot->icon_off = icon_off;
    slot->update = _update_toggle;
    lv_obj_add_event_cb(btn, _switch_toggle_event_cb, LV_EVENT_VALUE_CHANGED, slot);
    return btn;
}

static lv_obj_t *_create_embedded_switch(lv_obj_t *tile, switch_slot_t *slot, int32_t x, int32_t y,
                                         int32_t switch_width, int32_t switch_height)
{
    lv_obj_t *card = lv_button_create(tile);
    _style_panel(card, CARD_WIDTH, ROW_HEIGHT, x, y);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *glyph = _create_label(card, LV_SYMBOL_CHARGE, &lv_font_montserrat_18, 0xECBF41);
    lv_obj_set_pos(glyph, 16, 22);

    lv_obj_t *label = _create_label(card, switch_names[slot->index], &lv_font_montserrat_18, TEXT_MUTED);
    lv_obj_set_pos(label, 40, 22);

    lv_obj_t *toggle = lv_switch_create(card);
    lv_obj_set_size(toggle, switch_width, switch_height);
    lv_obj_set_pos(toggle, 140, 20);
    lv_obj_add_state(toggle, LV_STATE_CHECKED);
    lv_obj_set_style_radius(toggle, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(toggle, 40, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(SWITCH_TRACK), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(SWITCH_GREEN), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_DEFAULT);

    slot->widget = toggle;
    slot->update = _update_toggle;
    lv_obj_add_event_cb(toggle, _switch_toggle_event_cb, LV_EVENT_VALUE_CHANGED, slot);
    return toggle;
}

/* ── All Lights (slot 3 / switch4): momentary action with confirmation ──
 * Tap → confirm modal → publish switch4:1, then 0 after a short pulse so the
 * Home Assistant automation always sees a fresh off→on edge to trigger on. */

#define ALL_LIGHTS_ACCENT   0xD9534F
#define ALL_LIGHTS_PULSE_MS 1200

static lv_obj_t *s_confirm_mbox;

static void _all_lights_pulse_cb(lv_timer_t *timer)
{
    (void)timer;
    /* One-shot LVGL timer (LVGL task context); completes the 1→0 pulse. */
    _post_switch_value(3, 0);
}

static void _confirm_mbox_close(void)
{
    if (s_confirm_mbox) {
        lv_msgbox_close(s_confirm_mbox);
        s_confirm_mbox = NULL;
    }
}

static void _all_lights_confirm_cb(lv_event_t *e)
{
    (void)e;
    _confirm_mbox_close();
    _post_switch_value(3, 1);
    lv_timer_t *t = lv_timer_create(_all_lights_pulse_cb, ALL_LIGHTS_PULSE_MS, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void _all_lights_cancel_cb(lv_event_t *e)
{
    (void)e;
    _confirm_mbox_close();
}

static void _all_lights_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_confirm_mbox != NULL) {
        return;
    }

    lv_obj_t *mbox = lv_msgbox_create(NULL);
    s_confirm_mbox = mbox;
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_msgbox_add_title(mbox, "All Lights");
    lv_msgbox_add_text(mbox, "Turn off all lights?");

    lv_obj_t *ok = lv_msgbox_add_footer_button(mbox, "Turn Off");
    lv_obj_set_style_bg_color(ok, lv_color_hex(ALL_LIGHTS_ACCENT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ok, _all_lights_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_add_event_cb(cancel, _all_lights_cancel_cb, LV_EVENT_CLICKED, NULL);
}

static void _create_all_lights_card(lv_obj_t *tile, switch_slot_t *slot, int32_t x, int32_t y)
{
    lv_obj_t *btn = lv_button_create(tile);
    _style_panel(btn, CARD_WIDTH, CARD_HEIGHT, x, y);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    lv_obj_t *glyph = _create_label(btn, LV_SYMBOL_POWER, &lv_font_montserrat_48, ALL_LIGHTS_ACCENT);
    lv_obj_set_align(glyph, LV_ALIGN_CENTER);
    lv_obj_set_y(glyph, -14);

    lv_obj_t *label = _create_label(btn, switch_names[slot->index], &lv_font_montserrat_18, TEXT_MUTED);
    lv_obj_set_align(label, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(label, -5);

    slot->widget = btn;
    slot->update = _update_momentary;
    lv_obj_add_event_cb(btn, _all_lights_click_cb, LV_EVENT_CLICKED, slot);
}

/* ── HA-pushed display values: Loft climate + CO2 (read-only) ───────────
 * Fed by Home Assistant over CONFIG_TOPIC_DISPLAY_SET → VIEW_EVENT_HA_SENSOR
 * (see ha_sensor.c); never posts to the switch protocol.
 * Index contract: 0=temp 1=humidity 2=co2. */

#define HUMIDITY_ACCENT 0x52AAE5

static lv_obj_t *s_ha_display_value[CONFIG_HA_DISPLAY_VALUE_NUM];

static const char *const display_units[CONFIG_HA_DISPLAY_VALUE_NUM] = {
    CONFIG_HA_TEMP_UI_UNIT,
    CONFIG_HA_HUMIDITY_UI_UNIT,
    CONFIG_HA_CO2_UI_UNIT,
};

void ha_switch_screen_set_ha_value(int index, const char *value)
{
    /* Caller holds the LVGL lock (ha_switch.c event handler). */
    if (index < 0 || index >= CONFIG_HA_DISPLAY_VALUE_NUM || value == NULL) {
        return;
    }
    if (s_ha_display_value[index] == NULL) {
        return;
    }
    if (index == 0) {
        /* Temperature card renders the unit as its own label. */
        lv_label_set_text(s_ha_display_value[index], value);
    } else {
        /* Stat rows render value and unit inline. */
        char buf[48];
        lv_snprintf(buf, sizeof(buf), "%s %s", value, display_units[index]);
        lv_label_set_text(s_ha_display_value[index], buf);
    }
}

static void _create_loft_temp_card(lv_obj_t *tile, switch_slot_t *slot)
{
    lv_obj_t *card = lv_obj_create(tile);
    _style_panel(card, CARD_WIDTH, CARD_HEIGHT, 22, 96);

    lv_obj_t *icon = lv_image_create(card);
    lv_image_set_src(icon, &ui_img_ic_temp_png);
    lv_obj_set_pos(icon, 6, 2);

    /* montserrat_48, not the custom font2: font2 is a numerals-only subset
     * and silently renders nothing for the "--" placeholder. */
    s_ha_display_value[0] = _create_label(card, "--", &lv_font_montserrat_48, 0xECBF41);
    lv_obj_set_align(s_ha_display_value[0], LV_ALIGN_CENTER);
    lv_obj_set_pos(s_ha_display_value[0], -14, -20);

    lv_obj_t *unit = _create_label(card, CONFIG_HA_TEMP_UI_UNIT, &lv_font_montserrat_24, 0xECBF41);
    lv_obj_set_align(unit, LV_ALIGN_CENTER);
    lv_obj_set_pos(unit, 78, -30);

    lv_obj_t *name = _create_label(card, CONFIG_HA_TEMP_UI_NAME, &lv_font_montserrat_18, TEXT_MUTED);
    lv_obj_set_align(name, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(name, -5);

    /* Read-only: no touch callbacks; keep the slot populated so programmatic
     * switch-protocol updates for index 4 are silently absorbed. */
    slot->widget = card;
    slot->update = _update_momentary;
}

/* Half-height read-only stat row: muted name left, accented value right.
 * Two of these stacked with an 8 px gap equal one CARD_HEIGHT, aligning the
 * right column with the temperature card. */
static void _create_stat_row(lv_obj_t *tile, int32_t y, const char *name_text,
                             const lv_image_dsc_t *icon_src, uint32_t accent, int value_idx)
{
    lv_obj_t *card = lv_obj_create(tile);
    _style_panel(card, CARD_WIDTH, ROW_HEIGHT, 244, y);
    /* The default theme padding leaves ~50 px of content in a 78 px row —
     * not enough for two stacked labels. Use the full height. */
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *icon = lv_image_create(card);
    lv_image_set_src(icon, icon_src);
    lv_obj_set_align(icon, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(icon, 12);

    char placeholder[48];
    lv_snprintf(placeholder, sizeof(placeholder), "-- %s", display_units[value_idx]);
    s_ha_display_value[value_idx] = _create_label(card, placeholder, &lv_font_montserrat_24, accent);
    lv_obj_set_align(s_ha_display_value[value_idx], LV_ALIGN_TOP_MID);
    lv_obj_set_pos(s_ha_display_value[value_idx], 22, 10);

    /* Bottom-centered name, matching the temperature card's label style. */
    lv_obj_t *name = _create_label(card, name_text, &lv_font_montserrat_18, TEXT_MUTED);
    lv_obj_set_align(name, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_pos(name, 22, -6);
}

static void _create_slider_card(lv_obj_t *tile, switch_slot_t *slot)
{
    lv_obj_t *card = lv_button_create(tile);
    _style_panel(card, 440, ROW_HEIGHT, 22, 360);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *label = _create_label(card, switch_names[slot->index], &lv_font_montserrat_18, TEXT_MUTED);
    lv_obj_set_align(label, LV_ALIGN_BOTTOM_MID);

    lv_obj_t *dim = _create_label(card, LV_SYMBOL_MINUS, &lv_font_montserrat_18, TEXT_MUTED);
    lv_obj_set_pos(dim, 16, 14);

    lv_obj_t *bright = _create_label(card, LV_SYMBOL_PLUS, &lv_font_montserrat_18, TEXT_MUTED);
    lv_obj_set_align(bright, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(bright, -16, 14);

    lv_obj_t *slider = lv_slider_create(card);
    lv_slider_set_value(slider, 80, LV_ANIM_OFF);
    if (lv_slider_get_mode(slider) == LV_SLIDER_MODE_RANGE) {
        lv_slider_set_left_value(slider, 0, LV_ANIM_OFF);
    }
    lv_obj_set_size(slider, 330, 20);
    lv_obj_set_pos(slider, 50, 12);
    lv_obj_set_style_radius(slider, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(ARC_TRACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(SWITCH_GREEN), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_DEFAULT);

    slot->widget = slider;
    slot->update = _update_slider;
    lv_obj_add_event_cb(slider, _slider_event_cb, LV_EVENT_RELEASED, slot);
}

static void _init_slots(ha_switch_screen_t *s)
{
    for (int i = 0; i < SWITCH_SLOT_COUNT; i++) {
        s->slots[i] = (switch_slot_t){
            .index = i,
        };
    }
}

static void _create_general_tile(ha_switch_screen_t *s, lv_obj_t *tile)
{
    _create_header(tile, "General Controls");

    _create_all_lights_card(tile, &s->slots[3], 22, 96);
    /* Vertically centered beside the taller All Lights card. */
    _create_row_toggle(tile, &s->slots[5], 244, 139,
                       &ui_img_ic_switch1_on_png, &ui_img_ic_switch1_off_png);
    /* switch1-3 spares and the old placeholder sensor cards were dropped;
     * this page grows as real devices get mapped. */
}

static void _create_ctrl_tile(ha_switch_screen_t *s, lv_obj_t *tile)
{
    _create_header(tile, "Loft Controls");

    _create_loft_temp_card(tile, &s->slots[4]);
    /* Right column: two half-height stat rows aligned with the temp card
     * (96..174 and 182..260 vs the card's 96..260). */
    _create_stat_row(tile, 96, CONFIG_HA_HUMIDITY_UI_NAME, &ui_img_ic_hum_png, HUMIDITY_ACCENT, 1);
    _create_stat_row(tile, 182, CONFIG_HA_CO2_UI_NAME, &ui_img_ic_co2_png, 0xFFFFFF, 2);
    _create_embedded_switch(tile, &s->slots[6], 22, 268, 60, 28);
    _create_slider_card(tile, &s->slots[7]);
}

ha_switch_screen_t *ha_switch_screen_create(void)
{
    ha_switch_screen_t *s = &_instance;
    if (s->created) {
        return s;
    }

    memset(s, 0, sizeof(*s));
    _init_slots(s);

    lv_port_sem_take();
    lv_obj_t *ctrl_tile = nav_get_tile(NAV_TILE_HA_CTRL);
    lv_obj_t *mix_tile = nav_get_tile(NAV_TILE_HA_MIX);
    if (!ctrl_tile || !mix_tile) {
        ESP_LOGE(TAG, "HA switch nav tiles are not initialized");
        lv_port_sem_give();
        return s;
    }

    _create_ctrl_tile(s, ctrl_tile);
    _create_general_tile(s, mix_tile);
    s->created = true;
    lv_port_sem_give();

    return s;
}

void ha_switch_screen_update(ha_switch_screen_t *s, int index, int value)
{
    if (!s || index < 0 || index >= SWITCH_SLOT_COUNT) {
        ESP_LOGW(TAG, "invalid index %d", index);
        return;
    }

    switch_slot_t *slot = &s->slots[index];
    if (!slot->widget || !slot->update) {
        ESP_LOGW(TAG, "widget[%d] is NULL", index);
        return;
    }

    ESP_LOGI(TAG, "update index:%d value:%d", index, value);
    slot->update(slot->widget, value, slot->aux);
    /*
     * Programmatic (MQTT/restore) updates no longer fire VALUE_CHANGED, so
     * refresh the on/off toggle icon directly. No-op for slots without an icon
     * (arc, slider, embedded switch).
     */
    _set_toggle_icon(slot, value != 0);
}

void ha_switch_screen_destroy(ha_switch_screen_t *s)
{
    (void)s;
}
