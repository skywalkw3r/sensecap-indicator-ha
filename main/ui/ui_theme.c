#include "ui_theme.h"

lv_theme_t *ui_theme_install(lv_display_t *display)
{
    /* Dark default theme seeded with the product primary (blue) + destructive
     * (red) accents. Kept as the base so every LVGL built-in widget picks up a
     * consistent palette; domain screens override per-part colours with tokens
     * from ui_theme.h where they need an accent. */
    lv_theme_t *theme = lv_theme_default_init(display,
                                              UI_COLOR_PRIMARY,
                                              UI_COLOR_RED,
                                              true, /* dark */
                                              LV_FONT_DEFAULT);
    lv_display_set_theme(display, theme);
    return theme;
}
