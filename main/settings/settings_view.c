#include "settings_view.h"

#include <stdio.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lv_port.h"
#include "nav.h"
#include "sdkconfig.h"
#include "ui_components.h"
#include "ui_theme.h"
#include "view_data.h"

LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_28);

static const char *TAG = "settings-view";

static lv_obj_t *s_settings_modal = NULL;
static lv_obj_t *s_settings_ip_label = NULL;

static void settings_refresh_ip_label(void)
{
	if(!s_settings_ip_label)
	{
		return;
	}

	esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	esp_netif_ip_info_t ip_info = {0};

	if(netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
	   ip_info.ip.addr != 0)
	{
		char buf[40];
		snprintf(buf, sizeof(buf), "IP: " IPSTR, IP2STR(&ip_info.ip));
		lv_label_set_text(s_settings_ip_label, buf);
	}
	else
	{
		lv_label_set_text(s_settings_ip_label, "IP: not connected");
	}
}

static void settings_hide_modal(void)
{
	if(s_settings_modal)
	{
		ui_modal_anim_out(s_settings_modal);
	}
}

static void settings_post_screen(enum start_screen screen)
{
	/* LVGL task context (card tap): bound the post and warn on drop. */
	esp_err_t err = esp_event_post_to(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START,
		&screen, sizeof(screen), pdMS_TO_TICKS(100));
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "drop VIEW_EVENT_SCREEN_START: %s", esp_err_to_name(err));
	}
}

static void settings_open_wifi(lv_event_t *e)
{
	if(lv_event_get_code(e) == LV_EVENT_CLICKED)
	{
		settings_post_screen(SCREEN_WIFI_CONFIG);
	}
}

static void settings_open_display(lv_event_t *e)
{
	if(lv_event_get_code(e) == LV_EVENT_CLICKED)
	{
		settings_post_screen(SCREEN_DISPLAY_MODAL);
	}
}

static void settings_open_broker(lv_event_t *e)
{
	if(lv_event_get_code(e) == LV_EVENT_CLICKED)
	{
		settings_post_screen(SCREEN_BROKER_MODAL);
	}
}

static void settings_open_ha_ws(lv_event_t *e)
{
	if(lv_event_get_code(e) == LV_EVENT_CLICKED)
	{
		settings_post_screen(SCREEN_HA_WS_STATUS);
	}
}

static void settings_on_back(lv_event_t *e)
{
	if(lv_event_get_code(e) == LV_EVENT_CLICKED)
	{
		settings_hide_modal();
	}
}

void settings_view_show(void)
{
	if(!s_settings_modal)
	{
		return;
	}

	settings_refresh_ip_label();
	lv_obj_remove_flag(s_settings_modal, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(s_settings_modal);
	ui_modal_anim_in(s_settings_modal);
}

static void settings_on_gear(lv_event_t *e)
{
	if(lv_event_get_code(e) == LV_EVENT_CLICKED)
	{
		settings_view_show();
	}
}

static void settings_style_card(lv_obj_t *card)
{
	/* Muted surface card with animated pressed feedback — consistent with the
	 * control pages. The accent now lives in the card's icon colour. */
	ui_apply_card(card);
	ui_make_pressable(card);
}

static void settings_create_card(lv_obj_t *parent, int32_t x, int32_t y,
								 lv_color_t accent, const char *label,
								 const char *icon, lv_event_cb_t cb)
{
	lv_obj_t *card = lv_button_create(parent);
	lv_obj_set_size(card, 150, 150);
	lv_obj_set_pos(card, x, y);
	settings_style_card(card);
	lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *icon_label = ui_label(card, icon, &lv_font_montserrat_28, accent);
	lv_obj_set_align(icon_label, LV_ALIGN_CENTER);
	lv_obj_set_y(icon_label, -26);

	lv_obj_t *title = ui_label(card, label, UI_FONT_BODY, UI_COLOR_TEXT);
	lv_obj_set_align(title, LV_ALIGN_CENTER);
	lv_obj_set_y(title, 35);
}

static void settings_create_gear_button(lv_obj_t *tile)
{
	lv_obj_t *button = lv_button_create(tile);
	lv_obj_set_size(button, 52, 52);
	lv_obj_set_align(button, LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(button, 14, 14);
	lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP,
							LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_radius(button, UI_RADIUS_BUTTON, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	/* Animated pressed fill + subtle shrink from the design system. */
	ui_make_pressable(button);
	lv_obj_add_event_cb(button, settings_on_gear, LV_EVENT_CLICKED, NULL);

	lv_obj_t *label = ui_label(button, LV_SYMBOL_SETTINGS, &lv_font_montserrat_28, UI_COLOR_TEXT);
	lv_obj_center(label);
}

static void settings_create_modal(void)
{
	if(s_settings_modal)
	{
		return;
	}

	s_settings_modal = ui_modal_create();
	ui_modal_header(s_settings_modal, "Settings", settings_on_back, NULL);

	s_settings_ip_label = ui_label(s_settings_modal, "IP: not connected",
								   UI_FONT_BODY, UI_COLOR_TEXT_MUTED);
	lv_obj_set_align(s_settings_ip_label, LV_ALIGN_TOP_MID);
	lv_obj_set_y(s_settings_ip_label, 105);

	/* 2x2 card grid: WiFi/Display on the first row, MQTT/Home Assistant on
	 * the second. Same component everywhere so the entries match. */
	settings_create_card(s_settings_modal, 80, 150,
						 UI_COLOR_BLUE, "WiFi", LV_SYMBOL_WIFI,
						 settings_open_wifi);
	settings_create_card(s_settings_modal, 250, 150,
						 UI_COLOR_AMBER, "Display", LV_SYMBOL_IMAGE,
						 settings_open_display);
	settings_create_card(s_settings_modal, 80, 320,
						 UI_COLOR_GREEN, "MQTT", LV_SYMBOL_UPLOAD,
						 settings_open_broker);
	settings_create_card(s_settings_modal, 250, 320,
						 UI_COLOR_PRIMARY, "Home Asst", LV_SYMBOL_HOME,
						 settings_open_ha_ws);
}

int settings_view_init(void)
{
	lv_port_sem_take();

	for(int i = 0; i < NAV_TILE_COUNT; i++)
	{
		lv_obj_t *tile = nav_get_tile(i);
		if(!tile)
		{
			lv_port_sem_give();
			ESP_LOGE(TAG, "Settings gear tile %d is not init", i);
			return -1;
		}
		settings_create_gear_button(tile);
	}

	settings_create_modal();
	lv_port_sem_give();
	return 0;
}
