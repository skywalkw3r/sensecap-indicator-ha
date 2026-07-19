#ifndef HA_WS_STUB_H
#define HA_WS_STUB_H

/* Sim-only hooks into the interactive WebSocket stub (ha_ws_stub.c). The mock
 * data layer drives these from its start/tick so the dashboard behaves like a
 * live, subscribed panel. */

#include <stdint.h>

/* Post initial states for every stateful dashboard slot + the media card.
 * Call once after the dashboard screens are built. No-op in SIM_MQTT_MODE. */
void ha_ws_stub_seed(void);

/* Advance the fake media playback (track rotation while "playing"). */
void ha_ws_stub_tick(uint32_t now_ms);

#endif /* HA_WS_STUB_H */
