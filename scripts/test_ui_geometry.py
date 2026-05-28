#!/usr/bin/env python3
"""Regression checks for fullscreen LVGL UI geometry."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "main"
NAV = MAIN / "nav/nav.c"


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


if __name__ == "__main__":
    unittest.main(verbosity=2)
