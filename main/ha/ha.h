#ifndef HA_H
#define HA_H

/* Umbrella header — full HA domain access.
 * Prefer domain-specific headers when only one slice is needed:
 *   ha_config.h        — broker NVS config types and API
 *   ha_mqtt.h          — MQTT lifecycle, event loops, HA_CFG_EVENT
 *   ha_sensor.h        — sensor entity and MQTT routing
 *   ha_switch.h        — switch entity, MQTT routing, screen attachment
 *   ha_history.h       — sensor-history model (feeds the trends chart)
 *   ha_trend_screen.h  — trends chart tile
 */
#include "view_data.h"
#include "ha_config.h"
#include "ha_mqtt.h"
#include "ha_sensor.h"
#include "ha_switch.h"
#include "ha_history.h"
#include "ha_trend_screen.h"

#endif /* HA_H */
