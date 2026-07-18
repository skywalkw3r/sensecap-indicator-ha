#include "ha_trend_screen.h"

#include "esp_log.h"
#include "home_assistant_config.h"
#include "lv_port.h"
#include "nav.h"
#include "ui_components.h"
#include "ui_theme.h"
#include "view_data.h"

#include <stdint.h>

static const char *TAG = "ha-trend-screen";

/* Series order matches the display-value index contract: 0=temp 1=humidity
 * 2=co2. Temp + humidity live on the primary Y axis (0..100 covers °F and %);
 * CO2 has its own secondary axis because ppm sits on a wholly different scale. */
#define TREND_SERIES_COUNT CONFIG_HA_DISPLAY_VALUE_NUM

#define PRIMARY_Y_MIN   0     /* temp (°F) + humidity (%) */
#define PRIMARY_Y_MAX   100
#define SECONDARY_Y_MIN 400   /* CO2 (ppm) */
#define SECONDARY_Y_MAX 2000

/* Chart geometry inside the 480×480 tile. Left/right gutters hold the axis
 * anchor labels; the block below the header holds the legend. */
#define CHART_X 46
#define CHART_Y 128
#define CHART_W 388
#define CHART_H 296

static lv_obj_t        *s_chart;
static lv_chart_series_t *s_series[TREND_SERIES_COUNT];
static lv_obj_t        *s_empty_label;

/* Right-align a whole-series snapshot into the chart's y-array so the newest
 * sample always hugs the right edge; unfilled slots on the left stay hidden. */
static void _apply_series(lv_chart_series_t *ser, const struct view_data_ha_history *h)
{
    int32_t *ys = lv_chart_get_series_y_array(s_chart, ser);
    if (!ys) {
        return;
    }

    uint32_t n = lv_chart_get_point_count(s_chart);
    uint32_t count = (h->count > n) ? n : h->count;
    uint32_t pad = n - count;

    for (uint32_t i = 0; i < pad; i++) {
        ys[i] = LV_CHART_POINT_NONE;
    }
    for (uint32_t k = 0; k < count; k++) {
        /* Values are non-negative, so a plain +0.5 rounds to nearest without
         * pulling in libm. Sub-unit precision is carried by the stat cards. */
        ys[pad + k] = (int32_t)(h->samples[k] + 0.5f);
    }

    lv_chart_refresh(s_chart);
}

static void view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)id;

    const struct view_data_ha_history *h = (const struct view_data_ha_history *)event_data;
    if (h == NULL || h->index >= TREND_SERIES_COUNT) {
        return;
    }

    /* Posted from the view_event task, not the LVGL task — take the lock. */
    lv_port_sem_take();
    if (s_chart && s_series[h->index]) {
        _apply_series(s_series[h->index], h);
        if (h->count > 0 && s_empty_label) {
            lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_port_sem_give();
}

/* One "● Label unit" legend entry (coloured dot + text) inside a flex row. */
static void _legend_item(lv_obj_t *parent, lv_color_t color, const char *text)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, UI_SPACE_SM, 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(item);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, 6, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    ui_label(item, text, &lv_font_montserrat_16, UI_COLOR_TEXT);
}

static void _create_legend(lv_obj_t *tile)
{
    lv_obj_t *legend = lv_obj_create(tile);
    lv_obj_remove_style_all(legend);
    lv_obj_set_size(legend, 440, 26);
    lv_obj_set_pos(legend, 20, 92);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(legend, LV_OBJ_FLAG_SCROLLABLE);

    _legend_item(legend, UI_COLOR_AMBER, "Temp " CONFIG_HA_TEMP_UI_UNIT);
    _legend_item(legend, UI_COLOR_BLUE, CONFIG_HA_HUMIDITY_UI_NAME " " CONFIG_HA_HUMIDITY_UI_UNIT);
    _legend_item(legend, UI_COLOR_GREEN, CONFIG_HA_CO2_UI_NAME " " CONFIG_HA_CO2_UI_UNIT);
}

/* Small axis anchor label at an absolute tile position. */
static void _axis_label(lv_obj_t *tile, const char *text, lv_color_t color, int32_t x, int32_t y)
{
    lv_obj_t *lbl = ui_label(tile, text, &lv_font_montserrat_14, color);
    lv_obj_set_pos(lbl, x, y);
}

static void _create_chart(lv_obj_t *tile)
{
    s_chart = lv_chart_create(tile);
    lv_obj_set_size(s_chart, CHART_W, CHART_H);
    lv_obj_set_pos(s_chart, CHART_X, CHART_Y);
    lv_obj_remove_flag(s_chart, LV_OBJ_FLAG_SCROLLABLE);

    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, HA_HISTORY_MAX_SAMPLES);
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, PRIMARY_Y_MIN, PRIMARY_Y_MAX);
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_SECONDARY_Y, SECONDARY_Y_MIN, SECONDARY_Y_MAX);
    lv_chart_set_div_line_count(s_chart, 5, 7);

    /* Surface-card look from the shared tokens. */
    lv_obj_set_style_bg_color(s_chart, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_chart, UI_RADIUS_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_chart, UI_COLOR_OUTLINE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_chart, UI_SPACE_SM, LV_PART_MAIN);
    /* Subtle grid: division lines in the hairline colour. */
    lv_obj_set_style_line_color(s_chart, UI_COLOR_OUTLINE, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 1, LV_PART_MAIN);
    /* Series: rounded 3 px lines, no point markers for a clean trend. */
    lv_obj_set_style_line_width(s_chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(s_chart, true, LV_PART_ITEMS);
    lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);

    s_series[0] = lv_chart_add_series(s_chart, UI_COLOR_AMBER, LV_CHART_AXIS_PRIMARY_Y);
    s_series[1] = lv_chart_add_series(s_chart, UI_COLOR_BLUE, LV_CHART_AXIS_PRIMARY_Y);
    s_series[2] = lv_chart_add_series(s_chart, UI_COLOR_GREEN, LV_CHART_AXIS_SECONDARY_Y);
    for (int i = 0; i < TREND_SERIES_COUNT; i++) {
        if (s_series[i]) {
            lv_chart_set_all_values(s_chart, s_series[i], LV_CHART_POINT_NONE);
        }
    }

    /* Primary-axis anchors (temp/humidity, left) and secondary (CO2, right). */
    _axis_label(tile, "100", UI_COLOR_TEXT_MUTED, 14, CHART_Y - 2);
    _axis_label(tile, "0", UI_COLOR_TEXT_MUTED, 28, CHART_Y + CHART_H - 20);
    _axis_label(tile, "2000", UI_COLOR_GREEN, CHART_X + CHART_W + 4, CHART_Y - 2);
    _axis_label(tile, "400", UI_COLOR_GREEN, CHART_X + CHART_W + 4, CHART_Y + CHART_H - 20);
    /* Time direction: newest sample is pinned to the right edge. */
    lv_obj_t *now = ui_label(tile, "now " LV_SYMBOL_RIGHT, &lv_font_montserrat_14, UI_COLOR_TEXT_MUTED);
    lv_obj_set_pos(now, CHART_X + CHART_W - 52, CHART_Y + CHART_H + 4);

    /* Empty state until the first VIEW_EVENT_HA_HISTORY arrives. */
    s_empty_label = ui_label(s_chart, "Waiting for data...", UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
    lv_obj_center(s_empty_label);
}

void ha_trend_screen_init(void)
{
    lv_port_sem_take();
    lv_obj_t *tile = nav_get_tile(NAV_TILE_HA_TREND);
    if (!tile) {
        ESP_LOGE(TAG, "trend nav tile not initialized");
        lv_port_sem_give();
        return;
    }

    ui_header(tile, "Trends");
    _create_legend(tile);
    _create_chart(tile);
    lv_port_sem_give();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_HISTORY, view_event_handler, NULL, NULL));
}
