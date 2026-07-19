#include "cmd.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"

#include "home_assistant_config.h"
#include "ha.h"
#include "ha_tls.h"
#include "storage_nvs.h"
#include "indicator_util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

static const char *TAG = "CMD_RESP";

#define PROMPT_STR "Indicator"

static ha_cfg_interface ha_cfg;

static void print_ha_ws_usage(void) {
    printf("\nHome Assistant WebSocket (live dashboard + service calls, no MQTT broker)\n");
    printf("  Show current config:\n");
    printf("    haconfig\n\n");
    printf("  Configure (fields merge into the stored config, so steps can be split):\n");
    printf("    setha -a 192.168.1.10 -t <long-lived-token>\n");
    printf("    setha --enable\n\n");
    printf("  Turn off (falls back to MQTT: the client is re-enabled automatically):\n");
    printf("    setha --disable\n\n");
    printf("  Notes:\n");
    printf("    - Dashboard rooms/entities are a compile-time table: edit\n");
    printf("      main/dashboard_config.h and rebuild to change what the panel shows.\n");
    printf("    - One transport at a time: 'setha --enable' stops the MQTT client\n");
    printf("      (dashboard controls need WS), 'setha --disable' brings MQTT back\n");
    printf("      (panel degrades to read-only Loft values + Trends).\n");
    printf("    - Mint the token in HA: Profile -> Security -> Long-lived access tokens.\n");
    printf("    - Address forms: 192.168.1.10 or ha.local:8123 (-> ws://...:8123),\n");
    printf("      wss://ha.example.com or https://xyz.ui.nabu.casa (-> wss://...:443).\n");
    printf("    - wss:// uses the same trust settings as mqtts://: 'setmqttca' for a\n");
    printf("      private CA, 'setmqtt --insecure/--secure' toggles verification.\n\n");
}

static void print_mqtt_usage(void) {
    printf("\nMQTT configuration\n");
    printf("  Show current config:\n");
    printf("    haconfig\n\n");
    printf("  Set broker, client id and credentials:\n");
    printf("    setmqtt -a 192.168.1.10 -c indicator-01 -u mqtt_user -p mqtt_password\n");
    printf("    setmqtt --addr mqtt://192.168.1.10:1883\n\n");
    printf("  TLS (mqtts://):\n");
    printf("    setmqtt --addr mqtts://192.168.1.10:8883      (port defaults to 8883)\n");
    printf("    setmqttca            paste your CA certificate PEM, then it is stored\n");
    printf("    setmqttca -c         clear the stored CA (fall back to public CA bundle)\n");
    printf("    setmqtt --insecure   encrypt but skip server verification (discouraged)\n\n");
    printf("  Notes:\n");
    printf("    - The screen MQTT page only asks for the broker IP. It builds mqtt://<ip>:1883.\n");
    printf("    - TLS setup is console-only; a private-CA broker needs 'setmqttca' first.\n");
    printf("    - Restart is automatic after setmqtt/setmqttca succeeds.\n");
    printf("    - 'setmqtt --enable' / '--disable' toggles the MQTT client. Enabling it\n");
    printf("      turns the WebSocket client off (one transport at a time).\n\n");
    printf("MQTT topics and payloads\n");
    printf("  Sensor data from device:\n");
    printf("    topic: %s\n", CONFIG_TOPIC_SENSOR_DATA);
    printf("    data : {\"temp\":\"23.5\"}, {\"humidity\":\"55\"}, {\"co2\":\"600\"}, {\"tvoc\":\"12\"}\n\n");
    printf("  Push display values to the panel (read-only fallback when WS is off):\n");
    printf("    topic: %s\n", CONFIG_TOPIC_DISPLAY_SET);
    printf("    data : {\"loft_temp\":72.4}, {\"loft_humidity\":45,\"loft_co2\":620}\n\n");
    printf("  Sound the buzzer (HA MQTT siren entity; 'beep' tests it locally):\n");
    printf("    topic: %s\n", CONFIG_TOPIC_SIREN_SET);
    printf("    data : {\"state\":\"ON\",\"tone\":\"doorbell\"}, {\"state\":\"ON\",\"tone\":\"alarm\",\"duration\":15},\n");
    printf("           {\"state\":\"OFF\"} — or bare ON / OFF\n");
    printf("    state: %s\n\n", CONFIG_TOPIC_SIREN_STATE);
}

static bool normalize_broker_url(const char *input, char *output, size_t output_size) {
    if (!input || input[0] == '\0' || !output || output_size == 0) return false;

    int written = 0;
    if (strncmp(input, "mqtt://", 7) == 0 || strncmp(input, "mqtts://", 8) == 0)
        written = snprintf(output, output_size, "%s", input);
    else
        written = snprintf(output, output_size, "mqtt://%s", input);

    return written > 0 && (size_t)written < output_size;
}

/* Shared by the MQTT (mqtts://) and WebSocket (wss://) blocks — both ride on
 * the same stored trust settings. */
static void print_tls_trust_rows(void) {
    size_t ca_len = 0;
    char *ca = ha_tls_ca_load(&ca_len);
    ESP_LOGI(TAG, "| TLS mode                     | %-40s |",
             ha_tls_mode_get() == HA_TLS_MODE_INSECURE ? "INSECURE (no verification)" : "verify");
    if (ca != NULL) {
        char ca_status[41];
        snprintf(ca_status, sizeof(ca_status), "stored (%u bytes)", (unsigned)ca_len);
        ESP_LOGI(TAG, "| TLS custom CA                | %-40s |", ca_status);
        free(ca);
    } else {
        ESP_LOGI(TAG, "| TLS custom CA                | %-40s |", "(none — public CA bundle)");
    }
}

static int read_ha_config(int argc, char **argv) {
    esp_err_t err = ha_cfg_get(&ha_cfg);
    if (err != ESP_OK || ha_cfg.broker_url[0] == '\0') {
        ESP_LOGI(TAG, "MQTT not configured — set a broker with 'setmqtt' (see 'mqtthelp').");
    } else {
        ESP_LOGI(TAG, "| MQTT client                  | %-40s |",
                 ha_mqtt_enabled_get() ? "enabled" : "disabled");
        ESP_LOGI(TAG, "| Broker Address               | %-40s |", ha_cfg.broker_url);
        ESP_LOGI(TAG, "| Client ID                    | %-40s |", ha_cfg.client_id);
        ESP_LOGI(TAG, "| MQTT username                | %-40s |", ha_cfg.username);
        ESP_LOGI(TAG, "| MQTT password                | %-40s |", ha_cfg.password[0] ? "****" : "(not set)");
        if (ha_tls_url_is_tls(ha_cfg.broker_url)) {
            print_tls_trust_rows();
        } else {
            ESP_LOGI(TAG, "| TLS                          | %-40s |", "off (mqtt:// broker URL)");
        }
    }

    ha_ws_cfg_t ws_cfg;
    ha_ws_cfg_get(&ws_cfg);
    if (!ws_cfg.enabled && ws_cfg.url[0] == '\0' && ws_cfg.token[0] == '\0') {
        ESP_LOGI(TAG, "HA WebSocket not configured — see the 'setha' section in 'mqtthelp'.");
    } else {
        int subscribable = 0;
        for (int i = 0; i < DASH_SLOT_COUNT; i++) {
            if (dash_slot_subscribable(i)) subscribable++;
        }
        char entities_row[48];
        snprintf(entities_row, sizeof(entities_row), "%d dashboard slots (compile-time)", subscribable);
        ESP_LOGI(TAG, "| HA WebSocket                 | %-40s |", ws_cfg.enabled ? "enabled" : "disabled");
        ESP_LOGI(TAG, "| HA WebSocket URL             | %-40s |", ws_cfg.url[0] ? ws_cfg.url : "(not set)");
        ESP_LOGI(TAG, "| HA access token              | %-40s |", ws_cfg.token[0] ? "**** (set)" : "(not set)");
        ESP_LOGI(TAG, "| Entities                     | %-40s |", entities_row);
        if (strncmp(ws_cfg.url, "wss://", 6) == 0) {
            print_tls_trust_rows();
        }
    }

    ESP_LOGI(TAG, "Run 'mqtthelp' for setmqtt/setha examples and MQTT topic/payload examples.");
    return 0;
}

static int mqtt_help(int argc, char **argv) {
    print_mqtt_usage();
    print_ha_ws_usage();
    return 0;
}

static void register_read_config(void) {
    const esp_console_cmd_t cmd = {
        .command = "haconfig",
        .help    = "Show current MQTT broker/client configuration",
        .hint    = NULL,
        .func    = &read_ha_config,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_mqtt_help(void) {
    const esp_console_cmd_t cmd = {
        .command = "mqtthelp",
        .help    = "Show MQTT setup examples, topics and payloads",
        .hint    = NULL,
        .func    = &mqtt_help,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

struct {
    struct arg_str *username;
    struct arg_str *password;
    struct arg_str *broker_url;
    struct arg_str *client_id;
    struct arg_lit *insecure;
    struct arg_lit *secure;
    struct arg_lit *enable;
    struct arg_lit *disable;
    struct arg_end *end;
} mqtt_args;

static int mqtt_config_set(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&mqtt_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, mqtt_args.end, argv[0]);
        print_mqtt_usage();
        return 1;
    }

    if (!mqtt_args.username->count && !mqtt_args.password->count &&
        !mqtt_args.broker_url->count && !mqtt_args.client_id->count &&
        !mqtt_args.insecure->count && !mqtt_args.secure->count &&
        !mqtt_args.enable->count && !mqtt_args.disable->count) {
        print_mqtt_usage();
        return 0;
    }

    if (mqtt_args.insecure->count > 0 && mqtt_args.secure->count > 0) {
        ESP_LOGE(TAG, "--insecure and --secure are mutually exclusive");
        return 1;
    }
    if (mqtt_args.enable->count > 0 && mqtt_args.disable->count > 0) {
        ESP_LOGE(TAG, "--enable and --disable are mutually exclusive");
        return 1;
    }

    if (mqtt_args.enable->count > 0) {
        if (ha_mqtt_enabled_set(true) != ESP_OK) return 1;
        ESP_LOGI(TAG, "MQTT client enabled");
        /* One transport at a time: switching to MQTT turns WebSocket off. */
        ha_ws_cfg_t ws_cfg;
        ha_ws_cfg_get(&ws_cfg);
        if (ws_cfg.enabled) {
            ws_cfg.enabled = 0;
            if (ha_ws_cfg_set(&ws_cfg) != ESP_OK) return 1;
            ESP_LOGI(TAG, "HA WebSocket client disabled (one transport at a time)");
        }
    }
    if (mqtt_args.disable->count > 0) {
        if (ha_mqtt_enabled_set(false) != ESP_OK) return 1;
        ESP_LOGI(TAG, "MQTT client disabled");
    }
    if (mqtt_args.enable->count > 0 || mqtt_args.disable->count > 0 ||
        mqtt_args.insecure->count > 0 || mqtt_args.secure->count > 0) {
        /* Rebuild the WS side too: applies a WS shutdown on --enable, refreshes
         * the settings status modal, and — since the TLS trust settings are
         * shared with wss:// — makes --insecure/--secure take effect on a
         * running WebSocket client (its config is baked at client init). */
        esp_event_post_to(ha_cfg_event_handle, HA_WS_EVENT_BASE, HA_WS_CFG_CHANGED, NULL, 0, portMAX_DELAY);
    }
    if (mqtt_args.insecure->count > 0) {
        if (ha_tls_mode_set(HA_TLS_MODE_INSECURE) != ESP_OK) return 1;
        ESP_LOGW(TAG, "TLS verification DISABLED — the broker's certificate will not be checked.");
    }
    if (mqtt_args.secure->count > 0) {
        if (ha_tls_mode_set(HA_TLS_MODE_VERIFY) != ESP_OK) return 1;
        ESP_LOGI(TAG, "TLS verification enabled (stored CA if set, else public CA bundle).");
    }

    ha_cfg_get(&ha_cfg);

    if (mqtt_args.username->count > 0) {
        memset(ha_cfg.username, 0, sizeof(ha_cfg.username));
        strncpy(ha_cfg.username, mqtt_args.username->sval[0], sizeof(ha_cfg.username) - 1);
        ESP_LOGI(TAG, "Set MQTT username: %s", ha_cfg.username);
    }
    if (mqtt_args.password->count > 0) {
        memset(ha_cfg.password, 0, sizeof(ha_cfg.password));
        strncpy(ha_cfg.password, mqtt_args.password->sval[0], sizeof(ha_cfg.password) - 1);
        ESP_LOGI(TAG, "Set MQTT password: %s", ha_cfg.password[0] ? "****" : "(not set)");
    }
    if (mqtt_args.broker_url->count > 0) {
        char broker_url[sizeof(ha_cfg.broker_url)];
        if (!normalize_broker_url(mqtt_args.broker_url->sval[0], broker_url, sizeof(broker_url))) {
            ESP_LOGE(TAG, "Invalid or too long broker URL: %s", mqtt_args.broker_url->sval[0]);
            return 1;
        }
        memset(ha_cfg.broker_url, 0, sizeof(ha_cfg.broker_url));
        strncpy(ha_cfg.broker_url, broker_url, sizeof(ha_cfg.broker_url) - 1);
        ESP_LOGI(TAG, "Set MQTT broker URL: %s", ha_cfg.broker_url);
    }
    if (mqtt_args.client_id->count > 0) {
        memset(ha_cfg.client_id, 0, sizeof(ha_cfg.client_id));
        strncpy(ha_cfg.client_id, mqtt_args.client_id->sval[0], sizeof(ha_cfg.client_id) - 1);
        ESP_LOGI(TAG, "Set MQTT client ID: %s", ha_cfg.client_id);
    }

    if (ha_cfg_set(&ha_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MQTT configuration");
        return 1;
    }
    esp_event_post_to(ha_cfg_event_handle, HA_CFG_EVENT_BASE, HA_CFG_SET, NULL, 0, portMAX_DELAY);
    ESP_LOGI(TAG, "MQTT configuration saved. Reconnecting MQTT client.");
    return 0;
}

static void register_mqtt_config(void) {
    mqtt_args.username   = arg_str0("u", "usr", "<username>", "MQTT username");
    mqtt_args.password   = arg_str0("p", "psw", "<password>", "MQTT password");
    mqtt_args.broker_url = arg_str0("a", "addr", "<broker_url>",
                                    "MQTT broker URL, e.g. 192.168.1.10, mqtt://host:1883 or mqtts://host");
    mqtt_args.client_id  = arg_str0("c", "id", "<client_id>", "MQTT client ID");
    mqtt_args.insecure   = arg_lit0(NULL, "insecure", "mqtts: skip server certificate verification");
    mqtt_args.secure     = arg_lit0(NULL, "secure", "mqtts: verify server certificate (default)");
    mqtt_args.enable     = arg_lit0(NULL, "enable", "start the MQTT client (turns the WebSocket client off)");
    mqtt_args.disable    = arg_lit0(NULL, "disable", "stop the MQTT client");
    mqtt_args.end        = arg_end(8);

    const esp_console_cmd_t cmd = {
        .command  = "setmqtt",
        .help     = "Set MQTT config. Example: setmqtt -a 192.168.1.10 -c indicator-01 -u user -p pass",
        .hint     = NULL,
        .func     = &mqtt_config_set,
        .argtable = &mqtt_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

struct {
    struct arg_lit *clear;
    struct arg_end *end;
} mqttca_args;

/* Read a PEM certificate from the console until its END marker line.
 * Runs inside the REPL task while it executes this command, so reading stdin
 * here is race-free. select() bounds each line wait so a forgotten paste
 * cannot wedge the console forever. */
static int read_pem_from_console(char *buf, size_t buf_size) {
    static const char END_MARKER[] = "-----END CERTIFICATE-----";
    size_t used = 0;
    int fd = fileno(stdin);

    printf("Paste the CA certificate PEM now (must contain %s).\n", END_MARKER);
    printf("Max %u bytes; aborts after 30 s with no input.\n", (unsigned)(buf_size - 1));

    /* Read the raw fd rather than fgets() so select() and the byte stream stay
     * in sync: stdio buffering could pull the whole PEM (marker included) into
     * fgets()'s buffer, leaving select() to time out on an empty fd even though
     * the cert is complete; a final line without a newline could likewise block
     * fgets() past the timeout. Scanning the entire accumulated buffer for the
     * END marker also detects it with no trailing newline and even when a read()
     * splits it across chunks. */
    while (used < buf_size - 1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
        int sel = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            printf("setmqttca: console read error\n");
            return -1;
        }
        if (sel == 0) {
            printf("setmqttca: timed out waiting for certificate data\n");
            return -1;
        }

        ssize_t n = read(fd, buf + used, (buf_size - 1) - used);
        if (n < 0) {
            printf("setmqttca: console read error\n");
            return -1;
        }
        if (n == 0) {
            continue; /* readable but nothing consumed; re-arm the timeout */
        }
        used += (size_t)n;
        buf[used] = '\0';
        if (strstr(buf, END_MARKER) != NULL) {
            return (int)used;
        }
    }
    printf("setmqttca: certificate exceeds %u bytes\n", (unsigned)(buf_size - 1));
    return -1;
}

static int mqtt_ca_set(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&mqttca_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, mqttca_args.end, argv[0]);
        return 1;
    }

    if (mqttca_args.clear->count > 0) {
        if (ha_tls_ca_clear() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clear stored CA");
            return 1;
        }
        ESP_LOGI(TAG, "Stored CA cleared — mqtts:// now verifies against the public CA bundle.");
        esp_event_post_to(ha_cfg_event_handle, HA_CFG_EVENT_BASE, HA_CFG_SET, NULL, 0, portMAX_DELAY);
        /* Shared trust: a running wss:// client must rebuild to drop the CA. */
        esp_event_post_to(ha_cfg_event_handle, HA_WS_EVENT_BASE, HA_WS_CFG_CHANGED, NULL, 0, portMAX_DELAY);
        return 0;
    }

    char *pem = malloc(HA_TLS_CA_MAX_LEN + 1);
    if (pem == NULL) {
        ESP_LOGE(TAG, "Out of memory");
        return 1;
    }
    int len = read_pem_from_console(pem, HA_TLS_CA_MAX_LEN + 1);
    if (len <= 0) {
        free(pem);
        return 1;
    }
    if (ha_tls_ca_store(pem, (size_t)len) != ESP_OK) {
        ESP_LOGE(TAG, "Certificate rejected (must be a PEM CERTIFICATE block, %u bytes max)",
                 (unsigned)HA_TLS_CA_MAX_LEN);
        free(pem);
        return 1;
    }
    free(pem);
    ESP_LOGI(TAG, "CA stored (%d bytes). mqtts:// and wss:// connections now verify against it.", len);
    ESP_LOGI(TAG, "Reconnecting MQTT client.");
    esp_event_post_to(ha_cfg_event_handle, HA_CFG_EVENT_BASE, HA_CFG_SET, NULL, 0, portMAX_DELAY);
    /* Shared trust: a running wss:// client must rebuild to pick up the CA. */
    esp_event_post_to(ha_cfg_event_handle, HA_WS_EVENT_BASE, HA_WS_CFG_CHANGED, NULL, 0, portMAX_DELAY);
    return 0;
}

static void register_mqtt_ca(void) {
    mqttca_args.clear = arg_lit0("c", "clear", "clear the stored CA certificate");
    mqttca_args.end   = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command  = "setmqttca",
        .help     = "Store a CA certificate for mqtts:// (paste PEM), or clear it with -c",
        .hint     = NULL,
        .func     = &mqtt_ca_set,
        .argtable = &mqttca_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

struct {
    struct arg_str *addr;
    struct arg_str *token;
    struct arg_lit *enable;
    struct arg_lit *disable;
    struct arg_end *end;
} ha_ws_args;

static int ha_ws_config_set_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&ha_ws_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ha_ws_args.end, argv[0]);
        print_ha_ws_usage();
        return 1;
    }

    if (!ha_ws_args.addr->count && !ha_ws_args.token->count && !ha_ws_args.enable->count &&
        !ha_ws_args.disable->count) {
        print_ha_ws_usage();
        return 0;
    }

    if (ha_ws_args.enable->count > 0 && ha_ws_args.disable->count > 0) {
        ESP_LOGE(TAG, "--enable and --disable are mutually exclusive");
        return 1;
    }

    ha_ws_cfg_t ws_cfg;
    ha_ws_cfg_get(&ws_cfg); /* NOT_FOUND is fine: merge into a zeroed config */

    if (ha_ws_args.addr->count > 0) {
        char url[sizeof(ws_cfg.url)];
        if (!ha_ws_url_normalize(ha_ws_args.addr->sval[0], url, sizeof(url))) {
            ESP_LOGE(TAG, "Invalid HA address: %s", ha_ws_args.addr->sval[0]);
            return 1;
        }
        memset(ws_cfg.url, 0, sizeof(ws_cfg.url));
        strncpy(ws_cfg.url, url, sizeof(ws_cfg.url) - 1);
        ESP_LOGI(TAG, "Set HA WebSocket URL: %s", ws_cfg.url);
    }

    if (ha_ws_args.token->count > 0) {
        const char *token = ha_ws_args.token->sval[0];
        size_t token_len = strlen(token);
        if (token_len == 0 || token_len >= sizeof(ws_cfg.token)) {
            ESP_LOGE(TAG, "Token must be 1-%u characters (got %u)",
                     (unsigned)sizeof(ws_cfg.token) - 1, (unsigned)token_len);
            return 1;
        }
        for (size_t i = 0; i < token_len; i++) {
            unsigned char c = (unsigned char)token[i];
            if (c <= ' ' || c == '"' || c == '\\' || c > 0x7e) {
                ESP_LOGE(TAG, "Token contains an unsupported character at position %u", (unsigned)i);
                return 1;
            }
        }
        memset(ws_cfg.token, 0, sizeof(ws_cfg.token));
        strncpy(ws_cfg.token, token, sizeof(ws_cfg.token) - 1);
        ESP_LOGI(TAG, "Set HA access token: ****");
    }

    if (ha_ws_args.enable->count > 0) {
        /* Entities come from the compile-time dashboard table; only the
         * connection has to be provisioned at runtime. */
        if (ws_cfg.url[0] == '\0' || ws_cfg.token[0] == '\0') {
            ESP_LOGE(TAG, "Cannot enable: missing%s%s (see 'mqtthelp')",
                     ws_cfg.url[0] == '\0' ? " address (-a)" : "",
                     ws_cfg.token[0] == '\0' ? " token (-t)" : "");
            return 1;
        }
        ws_cfg.enabled = 1;
        ESP_LOGI(TAG, "HA WebSocket client enabled");
        /* One transport at a time: WS takes over, the MQTT client stops (the
         * display/set fallback pauses until 'setmqtt --enable'). */
        if (ha_mqtt_enabled_set(false) != ESP_OK) return 1;
        ESP_LOGI(TAG, "MQTT client disabled (one transport at a time)");
    }
    if (ha_ws_args.disable->count > 0) {
        ws_cfg.enabled = 0;
        ESP_LOGI(TAG, "HA WebSocket client disabled");
        if (ha_mqtt_enabled_set(true) != ESP_OK) return 1;
        ESP_LOGI(TAG, "MQTT client re-enabled");
    }

    if (ha_ws_cfg_set(&ws_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save HA WebSocket configuration");
        return 1;
    }
    esp_event_post_to(ha_cfg_event_handle, HA_WS_EVENT_BASE, HA_WS_CFG_CHANGED, NULL, 0, portMAX_DELAY);
    if (ha_ws_args.enable->count > 0 || ha_ws_args.disable->count > 0) {
        /* Apply the MQTT side of the toggle (start or stop that client). */
        esp_event_post_to(ha_cfg_event_handle, HA_CFG_EVENT_BASE, HA_CFG_SET, NULL, 0, portMAX_DELAY);
    }
    ESP_LOGI(TAG, "HA WebSocket configuration saved. Applying.");
    return 0;
}

static void register_ha_ws_config(void) {
    ha_ws_args.addr     = arg_str0("a", "addr", "<url>",
                                   "HA address, e.g. 192.168.1.10, ha.local:8123 or wss://ha.example.com");
    ha_ws_args.token    = arg_str0("t", "token", "<token>", "HA long-lived access token");
    ha_ws_args.enable   = arg_lit0(NULL, "enable", "start the WebSocket client (needs addr+token)");
    ha_ws_args.disable  = arg_lit0(NULL, "disable", "stop the WebSocket client");
    ha_ws_args.end      = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command  = "setha",
        .help     = "Set HA WebSocket config. Example: setha -a 192.168.1.10 -t <token> --enable",
        .hint     = NULL,
        .func     = &ha_ws_config_set_cmd,
        .argtable = &ha_ws_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

struct {
    struct arg_str *tone;
    struct arg_int *duration;
    struct arg_lit *off;
    struct arg_end *end;
} beep_args;

static int beep_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&beep_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, beep_args.end, argv[0]);
        return 1;
    }
    if (beep_args.off->count > 0) {
        ha_siren_stop();
        printf("siren stopped\n");
        return 0;
    }
    const char *tone = beep_args.tone->count > 0 ? beep_args.tone->sval[0] : "beep";
    int duration     = beep_args.duration->count > 0 ? beep_args.duration->ival[0] : 0;
    ha_siren_trigger(tone, duration);
    return 0;
}

static void register_beep(void) {
    beep_args.tone     = arg_str0("t", "tone", "<tone>", "beep (default), doorbell or alarm");
    beep_args.duration = arg_int0("d", "duration", "<seconds>", "alarm run time (default 10, max 120)");
    beep_args.off      = arg_lit0(NULL, "off", "stop the siren");
    beep_args.end      = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command  = "beep",
        .help     = "Sound the buzzer. Example: beep -t doorbell | beep -t alarm -d 15 | beep --off",
        .hint     = NULL,
        .func     = &beep_cmd,
        .argtable = &beep_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

int indicator_cmd_init(void) {
    esp_console_repl_t       *repl        = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt                    = PROMPT_STR ">";
    repl_config.max_cmdline_length        = 1024;

    ha_cfg_get(&ha_cfg);
    register_read_config();
    register_mqtt_help();
    register_mqtt_config();
    register_mqtt_ca();
    register_ha_ws_config();
    register_beep();

    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return ESP_OK;
}
