#include "wifi_model.h"
#include "esp_log.h"

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_event.h"
#include "ping/ping_sock.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT	   BIT1

struct indicator_wifi
{
	struct view_data_wifi_st st;
	bool is_cfg;
	int wifi_reconnect_cnt;
};

static struct indicator_wifi _g_wifi_model;
static SemaphoreHandle_t _g_wifi_mutex;
static SemaphoreHandle_t _g_data_mutex;
static SemaphoreHandle_t _g_net_check_sem;

static int s_retry_num = 0;
static int wifi_retry_max = 3;
static bool _g_ping_done = true;

/* ── Async AP scan state ──────────────────────────────────────────────────
 * The scan is started non-blocking (esp_wifi_scan_start with block=false) from
 * the view-event loop and finished on the default Wi-Fi event loop via
 * WIFI_EVENT_SCAN_DONE, so the shared UI/data bus never blocks for the 2-4s a
 * scan takes. _g_scan_in_progress gates overlapping requests; it is a plain
 * flag (like _g_ping_done above) because scan requests serialise on the view
 * loop and only the completion paths clear it, so no lock is required. The
 * guard timer recovers the flag if SCAN_DONE never arrives (_wifi_scan_guard_cb).
 *
 * Guard timeout: IDF v5.4 posts exactly one WIFI_EVENT_SCAN_DONE per successful
 * esp_wifi_scan_start(), so the happy path never needs the timer. It only fires
 * if the scan is torn down without a SCAN_DONE — e.g. the connect path calls
 * esp_wifi_stop() while a scan is still in flight. 8s is comfortably above the
 * ~2-4s a 2.4GHz active scan takes. */
#define WIFI_SCAN_GUARD_TIMEOUT_US (8 * 1000 * 1000)
static bool _g_scan_in_progress = false;
static esp_timer_handle_t _g_scan_guard_timer = NULL;

static const char* TAG = "wifi-model";

static int min(int a, int b) {
	return (a < b) ? a : b;
}

static void _wifi_st_set(struct view_data_wifi_st* p_st) {
	xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
	memcpy(&_g_wifi_model.st, p_st, sizeof(struct view_data_wifi_st));
	xSemaphoreGive(_g_data_mutex);
}

static void _wifi_st_get(struct view_data_wifi_st* p_st) {
	xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
	memcpy(p_st, &_g_wifi_model.st, sizeof(struct view_data_wifi_st));
	xSemaphoreGive(_g_data_mutex);
}

/* Build the AP-list payload (current connection + deduped scan records) and
 * publish VIEW_EVENT_WIFI_LIST. The view hides its scan spinner when this event
 * arrives, so EVERY terminal scan outcome — success, empty, or failure — must
 * call this or the UI strands a spinner. Pass ap_count == 0 to publish a list
 * carrying only the current connection (used on the failure / guard paths). */
static void _wifi_list_post(const wifi_ap_record_t* p_ap_info, uint16_t ap_count) {
	struct view_data_wifi_list list;
	struct view_data_wifi_st st;

	memset(&list, 0, sizeof(struct view_data_wifi_list));

	_wifi_st_get(&st);

	list.is_connect = st.is_connected;
	if(st.is_connected)
	{
		strlcpy((char*)list.connect.ssid, (char*)st.ssid, sizeof(list.connect.ssid));
		list.connect.auth_mode = false;
		list.connect.rssi = st.rssi;
	}

	bool is_exist = false;
	int list_cnt = 0;
	for(int i = 0; i < ap_count; i++)
	{
		is_exist = false;
		for(int j = 0; j < list_cnt; j++)
		{
			if(strcmp(list.aps[j].ssid, (char*)p_ap_info[i].ssid) == 0)
			{
				ESP_LOGI(TAG, "list exit ap:%s", (char*)p_ap_info[i].ssid);
				is_exist = true;
				break;
			}
		}
		if(!is_exist)
		{
			strlcpy(list.aps[list_cnt].ssid, (char*)p_ap_info[i].ssid, sizeof(list.aps[list_cnt].ssid));
			list.aps[list_cnt].rssi = p_ap_info[i].rssi;
			list.aps[list_cnt].auth_mode = (p_ap_info[i].authmode != WIFI_AUTH_OPEN);
			list_cnt++;
		}
	}
	list.cnt = list_cnt;
	/* Runs on the default Wi-Fi event loop (SCAN_DONE) or the esp_timer task
	 * (scan guard); never block either of them on a full view queue. */
	esp_err_t post_err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST, &list,
					  sizeof(struct view_data_wifi_list), pdMS_TO_TICKS(100));
	if(post_err != ESP_OK)
	{
		ESP_LOGW(TAG, "drop VIEW_EVENT_WIFI_LIST: %s", esp_err_to_name(post_err));
	}
}

/* Read the just-completed scan's records from the driver and publish them.
 * esp_wifi_scan_get_ap_records() also releases the driver's internal AP list,
 * so it must run once per scan. Errors degrade to an empty list rather than
 * aborting — the old blocking path ESP_ERROR_CHECK'd here, which would panic
 * the device on a transient get-records failure. */
static void _wifi_scan_publish_records(void) {
	uint16_t number = WIFI_SCAN_LIST_SIZE;
	uint16_t ap_count = 0;
	wifi_ap_record_t ap_info[WIFI_SCAN_LIST_SIZE];

	memset(ap_info, 0, sizeof(ap_info));

	if(esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK)
	{
		ap_count = 0;
	}
	if(esp_wifi_scan_get_ap_records(&number, ap_info) != ESP_OK)
	{
		number = 0;
	}
	ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);

	_wifi_list_post(ap_info, min(number, ap_count));
}

/* One-shot recovery for a scan that never reports completion. If SCAN_DONE is
 * absent (e.g. esp_wifi_stop() in the connect path aborted an in-flight scan),
 * clear the in-flight flag and unstick the view so future scans can run and the
 * spinner is hidden. Runs on the esp_timer task. */
static void _wifi_scan_guard_cb(void* arg) {
	if(!_g_scan_in_progress)
	{
		return;
	}
	ESP_LOGW(TAG, "scan guard fired: no SCAN_DONE within timeout, recovering");
	_g_scan_in_progress = false;
	esp_wifi_scan_stop();
	_wifi_list_post(NULL, 0);
}

/* Kick off a non-blocking AP scan and return immediately; results arrive later
 * on the default Wi-Fi event loop as WIFI_EVENT_SCAN_DONE. A second request
 * while one is in flight is ignored (no overlapping scans). A rejected start
 * (e.g. ESP_ERR_WIFI_STATE while a connect is in progress) still publishes a
 * list so the view's spinner is cleared. */
static void _wifi_scan_start(void) {
	if(_g_scan_in_progress)
	{
		ESP_LOGW(TAG, "scan already in progress, ignoring request");
		return;
	}

	/* Mark in-flight and arm the guard BEFORE starting, so a scan that
	 * completes immediately cannot race the flag into a stuck state. */
	_g_scan_in_progress = true;
	esp_timer_start_once(_g_scan_guard_timer, WIFI_SCAN_GUARD_TIMEOUT_US);

	esp_err_t err = esp_wifi_scan_start(NULL, false);
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
		esp_timer_stop(_g_scan_guard_timer);
		_g_scan_in_progress = false;
		_wifi_list_post(NULL, 0);
	}
}

static void _wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	switch(event_id)
	{
		case WIFI_EVENT_STA_START:
		{
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_START");
			struct view_data_wifi_st st;
			st.is_connected = false;
			st.is_network = false;
			st.is_connecting = true;
			memset(st.ssid, 0, sizeof(st.ssid));
			st.rssi = 0;
			_wifi_st_set(&st);

			esp_wifi_connect();
			break;
		}
		case WIFI_EVENT_SCAN_DONE:
		{
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_SCAN_DONE");
			/* Disarm the guard (harmless no-op if it already fired), then
			 * publish results — this runs off the old blocking scan path. */
			esp_timer_stop(_g_scan_guard_timer);
			_g_scan_in_progress = false;
			_wifi_scan_publish_records();
			break;
		}
		case WIFI_EVENT_STA_CONNECTED:
		{
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_CONNECTED");
			wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;
			struct view_data_wifi_st st;

			_wifi_st_get(&st);
			memset(st.ssid, 0, sizeof(st.ssid));
			int ssid_len = min(event->ssid_len, (int)sizeof(st.ssid) - 1);
			memcpy(st.ssid, event->ssid, ssid_len);
			st.ssid[ssid_len] = '\0';
			st.rssi = -50; // todo
			st.is_connected = true;
			st.is_connecting = false;
			_wifi_st_set(&st);

			esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st,
							  sizeof(struct view_data_wifi_st), portMAX_DELAY);

			struct view_data_wifi_connet_ret_msg msg;
			msg.ret = 0;
			strcpy(msg.msg, "Connection successful");
			esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg),
							  portMAX_DELAY);
			break;
		}
		case WIFI_EVENT_STA_DISCONNECTED:
		{
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_DISCONNECTED");

			if((wifi_retry_max == -1) || s_retry_num < wifi_retry_max)
			{
				esp_wifi_connect();
				s_retry_num++;
				ESP_LOGI(TAG, "retry to connect to the AP");
			}
			else
			{
				struct view_data_wifi_st st;

				_wifi_st_get(&st);
				st.is_connected = false;
				st.is_network = false;
				st.is_connecting = false;
				_wifi_st_set(&st);

				esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st,
								  sizeof(struct view_data_wifi_st), portMAX_DELAY);

				struct view_data_wifi_connet_ret_msg msg;
				msg.ret = 1;
				strcpy(msg.msg, "Connection failure");
				esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg),
								  portMAX_DELAY);
			}
			break;
		}
		default:
			break;
	}
}

static void _ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	if(event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;

		/* Got an IP: the LAN is usable. Mark the network up immediately and
		 * publish it so MQTT can start now, without waiting for an
		 * internet-reachability ping (many HA IoT VLANs have no internet
		 * egress). The ping below only refines status, never gates MQTT. */
		struct view_data_wifi_st st;
		_wifi_st_get(&st);
		st.is_connected = true;
		st.is_network = true;
		_wifi_st_set(&st);
		esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st,
						  sizeof(struct view_data_wifi_st), portMAX_DELAY);

		xSemaphoreGive(_g_net_check_sem);
	}
}

static int _wifi_connect(const char* p_ssid, const char* p_password, int retry_num) {
	wifi_retry_max = retry_num;
	s_retry_num = 0;

	wifi_config_t wifi_config = {0};
	strlcpy((char*)wifi_config.sta.ssid, p_ssid, sizeof(wifi_config.sta.ssid));
	ESP_LOGI(TAG, "ssid: %s", p_ssid);
	if(p_password)
	{
		strlcpy((char*)wifi_config.sta.password, p_password, sizeof(wifi_config.sta.password));
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	}
	else
	{
		wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
	}
	wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

	esp_wifi_stop();
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

	_g_wifi_model.is_cfg = true;

	struct view_data_wifi_st st = {0};
	st.is_connected = false;
	st.is_connecting = false;
	st.is_network = false;
	_wifi_st_set(&st);

	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "connect...");
	return 0;
}

static void _wifi_cfg_restore(void) {
	_g_wifi_model.is_cfg = false;

	struct view_data_wifi_st st = {0};
	st.is_connected = false;
	st.is_connecting = false;
	st.is_network = false;
	_wifi_st_set(&st);

	/* Reachable from the view loop (VIEW_EVENT_WIFI_CFG_DELETE handler); bound
	 * the post so a full queue cannot deadlock that task. */
	esp_err_t err = esp_event_post_to(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st,
		sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(100));
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "drop VIEW_EVENT_WIFI_ST: %s", esp_err_to_name(err));
	}

	esp_wifi_restore();
}

static void _wifi_shutdown(void) {
	_g_wifi_model.is_cfg = false;

	struct view_data_wifi_st st = {0};
	st.is_connected = false;
	st.is_connecting = false;
	st.is_network = false;
	_wifi_st_set(&st);

	/* Reachable from the view loop (VIEW_EVENT_SHUTDOWN handler); bound the post
	 * so a full queue cannot deadlock that task. */
	esp_err_t err = esp_event_post_to(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st,
		sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(100));
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "drop VIEW_EVENT_WIFI_ST: %s", esp_err_to_name(err));
	}

	esp_wifi_stop();
}

static void _ping_end(esp_ping_handle_t hdl, void* args) {
	ip_addr_t target_addr;
	uint32_t transmitted = 0;
	uint32_t received = 0;
	uint32_t total_time_ms = 0;
	uint32_t loss = 0;
	esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
	esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));

	if(transmitted > 0)
	{
		loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
	}
	else
	{
		loss = 100;
	}

	if(IP_IS_V4(&target_addr))
	{
		printf("\n--- %s ping statistics ---\n", inet_ntoa(*ip_2_ip4(&target_addr)));
	}
	else
	{
		printf("\n--- %s ping statistics ---\n", inet6_ntoa(*ip_2_ip6(&target_addr)));
	}
	printf("%" PRIu32 " packets transmitted, %" PRIu32 " received, %" PRIu32 "%% packet loss, time %" PRIu32 "ms\n", transmitted, received, loss,
		   total_time_ms);

	esp_ping_delete_session(hdl);

	/* Diagnostic only: internet reachability is reported to the console but is
	 * NOT written back into is_network. Network usability (and therefore MQTT
	 * gating) is owned by IP_EVENT_STA_GOT_IP, so a failed gateway ping never
	 * stops or gates an MQTT client. */
	_g_ping_done = true;
}

static void _ping_start(void) {
	esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();

	/* Ping the LAN default gateway (reachable without internet egress). Only
	 * fall back to a public anycast address if no gateway is known. */
	ip_addr_t target_addr;
	esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	esp_netif_ip_info_t ip_info = {0};
	if(netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.gw.addr != 0)
	{
		/* By-value variant: the pointer form's internal if(ipaddr) trips
		 * GCC 12+ -Waddress (always-true address of a local) under -Wall. */
		ip_addr_set_ip4_u32_val(target_addr, ip_info.gw.addr);
	}
	else
	{
		ipaddr_aton("1.1.1.1", &target_addr);
	}

	config.target_addr = target_addr;

	esp_ping_callbacks_t cbs = {
		.cb_args = NULL, .on_ping_success = NULL, .on_ping_timeout = NULL, .on_ping_end = _ping_end};
	esp_ping_handle_t ping;
	esp_ping_new_session(&config, &cbs, &ping);
	_g_ping_done = false;
	esp_ping_start(ping);
}

static void _indicator_wifi_task(void* p_arg) {
	int cnt = 0;
	struct view_data_wifi_st st;

	while(1)
	{
		xSemaphoreTake(_g_net_check_sem, pdMS_TO_TICKS(5000));
		_wifi_st_get(&st);

		if(st.is_connected)
		{
			if(_g_ping_done)
			{
				if(st.is_network)
				{
					cnt++;
					if(cnt > 60)
					{
						cnt = 0;
						ESP_LOGI(TAG, "Network normal last time, retry check network...");
						_ping_start();
					}
				}
				else
				{
					ESP_LOGI(TAG, "Last network exception, check network...");
					_ping_start();
				}
			}
		}
		else if(_g_wifi_model.is_cfg && !st.is_connecting)
		{
			if(_g_wifi_model.wifi_reconnect_cnt > 5)
			{
				ESP_LOGI(TAG, " Wifi reconnect...");
				_g_wifi_model.wifi_reconnect_cnt = 0;
				wifi_retry_max = 3;
				s_retry_num = 0;

				esp_wifi_stop();

				/* A transient Wi-Fi driver error here must not reboot the
				 * device. Log it and let the reconnect loop retry on the next
				 * cycle instead of aborting via ESP_ERROR_CHECK. */
				esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
				if(err != ESP_OK)
				{
					ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
				}
				else
				{
					err = esp_wifi_start();
					if(err != ESP_OK)
					{
						ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
					}
				}
			}
			_g_wifi_model.wifi_reconnect_cnt++;
		}
	}
}

static void _view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
	switch(id)
	{
		case VIEW_EVENT_WIFI_LIST_REQ:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_LIST_REQ");
			/* Non-blocking: kick the scan and return so the shared event bus
			 * keeps flowing. The result is posted from WIFI_EVENT_SCAN_DONE. */
			_wifi_scan_start();
			break;
		}
		case VIEW_EVENT_WIFI_CONNECT:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CONNECT");
			struct view_data_wifi_config* p_cfg = (struct view_data_wifi_config*)event_data;

			if(p_cfg->have_password)
			{
				_wifi_connect(p_cfg->ssid, (char*)p_cfg->password, 3);
			}
			else
			{
				_wifi_connect(p_cfg->ssid, NULL, 3);
			}
			break;
		}
		case VIEW_EVENT_WIFI_CFG_DELETE:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CFG_DELETE");
			_wifi_cfg_restore();
			break;
		}
		case VIEW_EVENT_SHUTDOWN:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_SHUTDOWN");
			_wifi_shutdown();
			break;
		}
		default:
			break;
	}
}

int indicator_wifi_model_init(void) {
	_g_wifi_mutex = xSemaphoreCreateMutex();
	_g_data_mutex = xSemaphoreCreateMutex();
	_g_net_check_sem = xSemaphoreCreateBinary();

	const esp_timer_create_args_t scan_guard_args = {
		.callback = &_wifi_scan_guard_cb,
		.name = "wifi_scan_guard",
	};
	ESP_ERROR_CHECK(esp_timer_create(&scan_guard_args, &_g_scan_guard_timer));

	memset(&_g_wifi_model, 0, sizeof(_g_wifi_model));

	xTaskCreate(&_indicator_wifi_task, "_indicator_wifi_task", 1024 * 5, NULL, 10, NULL);

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;

	ESP_ERROR_CHECK(
		esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &_wifi_event_handler, 0, &instance_any_id));

	ESP_ERROR_CHECK(
		esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_ip_event_handler, 0, &instance_got_ip));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, _view_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, _view_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CFG_DELETE, _view_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SHUTDOWN,
															 _view_event_handler, NULL, NULL));

	wifi_config_t wifi_cfg;
	esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

	if(strlen((const char*)wifi_cfg.sta.ssid))
	{
		_g_wifi_model.is_cfg = true;
		ESP_LOGI(TAG, "last config ssid: %s", wifi_cfg.sta.ssid);
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());
	}
	else
	{
		ESP_LOGI(TAG, "Not config wifi, Entry wifi config screen");
		uint8_t screen = SCREEN_WIFI_CONFIG;
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());
		esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, &screen, sizeof(screen),
						  portMAX_DELAY);
	}

	return 0;
}
