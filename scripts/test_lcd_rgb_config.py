#!/usr/bin/env python3
"""Regression checks for the RGB LCD + esp_lvgl_port integration."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parent.parent
BSP_LCD = ROOT / "components/bsp/src/peripherals/bsp_lcd.c"
LV_PORT = ROOT / "main/lv_port.c"


class LcdRgbConfigTests(unittest.TestCase):
    def test_rgb_panel_does_not_force_refresh_on_demand(self) -> None:
        text = BSP_LCD.read_text()

        self.assertNotIn(
            ".flags.refresh_on_demand = 1",
            text,
            "esp_lvgl_port owns RGB refresh now; forcing refresh_on_demand "
            "without its own refresh task can leave the screen stale.",
        )

    def test_bsp_does_not_own_rgb_refresh_loop(self) -> None:
        text = BSP_LCD.read_text()

        self.assertNotIn("esp_lcd_rgb_panel_refresh(", text)
        self.assertNotIn("xTaskCreate(lcd_task", text)
        self.assertNotIn("esp_lcd_rgb_panel_register_event_callbacks", text)

    def test_lvgl_task_stack_is_large_enough_for_lvgl9_rgb(self) -> None:
        text = LV_PORT.read_text()
        match = re.search(r"#define\s+LV_PORT_TASK_STACK_SIZE\s+\((\d+)\)", text)

        self.assertIsNotNone(match)
        assert match is not None
        self.assertGreaterEqual(int(match.group(1)), 8192)


if __name__ == "__main__":
    unittest.main(verbosity=2)
