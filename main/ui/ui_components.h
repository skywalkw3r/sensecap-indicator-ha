#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

/*
 * Shared widget constructors + style helpers built on the ui_theme tokens.
 *
 * These replace the per-file duplicated card/label/header/modal code that used
 * to live in each domain screen. Everything here is pure presentation; callers
 * still own their widget handles, event wiring and layout. All functions must
 * be called with the LVGL lock held (same contract as any *_view.c code).
 */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Label constructor: text + font + colour, fully opaque. */
lv_obj_t *ui_label(lv_obj_t *parent, const char *text,
                   const lv_font_t *font, lv_color_t color);

/* Apply the standard surface-card look (fill, card radius, hairline border, no
 * shadow, non-scrollable) to an existing object. Interactivity is layered on
 * separately via ui_make_pressable / ui_make_checkable. */
void ui_apply_card(lv_obj_t *obj);

/* Layer press feedback on an interactive card/button: pressed-state fill +
 * subtle shrink, animated with the shared ease-out transition. */
void ui_make_pressable(lv_obj_t *obj);

/* Layer checked-state feedback (brighter fill + `accent` hairline) plus press
 * feedback on a checkable card. */
void ui_make_checkable(lv_obj_t *obj, lv_color_t accent);

/* Accent-tinted circular badge (soft accent fill, accent glyph) holding one
 * MDI icon. Non-clickable so taps fall through to the card underneath. */
lv_obj_t *ui_icon_badge(lv_obj_t *parent, const char *glyph,
                        const lv_font_t *font, lv_color_t accent, int32_t diameter);

/* Pressable chip: MDI glyph on top, caption underneath (wraps, centred).
 * Child order contract: 0 = glyph label, 1 = caption label. Caller sizes the
 * chip, wires LV_EVENT_* and (for toggles) layers ui_make_checkable on top. */
lv_obj_t *ui_chip(lv_obj_t *parent, const char *glyph,
                  const lv_font_t *glyph_font, const char *text);

/* Themed confirm dialog (title + text + coloured OK button + Cancel), animated
 * in. on_confirm(user_data) runs after the dialog closes on OK; Cancel just
 * closes. One dialog at a time — returns NULL (and does nothing) if a confirm
 * is already open. LVGL task context (input callbacks), like all of ui_*. */
typedef void (*ui_confirm_cb_t)(void *user_data);
lv_obj_t *ui_confirm_msgbox(const char *title, const char *text,
                            const char *ok_label, lv_color_t ok_color,
                            ui_confirm_cb_t on_confirm, void *user_data);

/* Transparent full-width header bar (UI_HEADER_HEIGHT tall) with a centred
 * title. For the swipeable page tiles. Returns the header container. */
lv_obj_t *ui_header(lv_obj_t *parent, const char *title);

/* Standard "‹ Back" button (transparent, animated pressed fill, rounded). */
lv_obj_t *ui_back_button(lv_obj_t *parent, lv_event_cb_t cb, void *user_data);

/* Modal title bar: a ‹ Back button (top-left) + bottom-aligned title. Dedups
 * the identical header block across the settings/wifi/display modals. Returns
 * the header container. */
lv_obj_t *ui_modal_header(lv_obj_t *modal, const char *title,
                          lv_event_cb_t back_cb, void *back_user_data);

/* Full-screen modal overlay on lv_layer_top(): background fill, clickable,
 * non-scrollable, gesture-isolated, initially hidden. Returns the overlay. */
lv_obj_t *ui_modal_create(void);

/* Animate a modal in (fade + subtle rise). Caller un-hides + foregrounds it
 * first (so its own show-time bookkeeping runs). */
void ui_modal_anim_in(lv_obj_t *modal);

/* Animate a modal out (fade + sink), adding LV_OBJ_FLAG_HIDDEN when done and
 * restoring opacity/offset for the next open. */
void ui_modal_anim_out(lv_obj_t *modal);

#ifdef __cplusplus
}
#endif

#endif /* UI_COMPONENTS_H */
