#ifndef NAV_H
#define NAV_H

#include "lvgl.h"

/* Tile indices for the main swipeable screens.
 * The built-in sensor data tile was removed: this device (base D1) has no
 * onboard environmental sensors, so the page could only ever show "N/A". */
#define NAV_TILE_HA_CTRL  0   /* switch control view (home) */
#define NAV_TILE_HA_MIX   1   /* mixed sensors+switches view */
#define NAV_TILE_COUNT    2

int      nav_init(void);
lv_obj_t *nav_get_tile(int tile_idx);   /* returns the container for that tile */
void     nav_go_tile(int tile_idx);     /* programmatic navigation */

#endif /* NAV_H */
