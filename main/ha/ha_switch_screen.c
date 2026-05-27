#include "ha_switch_screen.h"

#include "esp_log.h"
#include "misc/lv_anim.h"
#include "ui.h"
#include "ui_helpers.h"
#include "widgets/lv_slider.h"

#include <stdlib.h>

static const char *TAG = "ha-switch-screen";

typedef void (*widget_update_fn)(lv_obj_t *widget, int value, void *aux);

typedef struct {
    lv_obj_t **widget_ptr;
    void *aux;
    widget_update_fn update;
} switch_slot_t;

struct ha_switch_screen {
    switch_slot_t slots[8];
};

static void _update_toggle(lv_obj_t *w, int value, void *aux)
{
    (void)aux;

    if (value) {
        lv_obj_add_state(w, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(w, LV_STATE_CHECKED);
    }
    lv_event_send(w, LV_EVENT_VALUE_CHANGED, NULL);
}

static void _update_arc(lv_obj_t *w, int value, void *aux)
{
    char buf[32];

    lv_snprintf(buf, sizeof(buf), "%d °C", value);
    if (aux) {
        lv_label_set_text((lv_obj_t *)aux, buf);
    }
    lv_arc_set_value(w, value);
    lv_event_send(w, LV_EVENT_VALUE_CHANGED, NULL);
}

static void _update_slider(lv_obj_t *w, int value, void *aux)
{
    (void)aux;

    lv_slider_set_value(w, value, LV_ANIM_ON);
    lv_event_send(w, LV_EVENT_VALUE_CHANGED, NULL);
}

static ha_switch_screen_t _instance;

ha_switch_screen_t *ha_switch_screen_create(void)
{
    ha_switch_screen_t *s = &_instance;

    s->slots[0] = (switch_slot_t){.widget_ptr = &ui_switch1, .aux = NULL, .update = _update_toggle};
    s->slots[1] = (switch_slot_t){.widget_ptr = &ui_switch2, .aux = NULL, .update = _update_toggle};
    s->slots[2] = (switch_slot_t){.widget_ptr = &ui_switch3, .aux = NULL, .update = _update_toggle};
    s->slots[3] = (switch_slot_t){.widget_ptr = &ui_switch4, .aux = NULL, .update = _update_toggle};
    s->slots[4] = (switch_slot_t){.widget_ptr = &ui_switch5_arc1, .aux = ui_switch5_arc_data1, .update = _update_arc};
    s->slots[5] = (switch_slot_t){.widget_ptr = &ui_switch6, .aux = NULL, .update = _update_toggle};
    s->slots[6] = (switch_slot_t){.widget_ptr = &ui_switch7, .aux = NULL, .update = _update_toggle};
    s->slots[7] = (switch_slot_t){.widget_ptr = &ui_switch8_slider1, .aux = NULL, .update = _update_slider};

    return s;
}

void ha_switch_screen_update(ha_switch_screen_t *s, int index, int value)
{
    if (!s || index < 0 || index >= 8) {
        ESP_LOGW(TAG, "invalid index %d", index);
        return;
    }

    lv_obj_t *w = *s->slots[index].widget_ptr;
    if (!w) {
        ESP_LOGW(TAG, "widget[%d] is NULL", index);
        return;
    }

    ESP_LOGI(TAG, "update index:%d value:%d", index, value);
    s->slots[index].update(w, value, s->slots[index].aux);
}

void ha_switch_screen_destroy(ha_switch_screen_t *s)
{
    (void)s;
}
