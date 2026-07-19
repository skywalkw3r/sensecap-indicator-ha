/**
 * @file view_display.c
 * @brief Display settings modal: brightness + sleep mode.
 *
 * Brightness drags post VIEW_EVENT_BRIGHTNESS_UPDATE live (the backlight is
 * the preview); the full config persists via VIEW_EVENT_DISPLAY_CFG_APPLY on
 * every discrete change and on close. Sleep timeout is a preset row — no
 * textarea/keyboard, so no empty-input or focus edge cases.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display_model.h"
#include "display_view.h"
#include "lv_port.h"
#include "ui_components.h"
#include "ui_icons.h"
#include "ui_theme.h"
#include "view_data.h"
#include <esp_log.h>
#include "sdkconfig.h"

static const char* TAG = "display-view";

/* Sleep-timeout presets (minutes). One row of choices beats a numeric
 * keyboard on a wall panel; a stored value between presets snaps to the
 * nearest one visually and is only rewritten when the user picks a preset. */
static const char* k_sleep_map[] = {"1", "5", "15", "30", "60", ""};
#define SLEEP_PRESET_COUNT 5

static lv_obj_t* s_display_modal = NULL;
static lv_obj_t* s_brightness_cfg = NULL;
static lv_obj_t* s_brightness_value = NULL;
static lv_obj_t* s_sleep_mode_cfg = NULL;
static lv_obj_t* s_sleep_mode_time_panel = NULL;
static lv_obj_t* s_sleep_matrix = NULL;

static void _apply_display_cfg_to_widgets(const struct view_data_display* cfg);
static void _ensure_display_modal(void);

static void _sync_sleep_time_panel(void) {
	if(!s_sleep_mode_cfg || !s_sleep_mode_time_panel)
	{
		return;
	}

	if(lv_obj_has_state(s_sleep_mode_cfg, LV_STATE_CHECKED))
	{
		lv_obj_remove_flag(s_sleep_mode_time_panel, LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
		lv_obj_add_flag(s_sleep_mode_time_panel, LV_OBJ_FLAG_HIDDEN);
	}
}

static int _sleep_matrix_minutes(void) {
	if(!s_sleep_matrix)
	{
		return 0;
	}
	uint32_t sel = lv_buttonmatrix_get_selected_button(s_sleep_matrix);
	if(sel == LV_BUTTONMATRIX_BUTTON_NONE || sel >= SLEEP_PRESET_COUNT)
	{
		return atoi(k_sleep_map[0]);
	}
	return atoi(k_sleep_map[sel]);
}

static void _display_cfg_from_widgets(struct view_data_display* cfg) {
	_display_cfg_get(cfg);

	if(s_brightness_cfg)
	{
		cfg->brightness = lv_slider_get_value(s_brightness_cfg);
	}

	cfg->sleep_mode_en = s_sleep_mode_cfg &&
		lv_obj_has_state(s_sleep_mode_cfg, LV_STATE_CHECKED);
	cfg->sleep_mode_time_min = cfg->sleep_mode_en ? _sleep_matrix_minutes() : 0;
}

static void _hide_display_modal(void) {
	if(s_display_modal)
	{
		ui_modal_anim_out(s_display_modal);
	}
}

static void _on_display_close(lv_event_t* e) {
	if(lv_event_get_code(e) != LV_EVENT_CLICKED)
	{
		return;
	}

	display_cfg_apply_event_cb(e);
	_hide_display_modal();
}

static void _on_sleep_mode_changed(lv_event_t* e) {
	if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
	{
		return;
	}

	_sync_sleep_time_panel();
	display_cfg_apply_event_cb(e);
}

static void _on_sleep_preset_changed(lv_event_t* e) {
	if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
	{
		return;
	}
	display_cfg_apply_event_cb(e);
}

static void _update_brightness_value(int32_t value) {
	if(s_brightness_value)
	{
		lv_label_set_text_fmt(s_brightness_value, "%d%%", (int)value);
	}
}

static void _ensure_display_modal(void) {
	if(s_display_modal)
	{
		return;
	}

	s_display_modal = ui_modal_create();
	ui_modal_header(s_display_modal, "Display", _on_display_close, NULL);

	/* ── Brightness card: icon + live % + slider ─────────────────────────── */
	lv_obj_t* brightness_panel = lv_obj_create(s_display_modal);
	lv_obj_set_size(brightness_panel, 440, 120);
	lv_obj_set_pos(brightness_panel, 20, 110);
	ui_apply_card(brightness_panel);
	lv_obj_set_style_pad_all(brightness_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

	lv_obj_t* brightness_icon = ui_label(brightness_panel, UI_ICON_BRIGHTNESS,
										 &ui_font_mdi_32, UI_COLOR_AMBER);
	lv_obj_set_pos(brightness_icon, 16, 12);

	lv_obj_t* brightness_title = ui_label(brightness_panel, "Brightness",
										  UI_FONT_BODY, UI_COLOR_TEXT);
	lv_obj_set_pos(brightness_title, 60, 18);

	s_brightness_value = ui_label(brightness_panel, "--",
								  &lv_font_montserrat_24, UI_COLOR_AMBER);
	lv_obj_set_align(s_brightness_value, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(s_brightness_value, -16, 14);

	lv_obj_t* low_label = ui_label(brightness_panel, LV_SYMBOL_MINUS,
								   UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
	lv_obj_set_pos(low_label, 22, 74);
	lv_obj_t* high_label = ui_label(brightness_panel, LV_SYMBOL_PLUS,
									UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
	lv_obj_set_align(high_label, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(high_label, -22, 74);

	/* Track clearance for the knob overhang, same rule as the light cards. */
	s_brightness_cfg = lv_slider_create(brightness_panel);
	lv_slider_set_range(s_brightness_cfg, 1, 100);
	lv_obj_set_size(s_brightness_cfg, 330, 20);
	lv_obj_set_align(s_brightness_cfg, LV_ALIGN_TOP_MID);
	lv_obj_set_pos(s_brightness_cfg, 0, 72);
	lv_obj_set_style_radius(s_brightness_cfg, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_brightness_cfg, UI_COLOR_SURFACE_PRESSED,
							  LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(s_brightness_cfg, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_brightness_cfg, UI_COLOR_AMBER,
							  LV_PART_INDICATOR | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_brightness_cfg, UI_COLOR_TEXT,
							  LV_PART_KNOB | LV_STATE_DEFAULT);
	lv_obj_add_event_cb(s_brightness_cfg, brighness_cfg_event_cb,
						LV_EVENT_VALUE_CHANGED, NULL);

	/* ── Sleep mode card: toggle ─────────────────────────────────────────── */
	lv_obj_t* sleep_panel = lv_obj_create(s_display_modal);
	lv_obj_set_size(sleep_panel, 440, 72);
	lv_obj_set_pos(sleep_panel, 20, 246);
	ui_apply_card(sleep_panel);
	lv_obj_set_style_pad_all(sleep_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

	lv_obj_t* sleep_icon = ui_label(sleep_panel, UI_ICON_LIGHTBULB_OFF,
									&ui_font_mdi_32, UI_COLOR_PRIMARY);
	lv_obj_set_align(sleep_icon, LV_ALIGN_LEFT_MID);
	lv_obj_set_x(sleep_icon, 16);

	lv_obj_t* sleep_title = ui_label(sleep_panel, "Sleep Mode",
									 UI_FONT_BODY, UI_COLOR_TEXT);
	lv_obj_set_align(sleep_title, LV_ALIGN_LEFT_MID);
	lv_obj_set_x(sleep_title, 60);

	s_sleep_mode_cfg = lv_switch_create(sleep_panel);
	lv_obj_set_size(s_sleep_mode_cfg, 64, 32);
	lv_obj_set_align(s_sleep_mode_cfg, LV_ALIGN_RIGHT_MID);
	lv_obj_set_x(s_sleep_mode_cfg, -16);
	lv_obj_set_style_radius(s_sleep_mode_cfg, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_radius(s_sleep_mode_cfg, 40, LV_PART_INDICATOR | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_sleep_mode_cfg, UI_COLOR_SURFACE_PRESSED,
							  LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_sleep_mode_cfg, UI_COLOR_GREEN,
							  LV_PART_INDICATOR | LV_STATE_CHECKED);
	lv_obj_add_event_cb(s_sleep_mode_cfg, _on_sleep_mode_changed,
						LV_EVENT_VALUE_CHANGED, NULL);

	/* ── Sleep timeout presets (visible while sleep mode is on) ──────────── */
	s_sleep_mode_time_panel = lv_obj_create(s_display_modal);
	lv_obj_set_size(s_sleep_mode_time_panel, 440, 104);
	lv_obj_set_pos(s_sleep_mode_time_panel, 20, 328);
	ui_apply_card(s_sleep_mode_time_panel);
	lv_obj_set_style_pad_all(s_sleep_mode_time_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

	lv_obj_t* after_label = ui_label(s_sleep_mode_time_panel, "Sleep after (minutes)",
									 UI_FONT_LABEL, UI_COLOR_TEXT_MUTED);
	lv_obj_set_pos(after_label, 16, 10);

	s_sleep_matrix = lv_buttonmatrix_create(s_sleep_mode_time_panel);
	lv_buttonmatrix_set_map(s_sleep_matrix, k_sleep_map);
	lv_buttonmatrix_set_one_checked(s_sleep_matrix, true);
	for(int i = 0; i < SLEEP_PRESET_COUNT; i++)
	{
		lv_buttonmatrix_set_button_ctrl(s_sleep_matrix, i, LV_BUTTONMATRIX_CTRL_CHECKABLE);
	}
	lv_obj_set_size(s_sleep_matrix, 408, 52);
	lv_obj_set_align(s_sleep_matrix, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(s_sleep_matrix, -10);
	lv_obj_set_style_bg_opa(s_sleep_matrix, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(s_sleep_matrix, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(s_sleep_matrix, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_pad_gap(s_sleep_matrix, UI_SPACE_SM, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_radius(s_sleep_matrix, UI_RADIUS_BUTTON, LV_PART_ITEMS | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_sleep_matrix, UI_COLOR_SURFACE_PRESSED,
							  LV_PART_ITEMS | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_sleep_matrix, UI_COLOR_PRIMARY,
							  LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_shadow_width(s_sleep_matrix, 0, LV_PART_ITEMS | LV_STATE_DEFAULT);
	lv_obj_add_event_cb(s_sleep_matrix, _on_sleep_preset_changed,
						LV_EVENT_VALUE_CHANGED, NULL);

	struct view_data_display cfg;
	_display_cfg_get(&cfg);
	_apply_display_cfg_to_widgets(&cfg);
}

static void _apply_display_cfg_to_widgets(const struct view_data_display* cfg) {
	if(!cfg || !s_display_modal)
	{
		return;
	}

	if(s_brightness_cfg)
	{
		lv_slider_set_value(s_brightness_cfg, cfg->brightness, LV_ANIM_OFF);
		_update_brightness_value(cfg->brightness);
	}

	if(s_sleep_mode_cfg)
	{
		if(cfg->sleep_mode_en)
		{
			lv_obj_add_state(s_sleep_mode_cfg, LV_STATE_CHECKED);
		}
		else
		{
			lv_obj_remove_state(s_sleep_mode_cfg, LV_STATE_CHECKED);
		}
	}

	if(s_sleep_matrix)
	{
		/* Snap to the nearest preset; the stored value is only rewritten when
		 * the user actually taps a preset or toggles sleep mode. */
		int best = 0;
		int best_delta = 1 << 30;
		for(int i = 0; i < SLEEP_PRESET_COUNT; i++)
		{
			int delta = abs(atoi(k_sleep_map[i]) - cfg->sleep_mode_time_min);
			if(delta < best_delta)
			{
				best_delta = delta;
				best = i;
			}
		}
		lv_buttonmatrix_set_button_ctrl(s_sleep_matrix, best, LV_BUTTONMATRIX_CTRL_CHECKED);
	}

	_sync_sleep_time_panel();
}

static void _show_display_modal(void) {
	_ensure_display_modal();
	if(!s_display_modal)
	{
		return;
	}

	struct view_data_display cfg;
	_display_cfg_get(&cfg);
	_apply_display_cfg_to_widgets(&cfg);
	lv_obj_remove_flag(s_display_modal, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(s_display_modal);
	ui_modal_anim_in(s_display_modal);
}

// static void _brighness_cfg_event_cb(lv_event_t * e)
void brighness_cfg_event_cb(lv_event_t* e) // Value changed
{
	lv_obj_t* slider = lv_event_get_target_obj(e);
	int32_t value = lv_slider_get_value(slider);
	_update_brightness_value(value);
	/* LVGL task context: bound the post so a full view queue cannot freeze it. */
	esp_err_t err = esp_event_post_to(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS_UPDATE,
		&value, sizeof(value), pdMS_TO_TICKS(100));
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "drop VIEW_EVENT_BRIGHTNESS_UPDATE: %s", esp_err_to_name(err));
	}
}

// static void _display_cfg_apply_event_cb(lv_event_t * e)
void display_cfg_apply_event_cb(lv_event_t* e) // any discrete config change
{
	struct view_data_display cfg;
	memset(&cfg, 0, sizeof(cfg));
	if(!s_brightness_cfg || !s_sleep_mode_cfg || !s_sleep_matrix)
	{
		return;
	}
	_display_cfg_from_widgets(&cfg);
	/* LVGL task context: bound the post so a full view queue cannot freeze it. */
	esp_err_t err = esp_event_post_to(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_DISPLAY_CFG_APPLY,
		&cfg, sizeof(cfg), pdMS_TO_TICKS(100));
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "drop VIEW_EVENT_DISPLAY_CFG_APPLY: %s", esp_err_to_name(err));
	}
}

static void _view_event_handler(void* handler_args, esp_event_base_t base,
								int32_t id, void* event_data) {
	switch(id)
	{
		case VIEW_EVENT_SCREEN_START:
		{
			if(!event_data)
			{
				break;
			}
			uint8_t screen = *(uint8_t*)event_data;
			if(screen == SCREEN_DISPLAY_MODAL)
			{
				lv_port_sem_take();
				_show_display_modal();
				lv_port_sem_give();
			}
			break;
		}
		case VIEW_EVENT_DISPLAY_CFG:
		{
			if(!event_data)
			{
				break;
			}
			struct view_data_display* cfg = (struct view_data_display*)event_data;
			lv_port_sem_take();
			_ensure_display_modal();
			_apply_display_cfg_to_widgets(cfg);
			lv_port_sem_give();
			break;
		}
		default:
			break;
	}
}

int indicator_display_view_init(void) {
	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START,
		_view_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_DISPLAY_CFG,
		_view_event_handler, NULL, NULL));

	return 0;
}
