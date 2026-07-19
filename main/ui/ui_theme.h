#ifndef UI_THEME_H
#define UI_THEME_H

/*
 * Shared design tokens for the SenseCAP Indicator UI.
 *
 * This is the single source of truth for colour, radius, spacing, type and
 * motion across every domain view/screen. Domains consume these tokens instead
 * of hard-coding literals so the whole panel stays visually consistent and a
 * palette tweak lands in one place.
 *
 * Presentation infrastructure only — no MQTT/NVS/model state lives here.
 */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Palette (dark) — raw 24-bit hex ──────────────────────────────────── */
#define UI_HEX_BG              0x101418  /* app background (unchanged) */
#define UI_HEX_SURFACE         0x1C2027  /* card / panel fill */
#define UI_HEX_SURFACE_PRESSED 0x252B34  /* pressed / active / checked fill */
#define UI_HEX_OUTLINE         0x2A313B  /* hairline card border */
#define UI_HEX_TEXT            0xF2F5F7  /* primary text */
#define UI_HEX_TEXT_MUTED      0x9AA4AE  /* secondary / label text */

/* Functional accents — hues preserved from the product design. */
#define UI_HEX_GREEN           0x529D53  /* on / success (switch, slider) */
#define UI_HEX_AMBER           0xECBF41  /* temperature */
#define UI_HEX_BLUE            0x52AAE5  /* humidity */
#define UI_HEX_RED             0xD9534F  /* destructive / All Lights */
#define UI_HEX_PRIMARY         0x4EACE4  /* primary accent (theme seed) */
#define UI_HEX_CORAL           0xE57373  /* room accent (Loft) — softer than
                                          * UI_HEX_RED, which stays destructive */

/* ── lv_color_t convenience wrappers ──────────────────────────────────── */
#define UI_COLOR_BG              lv_color_hex(UI_HEX_BG)
#define UI_COLOR_SURFACE         lv_color_hex(UI_HEX_SURFACE)
#define UI_COLOR_SURFACE_PRESSED lv_color_hex(UI_HEX_SURFACE_PRESSED)
#define UI_COLOR_OUTLINE         lv_color_hex(UI_HEX_OUTLINE)
#define UI_COLOR_TEXT            lv_color_hex(UI_HEX_TEXT)
#define UI_COLOR_TEXT_MUTED      lv_color_hex(UI_HEX_TEXT_MUTED)
#define UI_COLOR_GREEN           lv_color_hex(UI_HEX_GREEN)
#define UI_COLOR_AMBER           lv_color_hex(UI_HEX_AMBER)
#define UI_COLOR_BLUE            lv_color_hex(UI_HEX_BLUE)
#define UI_COLOR_RED             lv_color_hex(UI_HEX_RED)
#define UI_COLOR_PRIMARY         lv_color_hex(UI_HEX_PRIMARY)
#define UI_COLOR_CORAL           lv_color_hex(UI_HEX_CORAL)

/* ── Radius / spacing scale ───────────────────────────────────────────── */
#define UI_RADIUS_CARD    16   /* cards / panels */
#define UI_RADIUS_BUTTON  10   /* small buttons */

#define UI_SPACE_XS   4
#define UI_SPACE_SM   8
#define UI_SPACE_MD   12
#define UI_SPACE_LG   16
#define UI_SPACE_XL   24

#define UI_HEADER_HEIGHT 85   /* page / modal title bar height */

/* ── Type scale ───────────────────────────────────────────────────────── */
/* Built-in Montserrat faces (enabled in lv_conf.h / sdkconfig). The custom
 * asset fonts (ui_font_font0/2) are numeral-biased subsets that render blank
 * for placeholder glyphs like "--", so titles/labels use Montserrat. */
#define UI_FONT_TITLE   (&lv_font_montserrat_24)  /* page / modal titles */
#define UI_FONT_LABEL   (&lv_font_montserrat_18)  /* card labels */
#define UI_FONT_VALUE   (&lv_font_montserrat_48)  /* large numeric values */
#define UI_FONT_BODY    (&lv_font_montserrat_20)  /* body / list text */

/* ── Motion ───────────────────────────────────────────────────────────── */
#define UI_MOTION_PRESS_MS  140   /* pressed/checked style transition (ease-out) */
#define UI_MOTION_MODAL_MS  250   /* modal fade + subtle rise */
#define UI_MOTION_SNAP_MS   200   /* tileview snap animation */

/*
 * Install the tuned dark theme on `display`. Centralises the palette seed and
 * default font so every screen inherits one base look. Replaces a raw
 * lv_theme_default_init() call at the nav layer. Returns the installed theme.
 * Caller must hold the LVGL lock.
 */
lv_theme_t *ui_theme_install(lv_display_t *display);

#ifdef __cplusplus
}
#endif

#endif /* UI_THEME_H */
