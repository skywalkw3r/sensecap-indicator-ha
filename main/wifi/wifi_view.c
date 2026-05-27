#include "wifi_view.h"
#include "wifi_list_screen.h"
#include "wifi_connect_screen.h"
#include "view_data.h"
#include "lv_port.h"
#include "indicator_util.h"

/* THE ONLY FILE in wifi/ that imports ui.h / Squareline globals. */
#include "ui.h"

static const char *TAG = "wifi-view";

static wifi_list_screen_t    *s_list_screen    = NULL;
static wifi_connect_screen_t *s_connect_screen = NULL;

/* ── list item tap callbacks (live here to access module state) ───────── */

static void _on_unconnected_tap(lv_event_t *e) {
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(lv_indev_get_type(lv_indev_get_act()) != LV_INDEV_TYPE_POINTER) return;
    if(s_connect_screen != NULL) return; /* dialog already open */

    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = wifi_list_screen_get_item_ssid(s_list_screen, btn);
    bool have_password = (lv_obj_get_child_cnt(btn) > 2);
    s_connect_screen = wifi_connect_screen_show(ssid, have_password);
}

static void _on_connected_tap(lv_event_t *e) {
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(lv_indev_get_type(lv_indev_get_act()) != LV_INDEV_TYPE_POINTER) return;
    if(s_connect_screen != NULL) return;

    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = wifi_list_screen_get_item_ssid(s_list_screen, btn);
    s_connect_screen = wifi_details_screen_show(ssid);
}

/* ── connection result toast ─────────────────────────────────────────── */

static lv_obj_t *s_result_toast = NULL;

static void _toast_close_cb(lv_timer_t *timer) {
    if(s_result_toast) {
        lv_obj_del(s_result_toast);
        s_result_toast = NULL;
        lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
    }
}

static void _show_connect_result(struct view_data_wifi_connet_ret_msg *p_msg) {
    s_result_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_result_toast, 300, 150);
    lv_obj_set_align(s_result_toast, LV_ALIGN_CENTER);
    lv_obj_clear_flag(s_result_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_result_toast, lv_palette_main(LV_PALETTE_GREY), 0);

    lv_obj_t *msg = lv_label_create(s_result_toast);
    lv_label_set_text(msg, p_msg->msg);
    lv_obj_set_align(msg, LV_ALIGN_CENTER);

    lv_timer_t *timer = lv_timer_create(_toast_close_cb, 1500, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

/* ── event handler ───────────────────────────────────────────────────── */

static void _view_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t id, void *event_data) {
    switch(id) {
        case VIEW_EVENT_SCREEN_START: {
            uint8_t screen = *(uint8_t *)event_data;
            if(screen == SCREEN_WIFI_CONFIG) {
                ESP_LOGI(TAG, "navigate to wifi screen");
                lv_port_sem_take();
                _ui_screen_change(&ui_screen_wifi, LV_SCR_LOAD_ANIM_OVER_BOTTOM,
                                  200, 0, &ui_screen_wifi_screen_init);
                lv_port_sem_give();
            }
            break;
        }
        case VIEW_EVENT_WIFI_ST: {
            struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
            const void *src = NULL;

            if(p_st->is_connected) {
                switch(wifi_rssi_level_get(p_st->rssi)) {
                    case 1: src = &ui_img_wifi_1_png; break;
                    case 2: src = &ui_img_wifi_2_png; break;
                    case 3: src = &ui_img_wifi_3_png; break;
                    default: break;
                }
            } else {
                src = &ui_img_wifi_disconet_png;
            }

            lv_port_sem_take();
            lv_img_set_src(ui_wifi_st_0,  src);
            lv_img_set_src(ui_wifi_st_1,  src);
            lv_img_set_src(ui_wifi_st_2,  src);
            lv_img_set_src(ui_wifi_st_3,  src);
            lv_img_set_src(ui_wifi_st_4,  src);
            lv_img_set_src(ui_wifi_st_01, src);
            lv_port_sem_give();
            break;
        }
        case VIEW_EVENT_WIFI_LIST_START:
            lv_port_sem_take();
            wifi_list_screen_show_spinner(s_list_screen);
            lv_port_sem_give();
            break;

        case VIEW_EVENT_WIFI_CONNECT:
            /* Model picks this up to connect; view shows spinner. */
            lv_port_sem_take();
            wifi_list_screen_show_spinner(s_list_screen);
            s_connect_screen = NULL; /* already dismissed by connect_screen itself */
            lv_port_sem_give();
            break;

        case VIEW_EVENT_WIFI_LIST_REQ:
            /* Show spinner while scan is underway. */
            lv_port_sem_take();
            wifi_list_screen_show_spinner(s_list_screen);
            lv_port_sem_give();
            break;

        case VIEW_EVENT_WIFI_LIST: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_LIST");
            struct view_data_wifi_list *p_list = (struct view_data_wifi_list *)event_data;
            lv_port_sem_take();
            wifi_list_screen_update(s_list_screen, p_list);
            lv_port_sem_give();
            break;
        }
        case VIEW_EVENT_WIFI_CONNECT_RET: {
            struct view_data_wifi_connet_ret_msg *p_data =
                (struct view_data_wifi_connet_ret_msg *)event_data;

            if(lv_scr_act() != ui_screen_wifi) break;

            /* Refresh list then show result toast. */
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                              VIEW_EVENT_WIFI_LIST_REQ, NULL, 0, portMAX_DELAY);

            lv_port_sem_take();
            _show_connect_result(p_data);
            lv_port_sem_give();
            break;
        }
        default:
            break;
    }
}

/* ── init ────────────────────────────────────────────────────────────── */

int indicator_wifi_view_init(void) {
    /* Create screen components — ui_init() has already run at this point. */
    s_list_screen = wifi_list_screen_create(ui_screen_wifi, ui_wifi_scan_wait);
    wifi_list_screen_set_item_callbacks(s_list_screen, _on_connected_tap, _on_unconnected_tap);

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_START,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CFG_DELETE,
        _view_event_handler, NULL, NULL));

    return 0;
}
