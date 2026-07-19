#!/usr/bin/env python3
"""Regression checks for fullscreen LVGL UI geometry and the shared UI system.

These tests assert *intent*, not incidental literals: e.g. "a dark theme is
installed through the ui/ design-system module" rather than "nav.c calls
lv_theme_default_init directly". When the design system centralises a pattern,
the assertion follows it into ui/ instead of being deleted.
"""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "main"
NAV = MAIN / "nav/nav.c"
NAV_HEADER = MAIN / "nav/nav.h"
WIFI_VIEW = MAIN / "wifi/wifi_view.c"
SETTINGS_VIEW = MAIN / "settings/settings_view.c"
DISPLAY_VIEW = MAIN / "display/display_view.c"
UI_THEME_C = MAIN / "ui/ui_theme.c"
UI_THEME_H = MAIN / "ui/ui_theme.h"
UI_COMPONENTS_C = MAIN / "ui/ui_components.c"


class UiGeometryTests(unittest.TestCase):
    def test_no_fullscreen_container_uses_stale_800px_height(self) -> None:
        offenders: list[str] = []
        for path in MAIN.rglob("*.c"):
            text = path.read_text()
            if "480, 800" in text or "#define NAV_TILE_HEIGHT 800" in text:
                offenders.append(str(path.relative_to(ROOT)))

        self.assertEqual([], offenders)

    def test_nav_installs_dark_theme_via_ui_module(self) -> None:
        text = NAV.read_text()
        theme = UI_THEME_C.read_text()

        # nav still owns the fullscreen tile geometry ...
        self.assertIn("#define NAV_TILE_WIDTH  CONFIG_LCD_EVB_SCREEN_WIDTH", text)
        self.assertIn("#define NAV_TILE_HEIGHT CONFIG_LCD_EVB_SCREEN_HEIGHT", text)

        # ... but the theme now installs through the shared design-system module,
        # not a raw lv_theme_default_init() at the nav layer.
        self.assertIn("ui_theme_install", text)
        self.assertNotIn("lv_theme_default_init", text)

        # The ui/ module is the single place that seeds the palette + default font.
        self.assertIn("lv_theme_default_init", theme)
        self.assertIn("lv_display_set_theme", theme)

    def test_tileview_remains_scrollable_for_touch_swipes(self) -> None:
        text = NAV.read_text()

        self.assertIn("nav_style_base_obj(s_tileview)", text)
        self.assertNotIn("nav_style_static_obj(s_tileview)", text)

    def test_tileview_disables_elastic_offset_without_blocking_edge_swipes(self) -> None:
        text = NAV.read_text()

        self.assertIn("LV_OBJ_FLAG_SCROLL_ELASTIC", text)
        self.assertIn("LV_OBJ_FLAG_SCROLL_MOMENTUM", text)
        self.assertIn("LV_OBJ_FLAG_SCROLL_CHAIN", text)
        self.assertIn("LV_DIR_LEFT | LV_DIR_RIGHT", text)
        self.assertNotIn("nav_tile_scroll_dir", text)

    def test_nav_tunes_tileview_snap_animation(self) -> None:
        text = NAV.read_text()

        # Crisp page snap: nav overrides the pending scroll animation through the
        # LV_EVENT_SCROLL_BEGIN hook (LVGL passes the lv_anim_t* as the event
        # param) instead of leaving the distance-scaled default.
        self.assertIn("LV_EVENT_SCROLL_BEGIN", text)
        self.assertIn("UI_MOTION_SNAP_MS", text)
        self.assertIn("lv_anim_set_duration", text)

    def test_design_system_module_provides_shared_primitives(self) -> None:
        tokens = UI_THEME_H.read_text()
        comp = UI_COMPONENTS_C.read_text()

        # Design tokens live in one header.
        for token in (
            "UI_COLOR_SURFACE",
            "UI_COLOR_OUTLINE",
            "UI_RADIUS_CARD",
            "UI_MOTION_PRESS_MS",
            "UI_MOTION_MODAL_MS",
        ):
            self.assertIn(token, tokens)

        # Shared constructors + motion helpers replace the per-file duplication.
        for sym in (
            "ui_apply_card",
            "ui_make_pressable",
            "ui_make_checkable",
            "ui_modal_create",
            "ui_modal_header",
            "ui_back_button",
            "ui_modal_anim_in",
            "ui_modal_anim_out",
        ):
            self.assertIn(sym, comp)

        # Pressed/checked feedback is a real ease-out style transition, not an
        # instantaneous state swap.
        self.assertIn("lv_style_transition_dsc_init", comp)
        self.assertIn("lv_anim_path_ease_out", comp)

    def test_shared_modal_overlay_keeps_touch_invariants(self) -> None:
        comp = UI_COMPONENTS_C.read_text()

        # ui_modal_create() is the one place the fullscreen overlay invariants
        # live now (clickable so taps don't fall through, gesture-isolated so a
        # swipe can't leak to the tileview, hidden until shown).
        start = comp.index("lv_obj_t *ui_modal_create")
        end = comp.index("static void _opa_exec")
        body = comp[start:end]
        self.assertIn("LV_OBJ_FLAG_CLICKABLE", body)
        self.assertIn("LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE", body)
        self.assertIn("LV_OBJ_FLAG_HIDDEN", body)

    def test_settings_is_gear_modal_not_fourth_tile(self) -> None:
        nav_header = NAV_HEADER.read_text()
        settings_text = SETTINGS_VIEW.read_text()

        # Intent: settings is a modal, never a nav tile. Assert the invariant
        # (no settings tile, tile count stays bounded) rather than a literal
        # count, so legitimate tile removals don't trip this. 6 = dashboard
        # home + four room pages + trends.
        count_match = re.search(r"#define NAV_TILE_COUNT\s+(\d+)", nav_header)
        self.assertIsNotNone(count_match, "NAV_TILE_COUNT missing from nav.h")
        self.assertLessEqual(int(count_match.group(1)), 6)
        self.assertNotIn("NAV_TILE_SETTINGS", nav_header)
        self.assertIn("LV_SYMBOL_SETTINGS", settings_text)
        self.assertIn("lv_obj_set_align(button, LV_ALIGN_TOP_LEFT)", settings_text)

        # The modal is built through the shared overlay constructor (which owns
        # the clickable + gesture-isolated invariants — see the ui_modal_create
        # test above) rather than re-declaring those flags inline.
        self.assertIn("ui_modal_create()", settings_text)
        self.assertIn("SCREEN_DISPLAY_MODAL", settings_text)
        self.assertIn("SCREEN_BROKER_MODAL", settings_text)

    def test_settings_modal_preserves_subscreen_back_stack_and_shows_ip(self) -> None:
        settings_text = SETTINGS_VIEW.read_text()

        self.assertIn('#include "esp_netif.h"', settings_text)
        self.assertIn("static lv_obj_t *s_settings_ip_label = NULL;", settings_text)
        self.assertIn("settings_refresh_ip_label();", settings_text)
        self.assertIn('esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")', settings_text)
        self.assertIn('"IP: not connected"', settings_text)

        post_screen_start = settings_text.index("static void settings_post_screen")
        post_screen_end = settings_text.index("static void settings_open_wifi")
        post_screen_body = settings_text[post_screen_start:post_screen_end]
        self.assertNotIn("settings_hide_modal();", post_screen_body)
        self.assertIn("esp_event_post_to", post_screen_body)

    def test_settings_wifi_display_modals_animate_via_ui_module(self) -> None:
        # Every reused fullscreen modal is built + faded in through the shared
        # design-system helpers, so motion is consistent and defined once.
        for path in (SETTINGS_VIEW, WIFI_VIEW, DISPLAY_VIEW):
            text = path.read_text()
            self.assertIn("ui_modal_create()", text,
                          f"{path.name} should build its modal via ui_modal_create")
            self.assertIn("ui_modal_anim_in", text,
                          f"{path.name} should fade its modal in via ui_modal_anim_in")

    def test_temperature_unit_uses_product_degree_symbol(self) -> None:
        offenders: list[str] = []
        for path in MAIN.rglob("*.c"):
            text = path.read_text()
            if "deg C" in text:
                offenders.append(str(path.relative_to(ROOT)))

        self.assertEqual([], offenders)

    def test_wifi_modal_uses_shared_touch_friendly_pattern(self) -> None:
        text = WIFI_VIEW.read_text()
        comp = UI_COMPONENTS_C.read_text()

        # The modal + its "‹ Back" affordance now come from the shared
        # constructors; wifi_view keeps only the behavioural move-to-front.
        self.assertIn("ui_modal_create()", text)
        self.assertIn('ui_modal_header(s_wifi_modal, "Wi-Fi"', text)
        self.assertIn("lv_obj_move_foreground(s_wifi_modal)", text)

        # The touch-friendly back button (target size, pressed feedback, label)
        # is encoded once in the shared constructor.
        self.assertIn("lv_obj_set_size(back, 100, 50)", comp)
        self.assertIn("lv_obj_set_pos(back, 10, 17)", comp)
        self.assertIn("LV_STATE_PRESSED", comp)
        self.assertIn('lv_label_set_text', comp)
        self.assertIn('LV_SYMBOL_LEFT " Back"', comp)

    def test_wifi_back_during_scan_discards_late_scan_result(self) -> None:
        text = WIFI_VIEW.read_text()

        self.assertIn("static bool s_discard_next_list = false;", text)
        self.assertIn("static bool s_wifi_scan_pending = false;", text)
        self.assertIn("s_wifi_scan_pending = true;", text)
        self.assertIn("s_wifi_scan_pending = false;", text)
        self.assertIn("discard stale scan result", text)

        hide_start = text.index("static void _hide_wifi_modal")
        hide_end = text.index("static void _on_wifi_modal_back")
        hide_body = text[hide_start:hide_end]
        self.assertIn("if(s_wifi_scan_pending)", hide_body)
        self.assertIn("s_discard_next_list = true;", hide_body)

        list_case = text[text.index("case VIEW_EVENT_WIFI_LIST:"):]
        self.assertLess(
            list_case.index("if(s_discard_next_list)"),
            list_case.index("wifi_list_screen_update"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
