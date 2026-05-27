#ifndef HA_H
#define HA_H

#include "view_data.h"
#include "indicator_mqtt.h"
#include "ha_switch_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_HA_CFG_STORAGE "ha-mqtt"

ESP_EVENT_DECLARE_BASE(HA_CFG_EVENT_BASE);
extern esp_event_loop_handle_t ha_cfg_event_handle;
extern instance_mqtt mqtt_ha_instance;

enum HA_CFG_EVENT {
    HA_CFG_SET,
    HA_CFG_BROKER_CHANGED,
    HA_CFG_EVENT_ALL,
};

typedef struct {
    char broker_url[128];
    char client_id[16];
    char username[32];
    char password[64];
} ha_cfg_interface;

esp_err_t ha_cfg_get(ha_cfg_interface *cfg);
esp_err_t ha_cfg_set(ha_cfg_interface *cfg);

int indicator_ha_model_init(void);
int indicator_ha_view_init(void);

void ha_sensor_init(void);
void ha_switch_init(void);
void ha_switch_attach_screen(ha_switch_screen_t *screen);
void ha_config_view_init(void);

void ha_sensor_subscribe(esp_mqtt_client_handle_t client);
void ha_switch_subscribe(esp_mqtt_client_handle_t client);

int ha_sensor_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len);
int ha_switch_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len);

#ifdef __cplusplus
}
#endif

#endif /* HA_H */
