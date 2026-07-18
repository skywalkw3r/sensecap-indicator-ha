#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/* Minimal esp_netif stub for the desktop simulator.
 * settings_view.c queries the STA interface IP for its info label; the sim
 * has no network stack, so the handle lookup returns NULL and the caller's
 * `netif && esp_netif_get_ip_info(...) == ESP_OK` guard falls back to the
 * placeholder text — same rendering as a device with no IP yet.            */

typedef struct esp_netif_obj esp_netif_t;

typedef struct {
    uint32_t addr;
} esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

#define IPSTR "%d.%d.%d.%d"
#define esp_ip4_addr1_16(ipaddr) ((uint16_t)((ipaddr)->addr) & 0xFF)
#define esp_ip4_addr2_16(ipaddr) ((uint16_t)(((ipaddr)->addr) >> 8) & 0xFF)
#define esp_ip4_addr3_16(ipaddr) ((uint16_t)(((ipaddr)->addr) >> 16) & 0xFF)
#define esp_ip4_addr4_16(ipaddr) ((uint16_t)(((ipaddr)->addr) >> 24) & 0xFF)
#define IP2STR(ipaddr) esp_ip4_addr1_16(ipaddr), esp_ip4_addr2_16(ipaddr), \
    esp_ip4_addr3_16(ipaddr), esp_ip4_addr4_16(ipaddr)

static inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char *if_key)
{
    (void)if_key;
    return NULL;
}

static inline esp_err_t esp_netif_get_ip_info(esp_netif_t *esp_netif,
                                              esp_netif_ip_info_t *ip_info)
{
    (void)esp_netif;
    (void)ip_info;
    return ESP_FAIL;
}
