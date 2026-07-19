/*
 * Buzzer as a Home Assistant MQTT siren entity.
 *
 * The buzzer sits on the RP2040 (GPIO19); the only primitive the coprocessor
 * offers is "one fixed ~50 ms chirp per PKT_TYPE_CMD_BEEP_ON packet" (the ms
 * payload is ignored by both this repo's RP2040 firmware and Seeed's stock
 * one). Tones are therefore sequenced on this side: an esp_timer re-sends the
 * chirp packet at the tone's cadence until its pulse count or duration is
 * spent.
 *
 * Commands arrive on CONFIG_TOPIC_SIREN_SET as JSON from the HA siren's
 * command_template — {"state":"ON","tone":"alarm","duration":15} — or as a
 * bare ON/OFF for hand testing. State echoes on CONFIG_TOPIC_SIREN_STATE as
 * {"state":"ON","tone":"..."} / {"state":"OFF"}.
 */
#include "ha_siren.h"
#include "ha_mqtt.h"
#include "home_assistant_config.h"
#include "rp2040.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <strings.h>

static const char *TAG = "ha-siren";

#define SIREN_MAX_DURATION_S 120

typedef struct {
    const char *name;
    uint32_t    period_ms;  /* chirp cadence */
    int         pulses;     /* fixed chirp count, or <0 = run until duration */
    int         default_s;  /* duration-bound tones: duration when none given */
} siren_tone_t;

static const siren_tone_t s_tones[] = {
    { "beep",     0,    1, 0  },
    { "doorbell", 200,  2, 0  },
    { "alarm",    250, -1, 10 },
};

static SemaphoreHandle_t  s_lock;
static esp_timer_handle_t s_timer;
static bool               s_active;
static const siren_tone_t *s_tone;
static int                s_pulses_left; /* fixed tones only */
static int64_t            s_stop_at_us;  /* duration-bound tones only */

static void publish_state(bool on, const char *tone)
{
    if (!mqtt_ha_instance.mqtt_connected_flag) {
        return;
    }
    char buf[64];
    int len = on ? snprintf(buf, sizeof(buf), "{\"state\": \"ON\", \"tone\": \"%s\"}", tone)
                 : snprintf(buf, sizeof(buf), "{\"state\": \"OFF\"}");
    if (len > 0 && len < (int)sizeof(buf)) {
        /* enqueue, not publish: this runs from the shared esp_timer task (and
         * under s_lock), where publish's synchronous socket write could stall
         * every timer in the system. Enqueue is memory-only; the MQTT task
         * transmits. */
        esp_mqtt_client_enqueue(mqtt_ha_instance.mqtt_client, CONFIG_TOPIC_SIREN_STATE,
                                buf, len, CONFIG_TOPIC_SIREN_QOS, 0, true);
    }
}

/* Caller holds s_lock. */
static void siren_finish_locked(void)
{
    if (s_active) {
        esp_timer_stop(s_timer);
        s_active = false;
    }
    rp2040_beep_stop(); /* belt & braces: force the buzzer pin low */
    publish_state(false, NULL);
}

static void siren_timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_active) { /* raced with a stop */
        xSemaphoreGive(s_lock);
        return;
    }
    bool done = (s_tone->pulses < 0) ? (esp_timer_get_time() >= s_stop_at_us)
                                     : (s_pulses_left <= 0);
    if (done) {
        siren_finish_locked();
    } else {
        rp2040_beep_pulse();
        if (s_tone->pulses >= 0) {
            s_pulses_left--;
        }
    }
    xSemaphoreGive(s_lock);
}

static const siren_tone_t *tone_lookup(const char *name)
{
    if (name != NULL && name[0] != '\0') {
        for (size_t i = 0; i < sizeof(s_tones) / sizeof(s_tones[0]); i++) {
            if (strcasecmp(name, s_tones[i].name) == 0) {
                return &s_tones[i];
            }
        }
        ESP_LOGW(TAG, "unknown tone '%s', falling back to beep", name);
    }
    return &s_tones[0];
}

void ha_siren_trigger(const char *tone_name, int duration_s)
{
    if (s_lock == NULL) {
        ESP_LOGW(TAG, "siren not initialized yet");
        return;
    }
    const siren_tone_t *tone = tone_lookup(tone_name);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active) {
        esp_timer_stop(s_timer);
        s_active = false;
    }

    rp2040_beep_pulse(); /* first chirp right away */

    if (tone->pulses == 1) {
        /* One-shot: momentary ON so HA automations see the trigger land. */
        publish_state(true, tone->name);
        publish_state(false, NULL);
        xSemaphoreGive(s_lock);
        return;
    }

    s_tone = tone;
    if (tone->pulses < 0) {
        if (duration_s <= 0) {
            duration_s = tone->default_s;
        }
        if (duration_s > SIREN_MAX_DURATION_S) {
            duration_s = SIREN_MAX_DURATION_S;
        }
        s_stop_at_us = esp_timer_get_time() + (int64_t)duration_s * 1000000;
    } else {
        s_pulses_left = tone->pulses - 1;
    }
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, (uint64_t)tone->period_ms * 1000));
    s_active = true;
    publish_state(true, tone->name);
    xSemaphoreGive(s_lock);
}

void ha_siren_stop(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    siren_finish_locked();
    xSemaphoreGive(s_lock);
}

void ha_siren_init(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = siren_timer_cb,
        .name     = "ha_siren",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_timer));
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
}

void ha_siren_subscribe(esp_mqtt_client_handle_t client)
{
    int msg_id = esp_mqtt_client_subscribe(client, CONFIG_TOPIC_SIREN_SET, CONFIG_TOPIC_SIREN_QOS);
    ESP_LOGI(TAG, "subscribe:%s, msg_id=%d", CONFIG_TOPIC_SIREN_SET, msg_id);
    if (s_lock == NULL) { /* init not run yet; nothing to sync */
        return;
    }
    /* Sync HA with the actual state after every (re)connect. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    publish_state(s_active, s_active ? s_tone->name : NULL);
    xSemaphoreGive(s_lock);
}

int ha_siren_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len)
{
    if (topic_len != (int)strlen(CONFIG_TOPIC_SIREN_SET) ||
        memcmp(topic, CONFIG_TOPIC_SIREN_SET, topic_len) != 0) {
        return -1;
    }

    char state_buf[8] = {0};
    const char *state = NULL;
    const char *tone  = NULL;
    int duration_s    = 0;

    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root != NULL && cJSON_IsObject(root)) {
        cJSON *item = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsString(item)) {
            state = item->valuestring;
        }
        item = cJSON_GetObjectItem(root, "tone");
        if (cJSON_IsString(item)) {
            tone = item->valuestring;
        }
        item = cJSON_GetObjectItem(root, "duration");
        if (cJSON_IsNumber(item)) {
            duration_s = item->valueint;
        }
    } else if (data_len > 0 && data_len < (int)sizeof(state_buf)) {
        /* Bare ON/OFF, e.g. mosquitto_pub -m ON */
        memcpy(state_buf, data, data_len);
        state = state_buf;
    }

    if (state != NULL && strcasecmp(state, "ON") == 0) {
        ESP_LOGI(TAG, "siren ON (tone=%s duration=%d)", tone ? tone : "beep", duration_s);
        ha_siren_trigger(tone, duration_s);
    } else if (state != NULL && strcasecmp(state, "OFF") == 0) {
        ESP_LOGI(TAG, "siren OFF");
        ha_siren_stop();
    } else {
        ESP_LOGW(TAG, "unrecognized siren payload: %.*s", data_len, data);
    }

    cJSON_Delete(root);
    return 0;
}
