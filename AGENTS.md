# PROJECT KNOWLEDGE BASE

Scope: SenseCAP Indicator Home Assistant firmware, with emphasis on the ESP32S3 screen-side code.

## Stack

- ESP-IDF 5.4.x
- ESP32-S3 owns display, touch, Wi-Fi, MQTT, Home Assistant control logic, command console, display settings, and UART ingress from RP2040.
- RP2040 owns onboard/Grove sensor acquisition.
- LVGL UI is currently generated from SquareLine under `main/ui/`.

## Starter Project Guidance

The starter project at `/Users/spencer/Seeed/dev/indicator/sensecap-indicator-starter` is a reference for Agent-Driven workflow, not a source of driver code for this firmware.

Use these starter ideas:

- Agent-readable project knowledge files.
- Explicit module boundaries.
- Small verification scripts.
- A path toward no-hardware checks.

Do not copy starter BSP/display/touch/RP2040 driver code into this project unless a later scoped test proves a specific replacement is necessary. This repository's working firmware behavior is the source of truth.

## Boot Sequence

Start at `main/main.c`:

1. `bsp_board_init()`
2. `lv_port_init()`
3. create `view_event_handle`
4. `indicator_view_init()`
5. `indicator_model_init()`

View initialization starts SquareLine UI and view handlers. Model initialization starts storage, button, display, RP2040, sensor, command, Wi-Fi, MQTT, and Home Assistant logic.

## Where To Look

| Task | Start Here |
| --- | --- |
| Boot order | `main/main.c` |
| Runtime module wiring | `main/indicator_model.c`, `main/indicator_view.c` |
| Shared event ids and payloads | `main/view_data.h` |
| SquareLine UI objects | `main/ui/ui.h`, `main/ui/ui.c`, `main/ui/screens/` |
| LVGL port and locking | `main/lv_port.h`, `main/lv_port.c` |
| Wi-Fi model/view | `main/app/indicator_wifi_model.c`, `main/app/indicator_wifi_view.c` |
| MQTT controller | `main/app/indicator_mqtt.c`, `main/app/indicator_mqtt.h` |
| Home Assistant model/view | `main/app/indicator_ha_model.c`, `main/app/indicator_ha_view.c` |
| Display settings | `main/app/indicator_display_model.c`, `main/app/indicator_display_view.c` |
| RP2040 UART ingress | `main/app/esp32_rp2040.c` |
| Built-in sensor cache/parser | `main/app/indicator_sensor_model.c`, `main/app/indicator_sensor_view.c` |
| Future local development checks | Later Stage 1: `scripts/dev_check.py` |

## Architecture Rules

- Preserve current product behavior unless a task explicitly says otherwise.
- ESP32S3 receives sensor data from RP2040. Do not add direct Grove drivers on ESP32S3.
- SquareLine-generated UI files under `main/ui/` are generated assets. Avoid hand-editing generated files unless the task is specifically about the generated UI.
- View files may include `ui.h` and call LVGL APIs.
- Model files should not gain new LVGL object ownership.
- Background tasks and event handlers must update LVGL through the documented LVGL lock/deferral pattern for the current code.
- MQTT topics and Home Assistant payload compatibility are product behavior.
- Keep runtime refactors incremental; this project should remain buildable after each patch.

## Verification

Stage 1 target local checks, added by later Stage 1 tasks:

```bash
python3 scripts/dev_check.py --skip-build
python3 scripts/dev_check.py
```

The architecture-only fallback is also added later in Stage 1:

```bash
python3 scripts/architecture_scan.py
```

Until those scripts exist, the valid current check is the firmware build when ESP-IDF is available:

```bash
sh build.sh
```

`build.sh` currently deletes `build/` before building. Do not change that behavior as part of Stage 1 unless the user approves it.
