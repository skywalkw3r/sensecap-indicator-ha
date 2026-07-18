#ifndef LV_PORT_H
#define LV_PORT_H

#include "lvgl.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize related work for lvgl.
 *
 */
void lv_port_init(void);

/**
 * @brief Take the semaphore.
 * @note  It should be called before manipulate lvgl gui.
 *
 */
void lv_port_sem_take(void);

/**
 * @brief Give the semaphore.
 * @note  It should be called after manipulate lvgl gui.
 *
 */
void lv_port_sem_give(void);

/**
 * @brief Touch-activity notification, raised from the LVGL touch-read path.
 * @param wake  true  = a press was read while the display was asleep; the
 *                      display domain should wake the screen (e.g. post
 *                      VIEW_EVENT_SCREEN_CTRL). The press is swallowed by the
 *                      port and never reaches a widget.
 *              false = a press was read while the display was awake; the
 *                      display domain should restart its sleep timer. The port
 *                      rate-limits these to at most once per second.
 * @note  Invoked in the LVGL task context. The callback must not block and must
 *        not call LVGL widget APIs; post an event or use the lv_port lock.
 */
typedef void (*lv_port_touch_cb_t)(bool wake);

/**
 * @brief Register the touch-activity callback (see lv_port_touch_cb_t).
 * @note  Keeps the port display-domain-agnostic: the port only signals touch
 *        activity/wake; the display domain owns the backlight and sleep timer.
 */
void lv_port_display_set_touch_cb(lv_port_touch_cb_t cb);

/**
 * @brief Set the display "asleep" state used by the touch-read filter.
 * @note  While asleep the port swallows touch input (no widget receives a
 *        press) and the first press raises the wake callback instead.
 */
void lv_port_display_sleep_set(bool asleep);

/**
 * @brief Get the display "asleep" state tracked by the port.
 */
bool lv_port_display_sleep_get(void);

#ifdef __cplusplus
}
#endif

#endif
