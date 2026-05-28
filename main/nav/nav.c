#include "nav.h"

#include "lv_port.h"

#define NAV_TILE_WIDTH  480
#define NAV_TILE_HEIGHT 800

static lv_obj_t *s_tileview;
static lv_obj_t *s_tiles[NAV_TILE_COUNT];

int nav_init(void) {
	lv_port_sem_take();

	s_tileview = lv_tileview_create(lv_screen_active());
	lv_obj_set_size(s_tileview, NAV_TILE_WIDTH, NAV_TILE_HEIGHT);
	lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_bg_color(s_tileview, lv_color_black(), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(s_tileview, LV_OPA_COVER, LV_PART_MAIN);

	for (int i = 0; i < NAV_TILE_COUNT; i++) {
		s_tiles[i] = lv_tileview_add_tile(s_tileview, i, 0,
										  LV_DIR_LEFT | LV_DIR_RIGHT);
		lv_obj_set_size(s_tiles[i], NAV_TILE_WIDTH, NAV_TILE_HEIGHT);
		lv_obj_set_style_bg_color(s_tiles[i], lv_color_black(), LV_PART_MAIN);
		lv_obj_set_style_bg_opa(s_tiles[i], LV_OPA_COVER, LV_PART_MAIN);
	}

	lv_port_sem_give();

	return 0;
}

lv_obj_t *nav_get_tile(int tile_idx) {
	if (tile_idx < 0 || tile_idx >= NAV_TILE_COUNT) {
		return NULL;
	}

	return s_tiles[tile_idx];
}

void nav_go_tile(int tile_idx) {
	if (tile_idx < 0 || tile_idx >= NAV_TILE_COUNT) {
		return;
	}

	lv_tileview_set_tile_by_index(s_tileview, tile_idx, 0, LV_ANIM_ON);
}
