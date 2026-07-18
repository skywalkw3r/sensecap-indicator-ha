#pragma once
/* Sim shadow of main/mqtt/mqtt.h — just enough surface for ha_mqtt.h to
 * compile in the simulator. The real MQTT client never runs here. */

typedef struct instance_mqtt instance_mqtt;
struct instance_mqtt {
    int mqtt_connected_flag;
};
typedef instance_mqtt *instance_mqtt_t;
