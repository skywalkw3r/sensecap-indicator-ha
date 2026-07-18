# HA Domain — Vertical Slice Architecture

Home Assistant integration. This domain is the pilot for the project's target architecture:
each feature slice owns its full stack (data model + MQTT protocol + UI component).

---

## Files and Responsibilities

| File | Lines | Owns |
|------|-------|------|
| `ha.h` | 50 | Public API for the entire domain. Include this, not individual slice headers. |
| `ha_mqtt.c` | ~200 | MQTT client lifecycle: connect/disconnect/reconnect. Routes `MQTT_EVENT_DATA` to sensor and switch slices. Owns `mqtt_ha_instance`. |
| `ha_config.c` | ~154 | Broker NVS config (`ha_cfg_get`/`ha_cfg_set`) + IP address modal view. |
| `ha_sensor.c` | ~120 | Sensor entity metadata, publishes built-in sensor readings. Retains a dormant JSON parser (`ha_sensor_on_mqtt_data`) that would route incoming sensor JSON to `VIEW_EVENT_HA_SENSOR`; no live subscription (see Known Issues). |
| `ha_switch.c` | ~237 | Switch entity metadata, NVS state persistence, MQTT publish/subscribe, posts `VIEW_EVENT_HA_SWITCH_SET`. Holds `ha_switch_screen_t *` — does NOT touch LVGL directly. |
| `ha_switch_screen.c` | ~500 | Builds HA switch widgets on nav tiles. Owns widget handles for all 8 switch widgets. Dispatch table maps index → widget + updater function. |
| `ha_switch_screen.h` | 18 | `create` / `update` / `destroy` interface. |
| `ha_ws.c` | ~430 | HA WebSocket API client: lifecycle on `ha_event_task` (own `HA_WS_EVENT_BASE`), auth + `subscribe_entities` protocol, frame reassembly, posts `VIEW_EVENT_HA_SENSOR` + `VIEW_EVENT_HA_WS_STATUS`. |
| `ha_ws_config.c` | ~100 | WebSocket NVS config (`ha_ws_cfg_get`/`ha_ws_cfg_set`, own `"ha-ws"` key) + URL normalization. |
| `ha_ws_status_screen.c` | ~160 | Read-only WebSocket status modal (settings "Home Asst" card target). |

---

## Init Sequence

```
indicator_ha_view_init()    (called from indicator_view.c)
  ha_config_view_init()     register VIEW_EVENT_MQTT_ADDR_CHANGED + VIEW_EVENT_HA_ADDR_DISPLAY
  ha_switch_screen_create() builds switch widgets on NAV_TILE_HA_CTRL and NAV_TILE_HA_MIX
  ha_switch_attach_screen() gives ha_switch.c the screen handle

indicator_ha_model_init()   (called later from indicator_model.c)
  ha_sensor_init()          register VIEW_EVENT_SENSOR_DATA handler
  ha_switch_init()          init entities, restore NVS state,
                            register VIEW_EVENT_HA_SWITCH_ST + VIEW_EVENT_HA_SWITCH_SET handlers
  create ha_cfg_event_handle
  init mqtt_ha_instance
  register VIEW_EVENT_WIFI_ST handler
```

---

## Event Flows

```
User changes a switch widget
  → ha_switch_screen.c: LV_EVENT_VALUE_CHANGED (toggles) / LV_EVENT_RELEASED (slider, arc)
  → post VIEW_EVENT_HA_SWITCH_ST {index, value}
  → ha_switch.c: publish to MQTT topic_state, save NVS (skipped when unchanged)

MQTT broker sends switch command
  → ha_mqtt.c: MQTT_EVENT_DATA
  → ha_switch_on_mqtt_data()
  → post VIEW_EVENT_HA_SWITCH_SET {index, value}
  → ha_switch.c: ha_switch_screen_update() [LVGL lock] applies widget state silently,
    then publishes to MQTT topic_state + saves NVS directly (no LVGL event round-trip)

WiFi connects
  → VIEW_EVENT_WIFI_ST {is_network=true}
  → ha_mqtt.c: post MQTT_APP_START to mqtt_app_event_handle
  → mqtt/mqtt.c: calls mqtt_ha_instance.mqtt_starter()
  → ha_mqtt.c: _mqtt_ha_start() reads NVS config, creates MQTT client

MQTT connects
  → MQTT_EVENT_CONNECTED
  → ha_switch_subscribe() — subscribe to switch topics, start restore task
  (ha_sensor no longer self-subscribes to indicator/sensor — see Known Issues)

User changes broker IP
  → ha_config.c: posts VIEW_EVENT_MQTT_ADDR_CHANGED
  → ha_config.c: validates, saves NVS, posts HA_CFG_BROKER_CHANGED
  → ha_mqtt.c: stop/set_uri/start MQTT client
```

---

## Live display values (two sources)

`VIEW_EVENT_HA_SENSOR` (consumed by `ha_switch.c` tiles and `ha_history.c` →
Trends chart) has two producers, never active at the same time:

- **MQTT**: `ha_sensor.c` subscribes to `indicator/display/set` and parses
  HA-pushed JSON (`{"loft_temp":72.4,...}`).
- **WebSocket**: `ha_ws.c` subscribes to the configured HA entities over the HA
  WebSocket API.

The whole transports are one-at-a-time: `setha --enable` sets the `"mqtt-en"`
NVS flag to 0 so `_mqtt_ha_start` stays down (switch entities pause);
`setha --disable` / `setmqtt --enable` flip it back and disable WS. If NVS ever
claims both, WS wins (`_mqtt_ha_start` defers to `ha_ws_is_enabled()`), and
`ha_sensor_on_mqtt_data()` additionally ignores `indicator/display/set` while
WS is enabled so the history buffer can never double-feed.

```
WS connects (ha_event_task built it after HA_WS_CFG_CHANGED / HA_WS_NET_UP)
  → auth_required → auth (token) → auth_ok → subscribe_entities
  → "a"/"c" entity events → post VIEW_EVENT_HA_SENSOR {index, value}
  → auth_invalid → HA_WS_AUTH_FAIL → teardown + latch until next setha
```

---

## Adding a New Switch Widget Type

1. Add a new `_update_xxx()` function in `ha_switch_screen.c`
2. Add a slot entry in `ha_switch_screen_create()` pointing to the locally-created widget and updater
3. Extend `CONFIG_HA_SWITCH_ENTITY_NUM` in `home_assistant_config.h` if adding a new entity

## Extending to a New Feature Slice

Follow this pattern:
1. `ha_yyy.c` — data + MQTT logic, registers event handlers in `ha_yyy_init()`
2. `ha_yyy_screen.c` — LVGL component, `create`/`update`/`destroy` interface
3. Declare cross-module functions in `ha.h`
4. Call `ha_yyy_init()` from `indicator_ha_model_init()` in `ha_mqtt.c`
5. Call `ha_yyy_screen_create()` + attach from `indicator_ha_view_init()` in `ha_mqtt.c`
