# ESP32S3 SCREEN-SIDE GUIDE

Scope: `main/` and child modules for the ESP32S3 side of the Home Assistant firmware.

## Current Runtime Shape

`main/main.c` initializes hardware, LVGL, and a dedicated `view_event_handle`.

`main/indicator_view.c` creates the SquareLine UI with `ui_init()` and registers view-side modules.

`main/indicator_model.c` initializes model/service modules:

- NVS storage
- button handling
- display/backlight settings
- RP2040 UART ingress
- built-in sensor model
- console commands
- Wi-Fi model
- MQTT controller
- Home Assistant model

## Rules

- Do not replace `bsp_board_init()` or `lv_port_init()` with starter project code.
- Treat `main/ui/` as SquareLine-generated UI.
- View modules may include `ui.h` and mutate LVGL objects.
- Model modules should own state, persistence, protocol parsing, MQTT, Wi-Fi, and hardware-facing logic.
- Do not add new UI object ownership to model modules.
- Keep `view_event_handle` as the current event loop until a scoped Stage 2 event migration exists.
- Keep event payloads fixed-size unless the lifetime rule is documented next to the event id.
- Prefer adding small helpers over broad rewrites.

## LVGL Threading

Current code uses `lv_port_sem_take()` and `lv_port_sem_give()` around LVGL calls. Follow the local pattern for Stage 1.

Future Stage 2 work may add an `app_ui_post()` helper. Do not invent another UI deferral API in Stage 1.

## Generated UI

SquareLine files live in:

- `main/ui/ui.h`
- `main/ui/ui.c`
- `main/ui/ui_events.c`
- `main/ui/screens/`
- `main/ui/images/`
- `main/ui/fonts/`

Do not hand-edit generated files for architecture-only tasks.

## Verification

Later Stage 1 checks will include:

```bash
python3 scripts/architecture_scan.py
```

and:

```bash
python3 scripts/dev_check.py
```

Until those scripts exist, run the current build wrapper when ESP-IDF is available:

```bash
sh build.sh
```
