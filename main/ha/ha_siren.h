#ifndef HA_SIREN_H
#define HA_SIREN_H

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

void ha_siren_init(void);
void ha_siren_subscribe(esp_mqtt_client_handle_t client);
int  ha_siren_on_mqtt_data(const char *topic, int topic_len,
                           const char *data,  int data_len);

/* Shared entry for MQTT and the console 'beep' command. Unknown/NULL tone
 * falls back to "beep"; duration_s only applies to duration-bound tones
 * (currently "alarm") and 0 means the tone's default. */
void ha_siren_trigger(const char *tone, int duration_s);
void ha_siren_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_SIREN_H */
