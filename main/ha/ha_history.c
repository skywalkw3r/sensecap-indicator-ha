#include "ha_history.h"

#include "esp_log.h"
#include "home_assistant_config.h"
#include "view_data.h"

#include <stdlib.h>
#include <string.h>

/*
 * Model-only translation unit: it maintains numeric history state and speaks
 * exclusively over the event bus. It deliberately owns no widgets and makes no
 * LVGL calls — the trends view (ha_trend_screen.c) renders the snapshots this
 * file posts.
 */

static const char *TAG = "ha-history";

/* Per-series circular buffer. `next` is the write cursor; once `count` reaches
 * the cap the oldest sample lives at `next` and is overwritten in place. */
typedef struct {
    float    buf[HA_HISTORY_MAX_SAMPLES];
    uint16_t count; /* valid samples, saturating at HA_HISTORY_MAX_SAMPLES */
    uint16_t next;  /* index of the next write (wraps) */
} series_ring_t;

static series_ring_t s_series[CONFIG_HA_DISPLAY_VALUE_NUM];

static void ring_push(series_ring_t *r, float value)
{
    r->buf[r->next] = value;
    r->next = (uint16_t)((r->next + 1u) % HA_HISTORY_MAX_SAMPLES);
    if (r->count < HA_HISTORY_MAX_SAMPLES) {
        r->count++;
    }
}

/* Linearise the ring into `out` oldest→newest; returns the sample count. */
static uint16_t ring_snapshot(const series_ring_t *r, float *out)
{
    /* Before the first wrap the samples sit at [0, count); after it, the oldest
     * is at `next` (the slot about to be overwritten). */
    uint16_t start = (r->count < HA_HISTORY_MAX_SAMPLES) ? 0u : r->next;
    for (uint16_t i = 0; i < r->count; i++) {
        out[i] = r->buf[(start + i) % HA_HISTORY_MAX_SAMPLES];
    }
    return r->count;
}

static void view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)id;

    const struct view_data_ha_sensor_data *sensor = (const struct view_data_ha_sensor_data *)event_data;
    if (sensor == NULL || sensor->index >= CONFIG_HA_DISPLAY_VALUE_NUM) {
        return;
    }

    /* HA templates publish numbers or strings; ha_sensor.c already normalised
     * them into a decimal string. Reject anything unparseable rather than
     * charting a bogus 0. */
    char *end = NULL;
    float value = strtof(sensor->value, &end);
    if (end == sensor->value) {
        ESP_LOGW(TAG, "non-numeric value for series %u: '%s'", sensor->index, sensor->value);
        return;
    }

    series_ring_t *r = &s_series[sensor->index];
    ring_push(r, value);

    struct view_data_ha_history snapshot = {.index = sensor->index};
    snapshot.count = ring_snapshot(r, snapshot.samples);

    /*
     * Re-post the whole-series snapshot. This handler runs in the view_event
     * task, so the post must be bounded: portMAX_DELAY into the same loop this
     * task drains would self-deadlock if the queue were full. A dropped tick
     * just skips one chart repaint; the next sample re-posts the full window.
     */
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_HISTORY,
                                      &snapshot, sizeof(snapshot), pdMS_TO_TICKS(20));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drop VIEW_EVENT_HA_HISTORY (series %u): %s", sensor->index, esp_err_to_name(err));
    }
}

void ha_history_init(void)
{
    memset(s_series, 0, sizeof(s_series));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SENSOR, view_event_handler, NULL, NULL));
}
