/* Stub deps for compiling main/ha/ha_config.c in the simulator.
 *
 * - HA_CFG_EVENT_BASE / ha_cfg_event_handle: the confirm flow posts
 *   HA_CFG_BROKER_CHANGED here; no sim handler registers on it, and the
 *   esp_event stub matches entries by loop pointer, so a NULL handle
 *   dispatches to nothing (never dereferenced).
 * - indicator_nvs_*: reads report not-found so the modal presents as an
 *   unconfigured device (placeholder text, mqtt:// preselected); writes no-op.
 */

#include <stddef.h>

#include "esp_event.h"
#include "storage_nvs.h"

ESP_EVENT_DEFINE_BASE(HA_CFG_EVENT_BASE);
esp_event_loop_handle_t ha_cfg_event_handle = NULL;

int indicator_nvs_init(void) { return 0; }

esp_err_t indicator_nvs_write(char *p_key, void *p_data, size_t len)
{
    (void)p_key; (void)p_data; (void)len;
    return ESP_OK;
}

esp_err_t indicator_nvs_read(char *p_key, void *p_data, size_t *p_len)
{
    (void)p_key; (void)p_data; (void)p_len;
    return ESP_ERR_NVS_NOT_FOUND;
}
