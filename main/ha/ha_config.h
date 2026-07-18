#ifndef HA_CONFIG_H
#define HA_CONFIG_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_HA_CFG_STORAGE "ha-mqtt"

/* MQTT client on/off switch. Own NVS key (ha_cfg_interface's size is an NVS
 * contract — never grow it). Missing key = enabled, so existing devices keep
 * their behavior. The MQTT and WebSocket clients run one-at-a-time: enabling
 * one from the console disables the other (WS wins if NVS ever says both). */
#define MQTT_ENABLED_STORAGE "mqtt-en"

typedef struct {
    char broker_url[128];
    char client_id[16];
    char username[32];
    char password[64];
} ha_cfg_interface;

esp_err_t ha_cfg_get(ha_cfg_interface *cfg);
esp_err_t ha_cfg_set(ha_cfg_interface *cfg);
bool      ha_mqtt_enabled_get(void);
esp_err_t ha_mqtt_enabled_set(bool enabled);
void      ha_config_view_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_CONFIG_H */
