#pragma once
/* Sim shadow of main/storage/storage_nvs.h. Definitions in ha_cfg_stub.c:
 * reads report not-found (device presents as unconfigured), writes no-op. */

#include <stddef.h>
#include "esp_err.h"
#include "nvs.h"   /* stub: supplies ESP_ERR_NVS_NOT_FOUND */

#ifdef __cplusplus
extern "C" {
#endif

int       indicator_nvs_init(void);
esp_err_t indicator_nvs_write(char *p_key, void *p_data, size_t len);
esp_err_t indicator_nvs_read(char *p_key, void *p_data, size_t *p_len);

#ifdef __cplusplus
}
#endif
