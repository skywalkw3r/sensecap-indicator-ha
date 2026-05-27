# APP MODULE GUIDE

Scope: `main/app/`.

## Module Map

| Area | Files | Ownership |
| --- | --- | --- |
| Wi-Fi model | `indicator_wifi_model.c`, `indicator_wifi.h` | Wi-Fi scan/connect/state, NVS Wi-Fi config |
| Wi-Fi view | `indicator_wifi_view.c` | Wi-Fi list, password UI, connect feedback |
| MQTT controller | `indicator_mqtt.c`, `indicator_mqtt.h` | Shared MQTT app event loop and instance lifecycle |
| Home Assistant model | `indicator_ha_model.c`, `indicator_ha.h` | HA entities, MQTT payload handling, switch state persistence |
| Home Assistant view | `indicator_ha_view.c` | HA controls and broker IP screen interactions |
| Display model | `indicator_display_model.c`, `indicator_display.h` | Brightness, sleep mode, screen control |
| Display view | `indicator_display_view.c` | Display settings UI controls |
| Sensor model | `indicator_sensor_model.c`, `indicator_sensor.h` | Built-in sensor cache and RP2040 packet parsing |
| Sensor view | `indicator_sensor_view.c` | Sensor labels on screen |
| RP2040 link | `esp32_rp2040.c`, `esp32_rp2040.h` | UART/COBS ingress and sensor shutdown command |
| Commands | `indicator_cmd.c`, `indicator_cmd.h` | Console commands |
| Storage | `indicator_storage_nvs.c`, `indicator_storage_nvs.h` | NVS read/write helpers |

## Rules

- View files own LVGL object updates.
- Model files own state, persistence, protocol parsing, and network behavior.
- Do not add new direct `ui.h` includes to model files.
- Keep MQTT topics and Home Assistant JSON payload compatibility stable.
- Preserve existing switch indexes and sensor key mapping.
- RP2040 packet parsing should stay in the current ESP32S3 ingress path unless a scoped protocol migration exists.
- Stage 1 architecture checks may allowlist existing debt. Do not expand the allowlist without documenting why.

## Known Current Debt

- `indicator_ha_model.c` currently includes `ui.h`; Stage 1 should allowlist this instead of changing runtime behavior.
- `view_data.h` currently mixes event ids, payload structs, sensor definitions, BSP includes, and logging includes.
- Some event handlers update LVGL directly while holding the LVGL semaphore. Stage 2 should converge this behind one UI boundary.

## Safe Change Pattern

1. Identify whether the change belongs to a model file or a view file.
2. Keep runtime behavior compatible.
3. Run `python3 scripts/architecture_scan.py` after that later Stage 1 check exists.
4. Run `python3 scripts/dev_check.py` after that later Stage 1 check exists, or `sh build.sh` if ESP-IDF is available.
5. Document manual hardware checks when behavior changes.
