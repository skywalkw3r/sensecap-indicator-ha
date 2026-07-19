#ifndef HA_DASH_ROOM_SCREEN_H
#define HA_DASH_ROOM_SCREEN_H

/* Per-room dashboard pages (nav tiles NAV_TILE_ROOM_FIRST..): one swipeable
 * page per dash_rooms[] entry, rendering that room's slots from
 * dashboard_config.h — hero temperature, stat rows, toggles, a light card
 * with brightness, the media card and preset action chips. */

#ifdef __cplusplus
extern "C" {
#endif

void ha_dash_room_screens_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_DASH_ROOM_SCREEN_H */
