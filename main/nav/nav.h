#ifndef NAV_H
#define NAV_H

#include "lvgl.h"

/* Tile indices for the main swipeable screens.
 * The dashboard (ha/ha_dash_*_screen.c) owns tiles 0..4: Home plus one page
 * per room in dash_rooms[] order (dashboard_config.h). Keep these literal —
 * scripts/test_ui_geometry.py parses NAV_TILE_COUNT as a number; ha_dash's
 * screens static_assert the room count so the table and this map can't drift.
 * (The built-in sensor tile stays removed: base-D1 has no onboard sensors.) */
#define NAV_TILE_HOME        0   /* Dashboard home: quick actions + room cards */
#define NAV_TILE_ROOM_FIRST  1   /* Loft, Guest Room, Living Room, Hallway & Other */
#define NAV_TILE_HA_TREND    5   /* Trends: lv_chart history of temp/humidity/CO2 */
#define NAV_TILE_COUNT       6

int      nav_init(void);
lv_obj_t *nav_get_tile(int tile_idx);   /* returns the container for that tile */
void     nav_go_tile(int tile_idx);     /* programmatic navigation */

#endif /* NAV_H */
