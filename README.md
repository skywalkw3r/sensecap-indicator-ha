# SenseCAP Indicator — Home Assistant Firmware

[![CI](https://github.com/skywalkw3r/sensecap-indicator-ha/actions/workflows/ci.yml/badge.svg)](https://github.com/skywalkw3r/sensecap-indicator-ha/actions/workflows/ci.yml)

Firmware that turns the [Seeed Studio SenseCAP Indicator](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html) into a Home Assistant companion panel. Connects to Wi-Fi, renders touch controls that Home Assistant can drive both ways over MQTT, and displays HA-pushed values (temperature, humidity, CO₂) on screen. On sensor-equipped variants it also publishes the built-in sensor readings.

<figure class="third">
    <img align="left" src="./assets/loft-controls.png" width="240"/>
    <img align="center" src="./assets/general-controls.png" width="240"/>
    <img align="left" src="./assets/trends.png" width="240"/>
    <img align="center" src="./assets/mqtt-address-panel.png" width="240"/>
</figure>


---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [MQTT Protocol](#mqtt-protocol)
- [Home Assistant Setup](#home-assistant-setup)
- [Build & Flash](#build--flash)
- [Console Commands](#console-commands)
- [Configuration](#configuration)
- [Development](#development)
- [Version](#version)

---

## Features

- [x] MQTT broker integration (configurable address, port, client ID, username, password)
- [x] HA-pushed display values (`indicator/display/set`): temperature, humidity, CO₂ rendered on the panel
- [x] Built-in sensor publishing on sensor-equipped variants (SCD41 CO₂, SGP40 tVOC, SHT41 temp/humidity) — the base D1 this fork targets has none, so that pipeline idles
- [x] Three swipeable pages: Loft Controls (temp/humidity/CO₂ + LED strip + brightness), General Controls (All Lights with confirm, Xmas Lights), and a Trends history chart
- [x] All 8 MQTT switch entities remain addressable (switch1–3 have no on-screen widget but keep their topics)
- [x] Wi-Fi and MQTT configuration from the touchscreen
- [x] MQTT broker configuration from the serial console (`setmqtt`, `mqtthelp`)
- [x] NVS-backed persistence for Wi-Fi credentials, MQTT config, switch state
- [x] Display brightness and sleep-mode controls
- [x] PC simulator for iterating on the UI without flashing hardware
- [x] Home Assistant WebSocket API client — live temp/humidity/CO₂ straight from HA entities, no broker or automation needed; console toggle runs MQTT or WebSocket one-at-a-time (see [Option B](#option-b--native-websocket-no-broker-needed-for-live-values))
- [ ] REST API

---

## Quick Start

**Prerequisites:** ESP-IDF v5.4.x installed with `IDF_PATH` exported in your shell (see [ESP-IDF setup](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/get-started/)).

```bash
git clone <this-repo>
cd sensecap-indicator-ha
./dev build && ./dev flash
./dev monitor          # Ctrl-] to exit
```

After flashing, configure Wi-Fi and MQTT broker on the device, then continue to [Home Assistant Setup](#home-assistant-setup). Both can also be set from the serial console — see [Console Commands](#console-commands).

---

## Architecture

The SenseCAP Indicator is a dual-MCU device:

| MCU | Role | Key resources |
|-----|------|---------------|
| **ESP32-S3** | Display, touch, Wi-Fi, MQTT, Home Assistant logic | 8 MB flash, PSRAM @ 120 MHz OCT mode, 240 MHz CPU |
| **RP2040** | Sensor acquisition on Grove ports | Reads CO₂, tVOC, temperature, humidity; relays to ESP32-S3 over UART |

The two chips communicate over a COBS-framed UART link. All Grove sensor access goes through the RP2040; the ESP32-S3 never reads sensors directly. On the base D1 (no bundled Grove sensors) this link simply idles, and the on-screen temperature/humidity/CO₂ come from Home Assistant via `indicator/display/set` instead.

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
│  │   (CO₂)    │  │   (tVOC)   │  │ (temp/hum) │    │
│  └────────────┘  └────────────┘  └────────────┘    │
└─────────────────────────────────────────────────────┘
```

### Model / View pattern

Each application domain has a paired `*_model.c` and `*_view.c`:

- **Model** — owns state, NVS reads/writes, MQTT publish/subscribe, RP2040 packet parsing
- **View** — owns LVGL object updates, touch callbacks, screen transitions

Model and view communicate exclusively through the **ESP event loop** with typed event IDs and payloads defined in `main/view_data.h`. Neither side calls the other's functions directly.

SquareLine Studio-generated files in `main/ui/` are treated as assets. Custom logic goes in `*_view.c` files, not in the generated screens.

For per-domain locations and architecture rules, see [`AGENTS.md`](AGENTS.md).

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

The device speaks two transports and runs **one at a time**: MQTT (switch
entities + display values pushed by an HA automation, Option A) or the native
WebSocket client (subscribes to HA entities directly, Option B — simpler for
live values, no broker or automation YAML, but the MQTT switch entities pause
while it is active). `setha --enable` switches to WebSocket, `setha --disable`
or `setmqtt --enable` switches back to MQTT.

### MQTT (switches, and Option A for display values)

1. **Install an MQTT broker** (Mosquitto is the simplest) and enable the MQTT integration in Home Assistant.

2. **Point the Indicator at the broker** — from the device's MQTT screen, or via [`setmqtt`](#console-commands) over serial. The device ships with **no default broker**; until you set one it stays idle (it never connects to a public internet broker on its own). Once Wi-Fi has an IP address and a broker is configured, the client connects within seconds — internet access is not required, so isolated IoT VLANs work fine.

3. **Add MQTT entities.** Append [`examples/homeassistant/mqtt-entities.yaml`](examples/homeassistant/mqtt-entities.yaml) to your `configuration.yaml` under the `mqtt:` key, then reload MQTT.

4. **Add the dashboard.** Paste [`examples/homeassistant/dashboard.yaml`](examples/homeassistant/dashboard.yaml) into the Lovelace Raw Configuration Editor.

<img src="./assets/Home Assistant Dashboard.png" />

See also: [Seeed wiki — Home Assistant application guide](https://wiki.seeedstudio.com/SenseCAP_Indicator_Application_Home_Assistant/).

### Option B — native WebSocket (no broker needed for live values)

The device connects out to HA's WebSocket API (`ws(s)://<ha>:8123/api/websocket`),
authenticates with a long-lived access token and subscribes to up to three
entities — HA pushes every state change instantly. No inbound port is opened on
the device, and no HA-side automation is needed.

1. **Mint a token** in Home Assistant: Profile → Security → *Long-lived access tokens* → Create token.
2. **Configure over the serial console** (fields merge, so this can be split across commands):

   ```text
   setha -a 192.168.1.10 -t <long-lived-token>
   setha --temp sensor.loft_temperature --humidity sensor.loft_humidity --co2 sensor.loft_co2
   setha --enable
   ```

3. **Check it**: `haconfig` prints the WS block; the Settings → *Home Asst* card on
   the touchscreen shows live connection status. `setha --disable` switches back
   to MQTT.

Enabling the WebSocket client stops the MQTT client (one transport at a time),
so the panel's MQTT switch entities pause until you switch back with
`setha --disable` or `setmqtt --enable`. Addresses accept bare IPs
(`192.168.1.10` → `ws://…:8123`) or TLS URLs (`wss://ha.example.com`,
`https://xyz.ui.nabu.casa` → `wss://…:443`); `wss://` uses the same trust
settings as `mqtts://` (see [TLS](#tls-mqtts)).

---

## Build & Flash

### ESP32-S3 (main firmware)

**Prerequisites:** ESP-IDF v5.4.x with `IDF_PATH` exported.

```bash
./dev build          # idf.py build
./dev flash          # auto-detects port
./dev monitor        # Ctrl-] to exit
./dev fullclean      # reset build directory
```

Manual `idf.py` works the same way:

```bash
. "$IDF_PATH/export.sh"
idf.py build
idf.py -p /dev/ttyUSB0 -b 460800 flash monitor
```

**Critical `sdkconfig.defaults` settings — do not remove:**

- `CONFIG_LV_USE_CLIB_MALLOC=y` — required; LVGL 9 must allocate from the ESP heap (PSRAM). Its built-in 64 KB TLSF pool cannot hold composite-layer draw buffers, and a failed layer allocation live-locks the render task (UI freeze + task_wdt). The old `CONFIG_LV_MEM_CUSTOM=y` was an LVGL 8 option and has no effect on LVGL 9.
- PSRAM clock: 120 MHz OCT mode
- CPU: 240 MHz, flash: QIO 120 MHz

App partition is 7 MB (`partitions.csv`); total flash is 8 MB.

### RP2040 (sensor coprocessor)

Only needed when changing sensor protocols. Current version is **v2.1.0**. Built with PlatformIO (Arduino core by Earle Philhower).

```bash
./dev rp2040 build
./dev rp2040 upload      # device must appear as /dev/cu.usbmodem*
./dev rp2040 monitor
```

---

## Console Commands

Connect at the device's baud rate and use these commands:

| Command | Description |
|---------|-------------|
| `mqtthelp` | Print broker, topic, payload and `setha` examples |
| `haconfig` | Print the current MQTT + WebSocket configuration |
| `setmqtt -a <addr>` | Set broker address (e.g., `mqtt://192.168.1.10:1883`) |
| `setmqtt -a <addr> -c <client-id> -u <user> -p <pass>` | Full broker configuration |
| `setha -a <addr> -t <token>` | Set the HA WebSocket address + access token |
| `setha --temp/--humidity/--co2 <entity_id>` | Map HA entities to the display tiles (`""` clears a slot) |
| `setha --enable` / `--disable` | Switch to WebSocket / back to MQTT (one transport at a time) |
| `setmqtt --enable` / `--disable` | Turn the MQTT client on (stops WebSocket) / off |

**Examples:**

```text
setmqtt -a 192.168.1.10 -c indicator-01 -u mqtt_user -p mqtt_password
setmqtt --addr mqtt://192.168.1.10:1883
setha -a 192.168.1.10 -t eyJhbGciOi... --temp sensor.loft_temperature --enable
```

After `setmqtt`/`setha` succeeds, the configuration is saved to NVS and the affected client restarts automatically.

### TLS (`mqtts://`)

Use an `mqtts://` broker URL to encrypt the MQTT connection (port defaults to 8883):

| Command | Description |
|---------|-------------|
| `setmqtt -a mqtts://<host>[:port] ...` | TLS broker; server certificate is verified |
| `setmqttca` | Paste a CA certificate PEM over the console; stored in NVS and used to verify the broker |
| `setmqttca -c` | Clear the stored CA (falls back to the built-in public CA bundle) |
| `setmqtt --insecure` / `--secure` | Skip / re-enable server verification (skipping is discouraged) |

Verification order: stored CA if present, otherwise the public CA bundle (for
cloud brokers with real certificates). LAN brokers with a self-signed/private
CA need `setmqttca`. The touchscreen MQTT page stays plaintext-only (`mqtt://`).
The same stored trust settings (mode + CA) also govern `wss://` WebSocket
connections to Home Assistant.

**With Home Assistant's Mosquitto add-on:** set `certfile`/`keyfile`/`cafile`
in the add-on configuration to expose a TLS listener on 8883 (HA and other
local clients can keep using 1883 internally), then paste your CA cert into
`setmqttca` and point `setmqtt` at `mqtts://<HA-host>:8883`.

### Security notes

MQTT and Wi-Fi credentials — and the Home Assistant long-lived access token — are stored in plaintext in NVS, and the serial console is unauthenticated — anyone with physical or USB access can read them back. Use a dedicated MQTT user with a limited ACL so a leaked credential can't reach the rest of your broker, and remember the HA token can be revoked any time from the HA profile page. Before reselling or disposing of the device, run `idf.py erase-flash` to wipe stored credentials.

---

## Configuration

Run `idf.py menuconfig` to explore options. Notable locations:

- **LVGL fonts** — `Component config → LVGL configuration → Font usage`
- **PSRAM clock** — `Components → ESP PSRAM → SPI RAM config` (requires "Make experimental features visible")

To regenerate `sdkconfig` from defaults: delete it and run `./dev build`.

**MQTT broker** — there is no compiled-in default broker; a fresh device is unconfigured and stays idle until you set a broker from the touchscreen or `setmqtt`. Config is stored in NVS. If a stored config is ever unreadable or the wrong size (e.g. after a struct change), the device treats itself as unconfigured rather than falling back to any default. The client (re)starts automatically whenever Wi-Fi obtains an IP and a broker is configured, and when you save a new broker; it does not require internet reachability.

---

## Development

**Code completion.** Install [clangd](https://github.com/clangd/clangd/releases) and the [clangd VS Code extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd). After one successful build, `build/compile_commands.json` is generated and clangd uses it automatically for ESP-IDF-aware navigation.

**PC simulator.** The `sim/` directory builds a desktop version of the UI using LVGL's SDL2 backend (macOS, 480×480 window matching the real LCD). Useful for iterating on screens, fonts, and layouts without flashing.

```bash
cmake -S sim -B sim/build && cmake --build sim/build -j4
./sim/build/sensecap_sim
```

**Architecture rules.** See [`AGENTS.md`](AGENTS.md) for module locations, boot sequence, and the model/view boundary rules that contributors (human and AI) must follow.

---

## Version

| Component | Version |
|-----------|---------|
| Firmware (ESP32-S3) | `v1.1.0` |
| Coprocessor (RP2040) | `v2.1.0` |
| ESP-IDF | `v5.4.x` |
| LVGL | `v9.x` (managed component) |
| RP2040 build system | PlatformIO |

<details>
<summary>Changelog</summary>

| Version | Notes |
|---------|-------|
| `v1.1.0` | Upgraded to ESP-IDF 5.4.x and LVGL 9; added `mqtthelp` console command; improved MQTT setup UX; unified build/flash tooling into `./dev` |
| `v1.0.0` | Initial release (ESP-IDF 5.1 era, LVGL 8) |

</details>
