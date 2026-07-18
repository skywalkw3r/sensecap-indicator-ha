#ifndef HA_SWITCH_SCREEN_H
#define HA_SWITCH_SCREEN_H

#include "lvgl.h"

typedef struct ha_switch_screen ha_switch_screen_t;

// Builds the Home Assistant switch widgets on the nav tiles.
// Must be called after nav_init() has run.
ha_switch_screen_t *ha_switch_screen_create(void);

// Update widget state for switch at index [0..7].
// Caller must hold lv_port semaphore.
void ha_switch_screen_update(ha_switch_screen_t *s, int index, int value);

// Set the HA-pushed Bedroom/Loft temperature display (read-only card).
// Caller must hold lv_port semaphore. `value` is the numeric text, e.g. "72.4".
void ha_switch_screen_set_ha_temp(const char *value);

void ha_switch_screen_destroy(ha_switch_screen_t *s);

#endif /* HA_SWITCH_SCREEN_H */
