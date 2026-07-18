#include "ha_ws_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "storage_nvs.h"

static const char *TAG = "ha-ws-cfg";

esp_err_t ha_ws_cfg_get(ha_ws_cfg_t *cfg)
{
    size_t len = sizeof(ha_ws_cfg_t);
    memset(cfg, 0, sizeof(ha_ws_cfg_t));
    esp_err_t err = indicator_nvs_read(HA_WS_CFG_STORAGE, cfg, &len);
    if (err == ESP_OK && len == sizeof(ha_ws_cfg_t)) {
        return ESP_OK;
    }

    /* No valid stored config. Return a zeroed (disabled) config plus a
     * distinct status so callers treat the device as unconfigured. */
    memset(cfg, 0, sizeof(ha_ws_cfg_t));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no HA WebSocket config stored — configure via 'setha'");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (err == ESP_OK) {
        /* Read succeeded but the blob size no longer matches the struct. */
        ESP_LOGW(TAG, "stored HA WebSocket config invalid (size mismatch) — reconfigure");
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGW(TAG, "HA WebSocket cfg read err:%d — treating as unconfigured", err);
    return err;
}

esp_err_t ha_ws_cfg_set(ha_ws_cfg_t *cfg)
{
    esp_err_t err = indicator_nvs_write(HA_WS_CFG_STORAGE, cfg, sizeof(ha_ws_cfg_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HA WebSocket cfg write err:%d", err);
    } else {
        ESP_LOGI(TAG, "HA WebSocket cfg write successful");
    }
    return err;
}

bool ha_ws_url_normalize(const char *input, char *output, size_t output_size)
{
    if (input == NULL || input[0] == '\0' || output == NULL || output_size == 0) {
        return false;
    }

    bool tls = false;
    const char *p = input;
    if (strncmp(p, "wss://", 6) == 0) {
        tls = true;
        p += 6;
    } else if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
    } else if (strncmp(p, "https://", 8) == 0) {
        tls = true;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    /* Host, up to an optional ":port" or path. Capped so the assembled URL
     * always fits the 128-byte config field. */
    char host[100];
    size_t h = 0;
    while (*p != '\0' && *p != ':' && *p != '/') {
        if (h >= sizeof(host) - 1) {
            return false; /* host too long */
        }
        host[h++] = *p++;
    }
    host[h] = '\0';
    if (h == 0) {
        return false; /* empty host */
    }

    unsigned long port = tls ? 443 : 8123;
    if (*p == ':') {
        p++;
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        char *end = NULL;
        port = strtoul(p, &end, 10);
        if (port == 0 || port > 65535 || (*end != '\0' && *end != '/')) {
            return false;
        }
    }
    /* Anything after '/' (a path) is discarded — the API path is fixed. */

    int written = snprintf(output, output_size, "%s://%s:%lu/api/websocket",
                           tls ? "wss" : "ws", host, port);
    return written > 0 && (size_t)written < output_size;
}
