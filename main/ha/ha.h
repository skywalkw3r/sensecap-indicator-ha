#ifndef HA_H
#define HA_H

/* Umbrella header — full HA domain access.
 * Prefer domain-specific headers when only one slice is needed:
 *   ha_config.h           — broker NVS config types and API
 *   ha_mqtt.h             — MQTT lifecycle, event loops, HA_CFG_EVENT
 *   ha_sensor.h           — sensor entity and MQTT routing
 *   ha_dash.h             — dashboard registry (rooms/slots) + legacy bridge
 *   ha_dash_home_screen.h — Home tile: quick actions + room cards
 *   ha_dash_room_screen.h — per-room dashboard pages
 *   ha_siren.h            — buzzer siren entity (MQTT + console 'beep')
 *   ha_history.h          — sensor-history model (feeds the trends chart)
 *   ha_trend_screen.h     — trends chart tile
 *   ha_ws.h               — HA WebSocket client: subscribe + service calls
 *   ha_ws_config.h        — WebSocket NVS config types and API
 */
#include "view_data.h"
#include "ha_config.h"
#include "ha_mqtt.h"
#include "ha_sensor.h"
#include "ha_dash.h"
#include "ha_dash_home_screen.h"
#include "ha_dash_room_screen.h"
#include "ha_siren.h"
#include "ha_history.h"
#include "ha_trend_screen.h"
#include "ha_ws.h"
#include "ha_ws_config.h"
#include "ha_ws_status_screen.h"

#endif /* HA_H */
