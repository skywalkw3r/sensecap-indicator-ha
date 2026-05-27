#include <string.h>

#include "ha.h"
#include "home_assistant_config.h"
#include "indicator_storage_nvs.h"
#include "lv_port.h"
#include "ui.h"
#include "indicator_util.h"
#include "esp_log.h"

#define MAX_BROKER_URL_LEN 128

static const char *TAG = "ha-config";

static const char *_get_broker_url(const void *event_data)
{
    if (event_data) {
        return (const char *)event_data;
    }

    static ha_cfg_interface ha_cfg;
    if (ha_cfg_get(&ha_cfg) == ESP_OK) {
        return ha_cfg.broker_url;
    }

    return NULL;
}

static void btn_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    LV_LOG_USER("Button %s clicked", lv_msgbox_get_active_btn_text(obj));
    if (lv_msgbox_get_active_btn_text(obj) == "OK") {
        lv_msgbox_close(obj);
    }
}

static void show_message_box(const char *message, lv_color_t color)
{
    static const char *btns[] = {"OK", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Notification", message, btns, true);

    lv_obj_set_style_bg_color(mbox, color, LV_PART_MAIN);
    lv_obj_set_style_text_color(mbox, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(mbox, btn_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static void update_ip_textfield(const char *broker_url)
{
    char ip[16];
    if (extract_ip_from_url(broker_url, ip, sizeof(ip))) {
        lv_textarea_set_text(ui_textarea_ip_0, ip);
    } else {
        ESP_LOGE(TAG, "Failed to extract IP from URL: %s", broker_url);
        lv_textarea_set_text(ui_textarea_ip_0, "");
    }
}

static void handle_mqtt_addr_change(const char *new_broker_ip)
{
    if (!is_valid_ipv4(new_broker_ip)) {
        ESP_LOGE(TAG, "Invalid IPv4 address: %s", new_broker_ip);
        show_message_box("Invalid IPv4 address", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    ha_cfg_interface ha_cfg;
    ha_cfg_get(&ha_cfg);

    char broker_url[MAX_BROKER_URL_LEN];
    assemble_broker_url(new_broker_ip, broker_url, sizeof(broker_url));

    if (strlcpy(ha_cfg.broker_url, broker_url, sizeof(ha_cfg.broker_url)) >= sizeof(ha_cfg.broker_url)) {
        ESP_LOGE(TAG, "Broker URL too long");
        show_message_box("Broker URL too long", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    if (ha_cfg_set(&ha_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save broker IP");
        show_message_box("Failed to save broker IP", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    ESP_LOGI(TAG, "Valid broker URL saved: %s", ha_cfg.broker_url);
    esp_event_post_to(ha_cfg_event_handle, HA_CFG_EVENT_BASE, HA_CFG_BROKER_CHANGED, ha_cfg.broker_url, sizeof(ha_cfg.broker_url), portMAX_DELAY);
    show_message_box("Broker IP updated successfully", lv_palette_main(LV_PALETTE_GREEN));
}

static void view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    lv_port_sem_take();

    switch (id) {
        case VIEW_EVENT_MQTT_ADDR_CHANGED: {
            const char *new_broker_ip = lv_textarea_get_text(ui_textarea_ip_0);
            handle_mqtt_addr_change(new_broker_ip);
            break;
        }
        case VIEW_EVENT_HA_ADDR_DISPLAY: {
            const char *broker_url = _get_broker_url(event_data);
            if (broker_url) {
                update_ip_textfield(broker_url);
            } else {
                ESP_LOGE(TAG, "Failed to get broker URL for display");
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "Unhandled event: %ld", id);
            break;
    }

    lv_port_sem_give();
}

esp_err_t ha_cfg_get(ha_cfg_interface *ha_cfg)
{
    int len = sizeof(ha_cfg_interface);
    memset(ha_cfg, 0, sizeof(ha_cfg_interface));
    esp_err_t err = indicator_nvs_read(MQTT_HA_CFG_STORAGE, ha_cfg, &len);
    if (err == ESP_OK && len == sizeof(ha_cfg_interface)) {
        ESP_LOGI(TAG, "mqtt broker cfg read successful");
    } else {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "mqtt broker cfg not find");
        } else {
            ESP_LOGI(TAG, "mqtt broker cfg read err:%d", err);
        }
        strlcpy(ha_cfg->broker_url, CONFIG_BROKER_URL, sizeof(ha_cfg->broker_url));
        strlcpy(ha_cfg->client_id, CONFIG_MQTT_CLIENT_ID, sizeof(ha_cfg->client_id));
        strlcpy(ha_cfg->username, CONFIG_MQTT_USERNAME, sizeof(ha_cfg->username));
        strlcpy(ha_cfg->password, CONFIG_MQTT_PASSWORD, sizeof(ha_cfg->password));
    }
    return err;
}

esp_err_t ha_cfg_set(ha_cfg_interface *cfg)
{
    esp_err_t err = indicator_nvs_write(MQTT_HA_CFG_STORAGE, cfg, sizeof(ha_cfg_interface));
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "ha cfg write err:%d", err);
    } else {
        ESP_LOGI(TAG, "ha cfg write successful");
    }
    return err;
}

void ha_config_view_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_MQTT_ADDR_CHANGED, view_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ADDR_DISPLAY, view_event_handler, NULL, NULL));
}
