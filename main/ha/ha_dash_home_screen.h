#ifndef HA_DASH_HOME_SCREEN_H
#define HA_DASH_HOME_SCREEN_H

/* Dashboard home tile (NAV_TILE_HOME): three quick-action chips over a 2x2
 * grid of room cards with live temperatures. Tapping a room card navigates to
 * that room's page (ha_dash_room_screen.h). */

#ifdef __cplusplus
extern "C" {
#endif

void ha_dash_home_screen_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_DASH_HOME_SCREEN_H */
