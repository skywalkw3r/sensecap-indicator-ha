# Firmware Architecture

SenseCAP Indicator firmware for ESP32-S3 + RP2040. ESP-IDF v5.4, FreeRTOS, LVGL 8.x, esp-mqtt.

---

## Directory Map

```
main/
  main.c                  Entry point. Creates view_event_handle, calls model init then view init.
  indicator_model.c       Model init orchestrator (feature-flag guarded includes).
  indicator_view.c        View init orchestrator (feature-flag guarded includes).
  indicator_enabler.h     Aggregates all module headers; define-guards drive conditional init.
  view_data.h             Shared event bus contract: all VIEW_EVENT_* definitions + event manifest.
  lv_port.c               LVGL display/touch HAL. Owns lv_port_sem_take/give (LVGL thread safety).

  app/                    Feature modules (MVC style — gradual migration to vertical slices).
    indicator_wifi_*      WiFi scan, connect, status. Largest module (517L model, 626L view).
    indicator_sensor_*    Sensor data from RP2040 via UART/COBS.
    indicator_display_*   Display brightness, sleep mode.
    indicator_mqtt.*      MQTT client lifecycle controller (broker-agnostic).
    indicator_btn.*       Physical button handling.
    indicator_cmd.*        Serial command interface.
    indicator_storage_nvs.* NVS read/write helpers.
    esp32_rp2040.*        UART/COBS communication with RP2040 co-processor.

  ha/                     Home Assistant domain — vertical slice architecture (pilot).
    ha.h                  Public API for the entire HA domain.
    ha_mqtt.c             MQTT client lifecycle for HA broker. Owns mqtt_ha_instance.
    ha_config.c           Broker NVS config + IP display UI (owns ha_cfg_get/set).
    ha_sensor.c           External HA sensor subscribe/publish.
    ha_switch.c           Switch entity state, MQTT pub/sub, NVS persistence.
    ha_switch_screen.c    LVGL widget component for 8 switch widgets. Only HA file touching ui.h.
    ha_switch_screen.h    Public interface: create / update / destroy.

  ui/                     Squareline Studio generated code — being phased out.
    ui.c / ui.h           Widget globals (lv_obj_t *ui_xxx) and screen init functions.
    ui_events.c           LVGL event callbacks → post VIEW_EVENT_* to event bus.
    ui_helpers.c          Screen transition helpers (lv_scr_load_anim wrappers).
    screens/              Per-screen widget construction (generated, do not edit).
    fonts/ images/        Asset arrays (safe to keep after Squareline removal).

  util/
    cobs.*                COBS encode/decode for RP2040 UART framing.
    indicator_util.*      IP address helpers (extract_ip_from_url, assemble_broker_url).
```

---

## Boot Sequence

```
main()
  1. esp_event_loop_create_default()         // System event loop (WiFi driver)
  2. esp_event_loop_create(view_event_handle) // Main UI/data bus
  3. indicator_model_init()
       nvs_init → btn_init → display_init
       → esp32_rp2040_init → sensor_init
       → cmd_init
       → wifi_model_init → mqtt_init → ha_model_init
  4. indicator_view_init()
       ui_init()                              // LVGL screens created HERE
       → sensor_view_init
       → wifi_view_init
       → ha_view_init                         // ha_switch_screen_create() called here
  5. LVGL task loop (lv_port.c)
```

**Important**: `ha_model_init` runs before `ui_init`. Screen components (e.g. `ha_switch_screen_create`) must be called from the view init path, not model init.

---

## Event Loops

| Handle | Base | Created in | Purpose |
|--------|------|------------|---------|
| `view_event_handle` | `VIEW_EVENT_BASE` | `main.c` | Main UI/data bus — all cross-module data flow |
| `mqtt_app_event_handle` | `MQTT_APP_EVENT_BASE` | `indicator_mqtt.c` | MQTT client lifecycle commands (start/stop/restart) |
| `ha_cfg_event_handle` | `HA_CFG_EVENT_BASE` | `ha/ha_mqtt.c` | HA broker config changes |
| `cmd_cfg_event_handle` | `CMD_CFG_EVENT_BASE` | `indicator_cmd.c` | Serial command events |
| *(default)* | `WIFI_EVENT`, `IP_EVENT` | `indicator_wifi_model.c` | ESP-IDF WiFi driver events |

**Full event manifest**: see comments in `view_data.h` — each `VIEW_EVENT_*` documents producer, consumer, payload, and LVGL lock requirement.

---

## LVGL Thread Safety

LVGL is not thread-safe. Any code touching widget state outside the LVGL task must hold the semaphore:

```c
lv_port_sem_take();
// ... lv_obj_* calls ...
lv_port_sem_give();
```

Files that currently hold this lock: `indicator_wifi_view.c`, `indicator_sensor_view.c`, `ha/ha_config.c`, `ha/ha_switch.c`, `indicator_view.c`.

Rule: if your event handler calls any `lv_*` function, wrap it in the semaphore pair.

---

## Architectural Patterns

### Current (app/): MVC horizontal layers
Model file posts events → View file consumes and updates widgets. Event bus is the only coupling.

### Target (ha/): Vertical slices
Each feature domain (config, sensor, switch) owns its full stack: data + MQTT + UI component.
No cross-domain event hops for in-domain state changes.

### Screen components (ha_switch_screen.c pattern)
LVGL widget access is encapsulated in a `*_screen_t` struct:
- `create()` — builds or wraps widget tree, called from view init after `ui_init()`
- `update(s, index, value)` — updates widget state, caller holds LVGL lock
- `destroy(s)` — cleanup

The `*_screen.c` file is the **only** file in its domain that may `#include "ui.h"`.

---

## Agent Working Guidelines

**Before modifying an event**: read the manifest comment in `view_data.h` to find all producers and consumers. A payload struct change affects every file listed there.

**Before modifying a module**: check its blast radius in this table:

| File | Blast radius |
|------|-------------|
| `view_data.h` | Entire codebase — touch with care |
| `ha/ha.h` | All files in `ha/` |
| `ui/ui_events.c` | Squareline callbacks; touches `ha_switch`, `wifi_model`, `wifi_view` |
| `indicator_mqtt.h` | WiFi model, HA model, MQTT controller |

**Verifying changes**: run `python3 scripts/dev_check.py` — it runs `architecture_scan.py` then a full build. A passing build + scan is the minimum bar.

**Adding a new event**: add it to the enum in `view_data.h` with a full manifest comment (P/C/Payload/LVGL). Add it before `VIEW_EVENT_ALL`.

**Adding a new screen component**: follow `ha/ha_switch_screen.c` as the template. The new `*_screen.c` is the only file in its domain that may import `ui.h`.
