#include "ha_sensor.h"
#include "ha_mqtt.h"
#include "view_data.h"
#include "home_assistant_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define MAX_DATA_BUF_LEN 64

static const char *TAG = "ha-sensor";

typedef struct ha_sensor_entity {
    int index;
    char *key;
    char *topic;
    int qos;
} ha_sensor_entity_t;

static ha_sensor_entity_t ha_sensor_entities[CONFIG_HA_SENSOR_ENTITY_NUM];

static void publish_sensor_data(const struct view_data_sensor_data *sensor_data)
{
    if (!mqtt_ha_instance.mqtt_connected_flag) {
        return;
    }

    char data_buf[MAX_DATA_BUF_LEN];
    int len = 0;
    const char *topic = CONFIG_SENSOR_BUILDIN_TOPIC_DATA;
    const char *key = NULL;

    switch (sensor_data->sensor_type) {
        case SCD41_SENSOR_CO2:
            key = CONFIG_SENSOR_BUILDIN_CO2_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        case SGP40_SENSOR_TVOC:
            key = CONFIG_SENSOR_BUILDIN_TVOC_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        case SHT41_SENSOR_TEMP:
            key = CONFIG_SENSOR_BUILDIN_TEMP_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%.1f\"}", key, sensor_data->value);
            break;
        case SHT41_SENSOR_HUMIDITY:
            key = CONFIG_SENSOR_BUILDIN_HUMIDITY_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        default:
            ESP_LOGW(TAG, "Unknown sensor type: %d", sensor_data->sensor_type);
            return;
    }

    if (len > 0 && len < MAX_DATA_BUF_LEN) {
        esp_mqtt_client_publish(mqtt_ha_instance.mqtt_client, topic, data_buf, len, 0, 0);
    } else {
        ESP_LOGE(TAG, "Failed to format sensor data");
    }
}

static void view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (id == VIEW_EVENT_SENSOR_DATA) {
        ESP_LOGI(TAG, "event: VIEW_EVENT_SENSOR_DATA");
        publish_sensor_data((struct view_data_sensor_data *)event_data);
    }
}

void ha_sensor_init(void)
{
    const char *sensor_keys[] = CONFIG_SENSOR_VALUE_KEYS;
    const char *sensor_topics[] = CONFIG_SENSOR_TOPICS;

    for (int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++) {
        ha_sensor_entities[i] = (ha_sensor_entity_t){
            .index = i,
            .key = (char *)sensor_keys[i],
            .topic = (char *)sensor_topics[i],
            .qos = CONFIG_TOPIC_SENSOR_DATA_QOS,
        };
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA, view_event_handler, NULL, NULL));
}

void ha_sensor_subscribe(esp_mqtt_client_handle_t client)
{
    /* HA→device display values only. Deliberately NOT the device's own
     * publish topic (CONFIG_TOPIC_SENSOR_DATA) — subscribing to that echoed
     * every publish back into this parser for nothing. */
    int msg_id = esp_mqtt_client_subscribe(client, CONFIG_TOPIC_DISPLAY_SET, CONFIG_TOPIC_SENSOR_DATA_QOS);
    ESP_LOGI(TAG, "subscribe:%s, msg_id=%d", CONFIG_TOPIC_DISPLAY_SET, msg_id);
}

int ha_sensor_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len)
{
    if (topic_len != (int)strlen(CONFIG_TOPIC_DISPLAY_SET) ||
        memcmp(topic, CONFIG_TOPIC_DISPLAY_SET, topic_len) != 0) {
        return -1;
    }

    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root == NULL) {
        ESP_LOGE(TAG, "Invalid JSON structure");
        return -1;
    }

    /* Home Assistant templates publish numbers or strings depending on how the
     * automation is written — accept both. */
    cJSON *item = cJSON_GetObjectItem(root, CONFIG_HA_TEMP_VALUE_KEY);
    if (item == NULL || (!cJSON_IsNumber(item) && !cJSON_IsString(item))) {
        ESP_LOGW(TAG, "No usable '%s' key on %s", CONFIG_HA_TEMP_VALUE_KEY, CONFIG_TOPIC_DISPLAY_SET);
        cJSON_Delete(root);
        return -1;
    }

    struct view_data_ha_sensor_data sensor_data = {.index = 0};
    if (cJSON_IsNumber(item)) {
        snprintf(sensor_data.value, sizeof(sensor_data.value), "%.1f", item->valuedouble);
    } else {
        strncpy(sensor_data.value, item->valuestring, sizeof(sensor_data.value) - 1);
    }

    ESP_LOGI(TAG, "HA display value %s = %s", CONFIG_HA_TEMP_VALUE_KEY, sensor_data.value);
    /* Consumed by ha_switch.c's VIEW_EVENT_HA_SENSOR handler, which updates
     * the Bedroom/Loft temperature card under the LVGL lock. */
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SENSOR, &sensor_data, sizeof(sensor_data), portMAX_DELAY);
    cJSON_Delete(root);
    return 0;
}
