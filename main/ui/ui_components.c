#include "ui_components.h"
#include "ui_theme.h"

#include <stdbool.h>

/* ── Shared style transition ──────────────────────────────────────────────
 * A single ease-out transition drives every pressed/checked state change on
 * cards and buttons. The descriptor and its property list have static storage
 * because lv_obj_set_style_transition() keeps a pointer to them. Lazily
 * initialised so callers never depend on an explicit init ordering. */

static bool s_inited;
static lv_style_transition_dsc_t s_press_transition;

static const lv_style_prop_t s_press_props[] = {
    LV_STYLE_BG_COLOR,
    LV_STYLE_TRANSFORM_WIDTH,
    LV_STYLE_TRANSFORM_HEIGHT,
    LV_STYLE_BORDER_COLOR,
    (lv_style_prop_t)0, /* terminator */
};

static void ui_ensure_init(void)
{
    if (s_inited) {
        return;
    }
    lv_style_transition_dsc_init(&s_press_transition, s_press_props,
                                 lv_anim_path_ease_out, UI_MOTION_PRESS_MS, 0, NULL);
    s_inited = true;
}

/* ── Labels ───────────────────────────────────────────────────────────── */

lv_obj_t *ui_label(lv_obj_t *parent, const char *text,
                   const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    return label;
}

/* ── Cards ────────────────────────────────────────────────────────────── */

void ui_apply_card(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, UI_RADIUS_CARD, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, UI_COLOR_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, UI_COLOR_OUTLINE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_make_pressable(lv_obj_t *obj)
{
    ui_ensure_init();
    lv_obj_set_style_transition(obj, &s_press_transition, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, UI_COLOR_SURFACE_PRESSED, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    /* Subtle inward press: shrink the drawn box a couple of px each edge. */
    lv_obj_set_style_transform_width(obj, -4, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(obj, -4, LV_PART_MAIN | LV_STATE_PRESSED);
}

void ui_make_checkable(lv_obj_t *obj, lv_color_t accent)
{
    ui_ensure_init();
    lv_obj_set_style_transition(obj, &s_press_transition, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* Checked: brighter fill + accent hairline so "on" reads at a glance. */
    lv_obj_set_style_bg_color(obj, UI_COLOR_SURFACE_PRESSED, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(obj, accent, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
    /* Momentary press feedback while the finger is down. */
    lv_obj_set_style_bg_color(obj, UI_COLOR_SURFACE_PRESSED, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
}

/* ── Icon badge / chip ────────────────────────────────────────────────── */

lv_obj_t *ui_icon_badge(lv_obj_t *parent, const char *glyph,
                        const lv_font_t *font, lv_color_t accent, int32_t diameter)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_set_size(badge, diameter, diameter);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(badge, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *icon = ui_label(badge, glyph, font, accent);
    lv_obj_center(icon);
    return badge;
}

lv_obj_t *ui_chip(lv_obj_t *parent, const char *glyph,
                  const lv_font_t *glyph_font, const char *text)
{
    lv_obj_t *chip = lv_obj_create(parent);
    ui_apply_card(chip);
    ui_make_pressable(chip);
    lv_obj_set_style_pad_all(chip, UI_SPACE_SM, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Child 0: glyph. Recolouring on checked state is the caller's job (the
     * state lives on the chip object; children don't inherit LV_STATE). */
    lv_obj_t *icon = ui_label(chip, glyph, glyph_font, UI_COLOR_TEXT);
    lv_obj_set_align(icon, LV_ALIGN_TOP_MID);

    /* Child 1: caption, wrapping and centred at the bottom. */
    lv_obj_t *label = ui_label(chip, text, UI_FONT_LABEL, UI_COLOR_TEXT);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(label, LV_ALIGN_BOTTOM_MID);
    return chip;
}

/* ── Headers ──────────────────────────────────────────────────────────── */

static lv_obj_t *ui_header_bar(lv_obj_t *parent)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), UI_HEADER_HEIGHT);
    lv_obj_set_align(header, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    return header;
}

lv_obj_t *ui_header(lv_obj_t *parent, const char *title)
{
    lv_obj_t *header = ui_header_bar(parent);
    lv_obj_t *label = ui_label(header, title, UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    return header;
}

lv_obj_t *ui_back_button(lv_obj_t *parent, lv_event_cb_t cb, void *user_data)
{
    ui_ensure_init();
    lv_obj_t *back = lv_button_create(parent);
    lv_obj_set_size(back, 100, 50);
    lv_obj_set_pos(back, 10, 17);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(back, UI_COLOR_SURFACE_PRESSED, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(back, UI_RADIUS_BUTTON, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_transition(back, &s_press_transition, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (cb) {
        lv_obj_add_event_cb(back, cb, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *label = ui_label(back, LV_SYMBOL_LEFT " Back", LV_FONT_DEFAULT, UI_COLOR_TEXT);
    lv_obj_center(label);
    return back;
}

lv_obj_t *ui_modal_header(lv_obj_t *modal, const char *title,
                          lv_event_cb_t back_cb, void *back_user_data)
{
    lv_obj_t *header = ui_header_bar(modal);
    ui_back_button(header, back_cb, back_user_data);
    lv_obj_t *label = ui_label(header, title, UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_set_align(label, LV_ALIGN_BOTTOM_MID);
    return header;
}

/* ── Modals ───────────────────────────────────────────────────────────── */

lv_obj_t *ui_modal_create(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_obj_t *modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, lv_display_get_horizontal_resolution(disp),
                    lv_display_get_vertical_resolution(disp));
    lv_obj_set_align(modal, LV_ALIGN_CENTER);
    lv_obj_add_flag(modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(modal, UI_COLOR_BG, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(modal, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);
    return modal;
}

static void _opa_exec(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, LV_PART_MAIN);
}

static void _translate_exec(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, LV_PART_MAIN);
}

static void _anim_out_done(lv_anim_t *a)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    /* Reset so the next open starts from a clean, fully-opaque state. */
    lv_obj_set_style_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_translate_y(obj, 0, LV_PART_MAIN);
}

void ui_modal_anim_in(lv_obj_t *modal)
{
    lv_obj_set_style_opa(modal, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_translate_y(modal, 16, LV_PART_MAIN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, modal);
    lv_anim_set_duration(&a, UI_MOTION_MODAL_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

    lv_anim_set_exec_cb(&a, _opa_exec);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_start(&a);

    lv_anim_set_exec_cb(&a, _translate_exec);
    lv_anim_set_values(&a, 16, 0);
    lv_anim_start(&a);
}

void ui_modal_anim_out(lv_obj_t *modal)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, modal);
    lv_anim_set_duration(&a, UI_MOTION_MODAL_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

    lv_anim_set_exec_cb(&a, _translate_exec);
    lv_anim_set_values(&a, 0, 16);
    lv_anim_start(&a);

    lv_anim_set_exec_cb(&a, _opa_exec);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_ready_cb(&a, _anim_out_done);
    lv_anim_start(&a);
}
