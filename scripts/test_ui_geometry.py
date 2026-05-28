#!/usr/bin/env python3
"""Regression checks for fullscreen LVGL UI geometry."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "main"
NAV = MAIN / "nav/nav.c"
NAV_HEADER = MAIN / "nav/nav.h"
WIFI_VIEW = MAIN / "wifi/wifi_view.c"
SETTINGS_VIEW = MAIN / "settings/settings_view.c"


class UiGeometryTests(unittest.TestCase):
    def test_no_fullscreen_container_uses_stale_800px_height(self) -> None:
        offenders: list[str] = []
        for path in MAIN.rglob("*.c"):
            text = path.read_text()
            if "480, 800" in text or "#define NAV_TILE_HEIGHT 800" in text:
                offenders.append(str(path.relative_to(ROOT)))

        self.assertEqual([], offenders)

    def test_nav_uses_configured_screen_geometry_and_dark_theme(self) -> None:
        text = NAV.read_text()

        self.assertIn("#define NAV_TILE_WIDTH  CONFIG_LCD_EVB_SCREEN_WIDTH", text)
        self.assertIn("#define NAV_TILE_HEIGHT CONFIG_LCD_EVB_SCREEN_HEIGHT", text)
        self.assertIn("lv_theme_default_init", text)
        self.assertIn("lv_display_set_theme", text)

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

    def test_settings_is_gear_modal_not_fourth_tile(self) -> None:
        nav_header = NAV_HEADER.read_text()
        settings_text = SETTINGS_VIEW.read_text()

        self.assertIn("#define NAV_TILE_COUNT    3", nav_header)
        self.assertNotIn("NAV_TILE_SETTINGS", nav_header)
        self.assertIn("LV_SYMBOL_SETTINGS", settings_text)
        self.assertIn("lv_obj_set_align(button, LV_ALIGN_TOP_LEFT)", settings_text)
        self.assertIn("lv_obj_add_flag(s_settings_modal, LV_OBJ_FLAG_CLICKABLE)", settings_text)
        self.assertIn("LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE", settings_text)
        self.assertIn("SCREEN_DISPLAY_MODAL", settings_text)
        self.assertIn("SCREEN_BROKER_MODAL", settings_text)

    def test_temperature_unit_uses_product_degree_symbol(self) -> None:
        offenders: list[str] = []
        for path in MAIN.rglob("*.c"):
            text = path.read_text()
            if "deg C" in text:
                offenders.append(str(path.relative_to(ROOT)))

        self.assertEqual([], offenders)

    def test_wifi_back_button_uses_touch_friendly_modal_pattern(self) -> None:
        text = WIFI_VIEW.read_text()

        self.assertIn("lv_obj_move_foreground(s_wifi_modal)", text)
        self.assertIn("lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_CLICKABLE)", text)
        self.assertIn("LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE", text)
        self.assertIn("lv_obj_set_size(back, 100, 50)", text)
        self.assertIn("lv_obj_set_pos(back, 10, 17)", text)
        self.assertIn("lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP", text)
        self.assertIn("LV_STATE_PRESSED", text)
        self.assertIn('lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back")', text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
