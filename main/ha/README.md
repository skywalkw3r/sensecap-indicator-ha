# HA Domain — Vertical Slice Architecture

Home Assistant integration. This domain is the pilot for the project's target architecture:
each feature slice owns its full stack (data model + transport protocol + UI component).

The centrepiece is the **dashboard**: a compile-time registry of rooms and
entities (`main/dashboard_config.h`) rendered as a Home page plus one page per
room, fed and controlled over the HA WebSocket API.

---

## Files and Responsibilities

| File | Owns |
|------|------|
| `ha.h` | Public API for the entire domain. Include this, not individual slice headers. |
| `ha_dash.c` | Dashboard registry: `dash_slots[]`/`dash_rooms[]` expanded from `dashboard_config.h`, entity-id lookup, MQTT-mode legacy bridge (`VIEW_EVENT_HA_SENSOR` → `VIEW_EVENT_HA_ENTITY`). Pure model. |
| `ha_dash_home_screen.c` | Home tile (`NAV_TILE_HOME`): connection pill, quick-action chips (toggles + confirm-gated scripts), 2×2 room-card grid with live temps; tap → `nav_go_tile()`. |
| `ha_dash_room_screen.c` | One page per room: hero temperature, stat rows, embedded switches, light card with brightness slider, media card (title/artist/play-pause), preset action chips. |
| `ha_ws.c` | HA WebSocket API client: lifecycle on `ha_event_task` (own `HA_WS_EVENT_BASE`), auth + `subscribe_entities` over the dashboard registry, frame reassembly, compressed-state routing (incl. media attribute-diff merging), **`ha_ws_call()` service-call TX path**, posts `VIEW_EVENT_HA_ENTITY` / `VIEW_EVENT_HA_MEDIA` / legacy `VIEW_EVENT_HA_SENSOR` / `VIEW_EVENT_HA_WS_STATUS`. |
| `ha_ws_config.c` | WebSocket NVS config (`ha_ws_cfg_get`/`ha_ws_cfg_set`, own `"ha-ws"` key) + URL normalization. The stored per-slot entity ids are dormant (blob-layout compatibility); entities live in `dashboard_config.h`. |
| `ha_ws_status_screen.c` | Read-only WebSocket status modal (settings "Home Asst" card target). |
| `ha_mqtt.c` | MQTT client lifecycle: connect/disconnect/reconnect. Routes `MQTT_EVENT_DATA` to the sensor and siren slices. Owns `mqtt_ha_instance` and both domain init aggregators. |
| `ha_config.c` | Broker NVS config (`ha_cfg_get`/`ha_cfg_set`) + IP address modal view. |
| `ha_sensor.c` | Sensor entity metadata, publishes built-in sensor readings, parses HA-pushed `indicator/display/set` values into `VIEW_EVENT_HA_SENSOR` (gated off while WS is enabled). |
| `ha_siren.c` | Buzzer as an HA MQTT siren entity. Tone engine (esp_timer) sequences RP2040 beep packets; `indicator/siren/set` in, `indicator/siren/state` out. Shared with the console `beep` command via `ha_siren_trigger()`. |
| `ha_history.c` | Per-series ring buffer for the legacy display indices (0=temp 1=humidity 2=co2) → `VIEW_EVENT_HA_HISTORY` → Trends chart. |
| `ha_trend_screen.c` | Trends chart tile (`NAV_TILE_HA_TREND`). |

The old `ha_switch.c` / `ha_switch_screen.c` MQTT topic bridge (`switch1..8` on
`indicator/switch/set|state`) was deliberately retired: dashboard controls call
HA services directly over the WebSocket, which also syncs true entity state
back to the panel.

---

## Init Sequence

```
indicator_ha_view_init()        (called from indicator_view.c)
  ha_config_view_init()         register VIEW_EVENT_MQTT_ADDR_CHANGED + VIEW_EVENT_HA_ADDR_DISPLAY
  ha_dash_home_screen_init()    Home tile: chips + room cards, registers HA_ENTITY + HA_WS_STATUS
  ha_dash_room_screens_init()   4 room tiles from the registry, registers HA_ENTITY + HA_MEDIA
  ha_trend_screen_init()        Trends tile
  ha_ws_status_screen_init()    status modal

indicator_ha_model_init()       (called later from indicator_model.c)
  ha_sensor_init()              register VIEW_EVENT_SENSOR_DATA handler
  ha_dash_init()                register the MQTT-mode legacy bridge
  ha_history_init()             ring buffers for the trends chart
  ha_siren_init()               create the tone-engine timer + lock
  create ha_cfg_event_handle
  init mqtt_ha_instance
  register VIEW_EVENT_WIFI_ST handler
  ha_ws_init()                  WS lifecycle handlers on ha_cfg_event_handle
```

---

## Event Flows

```
WS connects (ha_event_task built it after HA_WS_CFG_CHANGED / HA_WS_NET_UP)
  → auth_required → auth (token) → auth_ok → subscribe_entities (dashboard slots)
  → result {id == s_subscribe_id, success} → HA_WS_STATUS_SUBSCRIBED
  → "a"/"c" entity events → route by slot kind:
      SENSOR         → VIEW_EVENT_HA_ENTITY (+ VIEW_EVENT_HA_SENSOR when legacy ≥ 0)
      TOGGLE/LIGHT   → VIEW_EVENT_HA_ENTITY {state, brightness}
      MEDIA          → merge state+attr diffs → VIEW_EVENT_HA_MEDIA (on change only)
  → auth_invalid → HA_WS_AUTH_FAIL → teardown + latch until next setha

User taps a dashboard control (LVGL task)
  → screen paints optimistically (silent state apply — no VALUE_CHANGED re-entry)
  → ha_ws_call(domain, service, entity, extra) → HA_WS_TX_CALL on ha_cfg_event_handle
  → ha_event_task frames {"id","type":"call_service",...} in its own buffer and sends
  → HA changes the entity → subscription echo → VIEW_EVENT_HA_ENTITY reconciles the UI
  Service map: toggles homeassistant.turn_on/off · slider light.turn_on {"brightness_pct":N}
               presets + All-Off script.turn_on (All-Off behind a confirm msgbox)
               media media_player.media_play_pause

WiFi connects
  → VIEW_EVENT_WIFI_ST {is_network=true}
  → ha_mqtt.c: post MQTT_APP_START · ha_ws.c: HA_WS_NET_UP
```

---

## Live values (two transports, one at a time)

- **WebSocket** (primary): `ha_ws.c` subscribes to every subscribable dashboard
  slot and produces `VIEW_EVENT_HA_ENTITY`/`HA_MEDIA`; legacy-index slots also
  produce `VIEW_EVENT_HA_SENSOR` for `ha_history.c` → Trends.
- **MQTT** (read-only fallback): `ha_sensor.c` parses `indicator/display/set`
  into `VIEW_EVENT_HA_SENSOR`; `ha_dash.c`'s bridge re-posts those onto the
  matching legacy slots as `VIEW_EVENT_HA_ENTITY` — the dashboard shows Loft
  values, everything else stays "--" and controls are inert.

Screens consume **only** `VIEW_EVENT_HA_ENTITY`/`HA_MEDIA`, so they never care
which transport is active. `setha --enable` sets the `"mqtt-en"` NVS flag to 0
so `_mqtt_ha_start` stays down; `setha --disable` / `setmqtt --enable` flip it
back and disable WS. If NVS ever claims both, WS wins (`_mqtt_ha_start` defers
to `ha_ws_is_enabled()`), and `ha_sensor_on_mqtt_data()` additionally ignores
`indicator/display/set` while WS is enabled so the history can never double-feed.

---

## Changing the Dashboard

Everything lives in `main/dashboard_config.h`:

1. **New entity on an existing page** — add an `X(...)` row to `DASH_SLOT_LIST`
   with the page, kind (`SENSOR`/`TOGGLE`/`LIGHT`/`ACTION`/`MEDIA`), entity id,
   label, icon and accent. The subscription, routing and page layout pick it up
   automatically.
2. **New icon** — add it to `ICONS` in `scripts/gen_mdi_font.py`, regenerate
   (`python3 scripts/gen_mdi_font.py`), reference the new `UI_ICON_*` macro.
3. **New room** — add an `X(...)` row to `DASH_ROOM_LIST` *and* a page id in
   `enum dash_page`, then bump the nav literals in `main/nav/nav.h`
   (`NAV_TILE_HA_TREND`, `NAV_TILE_COUNT`) — the `_Static_assert`s in
   `ha_dash.c`/`ha_dash_home_screen.c` fail the build if anything drifts.
4. Rebuild, run the sim (`.claude/skills/sim-preview.md`) to eyeball it, flash.

## Extending to a New Feature Slice

Follow this pattern:
1. `ha_yyy.c` — data + transport logic, registers event handlers in `ha_yyy_init()`
2. `ha_yyy_screen.c` — LVGL component (file name must match `ha_*_screen.c` for the architecture scan)
3. Declare cross-module functions in `ha.h`
4. Call `ha_yyy_init()` from `indicator_ha_model_init()` in `ha_mqtt.c`
5. Call `ha_yyy_screen_init()` from `indicator_ha_view_init()` in `ha_mqtt.c`
