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

// Set an HA-pushed display value (read-only cards on the Loft Controls page).
// index: 0=temperature 1=humidity 2=co2. Caller must hold lv_port semaphore.
// `value` is the numeric text, e.g. "72.4".
void ha_switch_screen_set_ha_value(int index, const char *value);

void ha_switch_screen_destroy(ha_switch_screen_t *s);

#endif /* HA_SWITCH_SCREEN_H */
