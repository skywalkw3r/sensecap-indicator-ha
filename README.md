# SenseCAP Indicator — Home Assistant Firmware

Firmware that turns the [Seeed Studio SenseCAP Indicator](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html) into a Home Assistant companion panel. The device connects to Wi-Fi, publishes built-in environmental sensor data over MQTT, and renders on-screen controls that Home Assistant can drive in both directions.

<figure class="third">
    <img align="left" src="./assets/Home Assistant Data.png" width="240"/>
    <img align="center" src="./assets/Home Assistant.png" width="240"/>
    <img align="left" src="./assets/Home Assistant Control(ON).png" width="240"/>
    <img align="center" src="./assets/Home Assistant Control(OFF).png" width="240"/>
    <img align="center" src="./assets/mqtt-address-panel.png" width="240"/>
</figure>

---

## Table of Contents

- [Hardware](#hardware)
- [Features](#features)
- [Architecture](#architecture)
- [MQTT Protocol](#mqtt-protocol)
- [Home Assistant Setup](#home-assistant-setup)
- [Build & Flash](#build--flash)
  - [ESP32S3 (main firmware)](#esp32s3-main-firmware)
  - [RP2040 (coprocessor)](#rp2040-coprocessor)
- [Console Commands](#console-commands)
- [Project Layout](#project-layout)
- [Configuration](#configuration)
- [Code Autocompletion](#code-autocompletion)
- [Version](#version)

---

## Hardware

The SenseCAP Indicator is a dual-MCU device:

| MCU | Role | Key resources |
|-----|------|---------------|
| **ESP32-S3** | Display, touch, Wi-Fi, MQTT, Home Assistant logic | 8 MB flash, PSRAM @ 120 MHz OCT mode, 240 MHz CPU |
| **RP2040** | Sensor acquisition on Grove ports | Reads CO₂, tVOC, temperature, humidity; relays data to ESP32-S3 over UART |

The two chips communicate over a COBS-framed UART link. All Grove sensor access goes through the RP2040; the ESP32-S3 never reads sensors directly.

---

## Features

- [x] MQTT broker integration (configurable address, port, client ID, username, password)
- [x] Built-in sensor publishing: temperature, humidity, CO₂ (SCD41), tVOC (SGP40/SHT41)
- [x] Home Assistant control widgets: 6 binary switches + 2 numeric sliders
- [x] Wi-Fi setup from the touchscreen (scan, select network, enter password)
- [x] MQTT broker configuration from the touchscreen
- [x] MQTT broker/client configuration from the serial console (`setmqtt`, `mqtthelp`)
- [x] NVS-backed persistence for Wi-Fi credentials, MQTT config, and switch state
- [x] Display brightness and sleep-mode controls
- [ ] REST API
- [ ] WebSocket

---

## Architecture

### Dual-MCU split

```
┌─────────────────────────────────────────────────────┐
│                     ESP32-S3                         │
│                                                      │
│  ┌──────────┐   ESP event   ┌──────────┐            │
│  │  Model   │◄─────loop────►│   View   │            │
│  │ (state,  │               │ (LVGL,   │            │
│  │  NVS,    │               │  touch   │            │
│  │  MQTT)   │               │  input)  │            │
│  └────┬─────┘               └──────────┘            │
│       │ UART / COBS                                  │
└───────┼─────────────────────────────────────────────┘
        │
┌───────┼─────────────────────────────────────────────┐
│       ▼          RP2040                              │
│  Packet dispatch                                     │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐    │
│  │   SCD41    │  │   SGP40    │  │   SHT41    │    │
│  │   (CO₂)   │  │  (tVOC)   │  │ (temp/hum) │    │
│  └────────────┘  └────────────┘  └────────────┘    │
└─────────────────────────────────────────────────────┘
```

### Model / View pattern

Each application domain has a paired `*_model.c` and `*_view.c`:

- **Model** — owns state, NVS reads/writes, MQTT publish/subscribe, RP2040 packet parsing.
- **View** — owns all LVGL object updates, touch callbacks, and screen transitions.

Communication between model and view flows exclusively through the **ESP event loop** using typed event IDs and payloads defined in `main/view_data.h`. Neither side calls the other's functions directly.

SquareLine Studio-generated files in `main/ui/` are treated as assets. Custom logic goes in the `*_view.c` files, not in the generated screens.

### UI screens

| Screen | Purpose |
|--------|---------|
| `ui_screen_wifi` | Wi-Fi scan and connect |
| `ui_screen_broker` | MQTT broker IP entry |
| `ui_screen_ha_data` | Live sensor readings |
| `ui_screen_ha_ctrl` | Switch and slider controls |
| `ui_screen_ha_mix` | Combined sensor + control view |
| `ui_screen_display` | Brightness and sleep settings |
| `ui_screen_setting` | General settings menu |

---

## MQTT Protocol

Three fixed topics carry all communication:

| Direction | Topic | Example payload |
|-----------|-------|-----------------|
| Device → HA (sensors) | `indicator/sensor` | `{"temp":"23.5","humidity":"45","co2":"450","tvoc":"100"}` |
| HA → Device (commands) | `indicator/switch/set` | `{"switch1":1,"switch5":50}` |
| Device → HA (state echo) | `indicator/switch/state` | `{"switch1":1,"switch2":0}` |

**Sensor keys:** `temp`, `humidity`, `co2`, `tvoc`

**Control keys:**

| Key | HA entity type | Range |
|-----|---------------|-------|
| `switch1`–`switch4`, `switch6`–`switch7` | Binary switch | `0` / `1` |
| `switch5`, `switch8` | Numeric slider | integer value |

---

## Home Assistant Setup

**Step 1 — MQTT broker.** Install and enable an MQTT broker (Mosquitto is the simplest choice) and add the MQTT integration in Home Assistant.

**Step 2 — Configure the Indicator.** Point the device at the same broker (see [Console Commands](#console-commands) or use the on-screen broker settings page).

**Step 3 — Add entities** to `configuration.yaml`:

<details>
<summary>Expand full <code>configuration.yaml</code> snippet</summary>

```yaml
mqtt:
  sensor:
    - unique_id: indicator_temperature
      name: "Indicator Temperature"
      state_topic: "indicator/sensor"
      suggested_display_precision: 1
      unit_of_measurement: "°C"
      value_template: "{{ value_json.temp }}"
    - unique_id: indicator_humidity
      name: "Indicator Humidity"
      state_topic: "indicator/sensor"
      unit_of_measurement: "%"
      value_template: "{{ value_json.humidity }}"
    - unique_id: indicator_co2
      name: "Indicator CO2"
      state_topic: "indicator/sensor"
      unit_of_measurement: "ppm"
      value_template: "{{ value_json.co2 }}"
    - unique_id: indicator_tvoc
      name: "Indicator tVOC"
      state_topic: "indicator/sensor"
      unit_of_measurement: ""
      value_template: "{{ value_json.tvoc }}"
  switch:
    - unique_id: indicator_switch1
      name: "Indicator Switch1"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch1 }}"
      payload_on: '{"switch1":1}'
      payload_off: '{"switch1":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch2
      name: "Indicator Switch2"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch2 }}"
      payload_on: '{"switch2":1}'
      payload_off: '{"switch2":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch3
      name: "Indicator Switch3"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch3 }}"
      payload_on: '{"switch3":1}'
      payload_off: '{"switch3":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch4
      name: "Indicator Switch4"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch4 }}"
      payload_on: '{"switch4":1}'
      payload_off: '{"switch4":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch6
      name: "Indicator Switch6"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch6 }}"
      payload_on: '{"switch6":1}'
      payload_off: '{"switch6":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch7
      name: "Indicator Switch7"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch7 }}"
      payload_on: '{"switch7":1}'
      payload_off: '{"switch7":0}'
      state_on: 1
      state_off: 0
  number:
    - unique_id: indicator_switch5
      name: "Indicator Switch5"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      command_template: '{"switch5": {{ value }} }'
      value_template: "{{ value_json.switch5 }}"
    - unique_id: indicator_switch8
      name: "Indicator Switch8"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      command_template: '{"switch8": {{ value }} }'
      value_template: "{{ value_json.switch8 }}"
```

</details>

**Step 4 — Add a dashboard.** Paste this into the raw configuration editor:

<details>
<summary>Expand dashboard YAML</summary>

```yaml
views:
  - title: Indicator device
    icon: ''
    badges: []
    cards:
      - graph: line
        type: sensor
        detail: 1
        icon: mdi:molecule-co2
        unit: ppm
        entity: sensor.indicator_co2
      - graph: line
        type: sensor
        entity: sensor.indicator_temperature
        detail: 1
        icon: mdi:coolant-temperature
      - graph: line
        type: sensor
        detail: 1
        entity: sensor.indicator_humidity
      - graph: line
        type: sensor
        entity: sensor.indicator_tvoc
        detail: 1
        icon: mdi:air-filter
      - type: entities
        entities:
          - entity: switch.indicator_switch1
          - entity: switch.indicator_switch2
          - entity: switch.indicator_switch3
          - entity: switch.indicator_switch4
          - entity: number.indicator_switch5
          - entity: switch.indicator_switch6
          - entity: switch.indicator_switch7
          - entity: number.indicator_switch8
        title: Indicator control
        show_header_toggle: false
        state_color: true
```

</details>

<img src="./assets/Home Assistant Dashboard.png" />

See also the Seeed wiki: [SenseCAP Indicator — Home Assistant Application Development](https://wiki.seeedstudio.com/SenseCAP_Indicator_Application_Home_Assistant/).

---

## Build & Flash

### ESP32S3 (main firmware)

**Prerequisites:** ESP-IDF v5.4.x. Install it under `$HOME/esp/v5.4.3/esp-idf` or set `IDF_PATH` before building.

```bash
# Quick build via helper script (sets IDF_PATH automatically)
sh build.sh

# Or use the Python wrapper
./fw build
./fw flash          # detects port automatically
./fw monitor
```

**Manual steps:**

```bash
# Source the IDF environment once per shell
. "$HOME/esp/v5.4.3/esp-idf/export.sh"

idf.py build
idf.py -p /dev/ttyUSB0 -b 460800 flash
idf.py -p /dev/ttyUSB0 monitor   # Ctrl-] to exit
```

The application partition is 7 MB (`partitions.csv`). Total SenseCAP flash is 8 MB.

Key `sdkconfig.defaults` settings (do not remove these):
- `CONFIG_LV_MEM_CUSTOM=y` — required; removing it causes an LVGL freeze
- PSRAM clock: 120 MHz OCT mode
- CPU: 240 MHz, flash: QIO 120 MHz

### RP2040 (coprocessor)

The RP2040 firmware lives in `rp2040/` and is built with **PlatformIO** (Arduino core by Earle Philhower).

```bash
cd rp2040

# Build
pio run

# Upload (device must appear as /dev/cu.usbmodem*)
pio run -t upload

# Monitor at 115200 baud
pio device monitor
```

The RP2040 firmware is at version **v2.1.0**. You only need to reflash it if you are working on sensor protocol changes.

---

## Console Commands

Connect at the device's baud rate and use these commands:

| Command | Description |
|---------|-------------|
| `mqtthelp` | Print broker, topic, and payload examples |
| `haconfig` | Print the current MQTT/HA configuration |
| `setmqtt -a <addr>` | Set broker address (e.g., `mqtt://192.168.1.10:1883`) |
| `setmqtt -a <addr> -c <client-id> -u <user> -p <pass>` | Full broker configuration |

**Examples:**

```text
setmqtt -a 192.168.1.10 -c indicator-01 -u mqtt_user -p mqtt_password
setmqtt --addr mqtt://192.168.1.10:1883
setmqtt --addr mqtt://broker.emqx.io
```

After `setmqtt` succeeds, the configuration is saved to NVS and the MQTT client restarts automatically.

---

## Project Layout

<details>
<summary>Expand directory tree</summary>

```
.
├── main/
│   ├── main.c                   # Boot: bsp_init → lv_port_init → view_init → model_init
│   ├── view_data.h              # Shared event IDs, payloads, sensor enums, Wi-Fi structs
│   ├── home_assistant_config.h  # MQTT topics and HA entity config macros
│   ├── lv_port.c/h              # LVGL FreeRTOS port (semaphore-locked rendering)
│   ├── indicator_model.c        # Model layer boot sequencer
│   ├── indicator_view.c         # View layer boot sequencer (SquareLine UI startup)
│   │
│   ├── app/                     # Application modules (model + view pairs)
│   │   ├── indicator_wifi_*     # Wi-Fi scanning, connection, UI
│   │   ├── indicator_ha_*       # HA entity sync, MQTT payload parsing, control state
│   │   ├── indicator_mqtt.*     # MQTT lifecycle and event loop
│   │   ├── indicator_sensor_*   # Sensor data cache and historical ring buffer
│   │   ├── indicator_display_*  # Brightness and sleep mode UI
│   │   ├── esp32_rp2040.*       # COBS UART ingress from RP2040
│   │   ├── indicator_btn.*      # Physical button events (single/double/long press)
│   │   ├── indicator_cmd.*      # Serial console command dispatch
│   │   └── indicator_storage_nvs.* # NVS read/write helpers
│   │
│   ├── ui/                      # SquareLine-generated UI (treat as assets)
│   │   ├── screens/             # Seven screen implementations
│   │   ├── images/              # Compiled PNG icon resources
│   │   └── fonts/               # LVGL font data
│   │
│   └── util/
│       ├── cobs.*               # COBS encode/decode (RP2040 link protocol)
│       └── indicator_util.*     # Miscellaneous helpers
│
├── components/
│   ├── bsp/                     # Board support: LCD, touch, LED, buttons, I2C, audio
│   ├── bus/                     # I2C/SPI bus abstraction
│   ├── i2c_devices/             # Sensor drivers (SCD41, SGP40, SHT41, …)
│   ├── iot_button/              # FreeRTOS button debounce and event driver
│   ├── lora/                    # LoRa radio support (optional)
│   └── lvgl/                    # LVGL graphics library
│
├── rp2040/                      # RP2040 PlatformIO project
│   ├── src/main.cpp             # UART packet dispatch, command handler
│   ├── src/sensors.cpp          # Sensor init and acquisition
│   └── platformio.ini           # Build config (Arduino core, sensor libraries)
│
├── scripts/
│   ├── fw.py                    # CLI wrapper: build / flash / monitor
│   ├── dev_check.py             # Architecture scan + firmware build verification
│   ├── architecture_scan.py     # Detects model/view boundary violations
│   └── verify_rp2040_protocol.py # Validates RP2040 packet format
│
├── CMakeLists.txt
├── sdkconfig.defaults           # Minimal ESP-IDF defaults (PSRAM, LVGL, CPU speed)
├── partitions.csv               # 7 MB app partition layout
├── build.sh / fw.bat            # Shell build helpers
└── AGENTS.md                    # Architecture rules for AI agents and contributors
```

</details>

---

## Configuration

### MQTT — on device

Open **Settings → MQTT Broker** on the touchscreen. Enter the broker IP and confirm. The firmware stores the address as `mqtt://<ip>:1883`.

### MQTT — serial console

```text
setmqtt -a 192.168.1.10 -c indicator-01 -u user -p pass
```

### Display

Use the **Display** screen on the device to adjust brightness and configure screen sleep timeout.

### sdkconfig

Run `idf.py menuconfig` to explore all options. Notable locations:

- **LVGL fonts** — `Component config → LVGL configuration → Font usage → Enable built-in fonts`
- **PSRAM clock** — `Components → ESP PSRAM → SPI RAM config → Set RAM clock speed` (requires "Make experimental features visible")

To regenerate a clean `sdkconfig` from defaults: delete `sdkconfig` and run `idf.py build`.

---

## Code Autocompletion

Install [clangd](https://github.com/clangd/clangd/releases) and the [clangd VS Code extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd). After one successful build, `build/compile_commands.json` is generated and clangd uses it automatically for full ESP-IDF-aware completion and navigation.

---

## Version

| Component | Version |
|-----------|---------|
| Firmware (ESP32-S3) | `v1.1.0` |
| Coprocessor (RP2040) | `v2.1.0` |
| ESP-IDF | `v5.4.x` |
| RP2040 build system | PlatformIO |

**Last commit:** `196e89f` — 2026-05-27 · refactor(tooling): unify build/flash into one cross-platform `./dev`

`v1.1.0` upgrades the project from the ESP-IDF 5.1-era codebase to ESP-IDF 5.4.x and improves the MQTT setup experience. The serial console now includes an `mqtthelp` command with concrete broker, topic, and payload examples.

<details>
<summary>Full changelog</summary>

| Version | Notes |
|---------|-------|
| `v1.1.0` | Upgraded to ESP-IDF 5.4.x; added `mqtthelp` console command; improved MQTT setup UX |
| `v1.0.0` | Initial release (ESP-IDF 5.1 era) |

</details>
