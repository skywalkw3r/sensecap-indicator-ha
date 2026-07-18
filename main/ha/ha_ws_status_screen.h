#ifndef HA_WS_STATUS_SCREEN_H
#define HA_WS_STATUS_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only Home Assistant WebSocket status modal (settings card target).
 * Registers VIEW_EVENT_SCREEN_START (SCREEN_HA_WS_STATUS) and
 * VIEW_EVENT_HA_WS_STATUS handlers; the modal itself is built on first open.
 * Call once from the HA view init path (indicator_ha_view_init). */
void ha_ws_status_screen_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_WS_STATUS_SCREEN_H */
