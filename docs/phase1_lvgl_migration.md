# Phase 1 LVGL v9 Migration Plan

Goal: migrate the ESP32-S3 screen-side firmware from the checked-in LVGL 8.3.11 component to managed LVGL 9 plus Espressif's ESP LVGL port.

Architecture: keep the existing product behavior and vertical domain slices. Treat `main/ui/` as generated SquareLine LVGL 8 code, put compatibility around it, and migrate handwritten view and port code incrementally.

Tech stack: ESP-IDF 5.4.3, ESP32-S3, FreeRTOS, LVGL 9.x, `lvgl/lvgl`, `espressif/esp_lvgl_port`, current BSP LCD and input drivers.

## Current State Summary

### Dependency and component state

- `dependencies.lock` records ESP-IDF `5.4.3` and target `esp32s3`.
- The project root `CMakeLists.txt` sets `EXTRA_COMPONENT_DIRS components`, so local components under `components/` are preferred until they are removed or made non-components.
- `components/lvgl/` is present as a local component. Its `CMakeLists.txt` is the upstream LVGL wrapper that includes ESP-specific CMake from `env_support/cmake/esp.cmake`.
- `components/lvgl/idf_component.yml` exists but only contains metadata. It does not pin or declare a managed dependency version.
- `components/lvgl/lvgl.h` defines `LVGL_VERSION_MAJOR 8`, `LVGL_VERSION_MINOR 3`, and `LVGL_VERSION_PATCH 11`.
- `main/ui/ui.h` confirms the generated UI came from SquareLine Studio 1.4.1 targeting LVGL 8.3.11.
- `components/esp_lvgl_port/CMakeLists.txt` was not found because `components/esp_lvgl_port/` is not present in this tree.
- No top-level `idf_component.yml` or `main/idf_component.yml` existed before this phase. The only existing manifest was `components/lvgl/idf_component.yml`.
- On the ESP Component Registry, LVGL is published as `lvgl/lvgl`, not `espressif/lvgl`. This plan and the new manifest use the registry-correct name.

### Generated UI API surface

`main/ui/ui.h` includes `lvgl/lvgl.h` and exports global `lv_obj_t *` handles plus generated `lv_event_t *` callbacks. The generated UI and helpers use these LVGL 8 API families:

- Object creation and styling: `lv_obj_create`, `lv_btn_create`, `lv_label_create`, `lv_obj_set_width`, `lv_obj_set_height`, `lv_obj_set_x`, `lv_obj_set_y`, `lv_obj_set_align`, `lv_obj_set_style_*`, `lv_color_hex`, `lv_palette_main`.
- Widgets: labels, buttons, switches, sliders, arcs, bars, text areas, keyboard, roller, dropdown, spinner, and images.
- Events and input: `lv_event_t`, `lv_event_get_code`, `lv_event_get_target`, `lv_event_get_user_data`, `lv_obj_add_event_cb`, `LV_EVENT_*`, `lv_indev_get_act`, `lv_indev_get_gesture_dir`, `lv_indev_wait_release`.
- Screen helpers: `lv_scr_load_anim_t`, `lv_scr_load_anim`, `lv_disp_load_scr`, `lv_disp_get_default`, `lv_disp_set_theme`, `lv_scr_act`.
- Image APIs and descriptors: `lv_img_create`, `lv_img_set_src`, `lv_img_set_zoom`, `lv_img_get_zoom`, `lv_img_set_angle`, `lv_img_get_angle`, `lv_img_dsc_t`, `LV_IMG_CF_TRUE_COLOR_ALPHA`.
- Font descriptors: generated font files use `lv_font_fmt_txt_*`, `lv_font_t`, `lv_font_get_glyph_dsc_fmt_txt`, and `lv_font_get_bitmap_fmt_txt`.

Two generated-helper details are higher risk than simple renames:

- `main/ui/ui_helpers.c` reads `a->user_data` from `lv_anim_t` and frees it with `lv_mem_free`.
- `_ui_slider_set_text_value()` casts an object to `lv_arc_t *` and reads `arc->value` directly. That depends on LVGL internal struct layout and should be replaced with a public getter.

The generated image descriptors use LVGL 8 fields such as `.header.always_zero`, `.header.cf`, and `LV_IMG_CF_TRUE_COLOR_ALPHA`. These may need regeneration or targeted compatibility code because LVGL 9 changed image and color-format handling.

### Current porting layer

`main/lv_port.c` is a custom LVGL 8 port, not `esp_lvgl_port`. It currently owns:

- `lv_init()`, manual tick setup with `esp_timer`, and a FreeRTOS `lvgl_task` loop that calls `lv_task_handler()` every 5 ms.
- A project semaphore API: `lv_port_sem_take()` and `lv_port_sem_give()`.
- Display setup using `lv_disp_draw_buf_t`, `lv_disp_drv_t`, `lv_disp_draw_buf_init()`, `lv_disp_drv_init()`, `lv_disp_drv_register()`, `lv_disp_flush_ready()`, and `lv_disp_flush_is_last()`.
- LCD callbacks through the local BSP: `bsp_lcd_get_frame_buffer()`, `bsp_lcd_flush()`, `bsp_lcd_set_cb()`, `bsp_lcd_flush_is_last_register()`, and `bsp_lcd_direct_mode_register()`.
- Input setup using `lv_indev_drv_t`, `lv_indev_drv_init()`, and `lv_indev_drv_register()` for touch or keypad input backed by `indev_get_major_value()`.
- Direct-mode buffer copying through private LVGL refresh internals: `_lv_refr_get_disp_refreshing()` and fields under `disp_refr->driver`.

The display and input driver types above were removed or refactored in LVGL 9, so this file is the largest handwritten migration target.

### Domain view files

The `*_view.c` files found in `main/` are:

- `main/indicator_view.c`: includes `lv_port.h`, `ui.h`, and `view_data.h`; locks with `lv_port_sem_take()`, calls `ui_init()`, then unlocks.
- `main/display/display_view.c`: includes `ui.h`; event callbacks use `lv_event_get_target()`, `lv_slider_get_value()`, `lv_obj_has_state()`, `lv_textarea_get_text()`, `lv_slider_set_value()`, `lv_obj_clear_state()`, `lv_obj_add_state()`, and `lv_textarea_set_text()`.
- `main/sensor/sensor_view.c`: includes `lv_port.h` and `ui.h`; caches generated label objects and updates them with `lv_label_set_text()` under the LVGL semaphore.
- `main/wifi/wifi_view.c`: includes `lv_port.h` and `ui.h`; uses event filters with `lv_event_get_code()`, `lv_indev_get_type()`, `lv_indev_get_act()`, `lv_event_get_target()`, overlay/toast APIs such as `lv_layer_top()`, `lv_obj_create()`, `lv_obj_del()`, `lv_timer_create()`, `lv_timer_set_repeat_count()`, `lv_label_set_text()`, `lv_img_set_src()`, and `lv_scr_act()`.

Additional non-`*_view.c` LVGL usage exists in `main/ha/ha_switch.c` and `main/ha/ha_config.c`; include those files in the API audit before the first compile attempt even though they were outside the requested view-file list.

## Migration Steps

### 1. Add managed LVGL dependencies

- Create `main/idf_component.yml`.
- Add `lvgl/lvgl` pinned to LVGL 9.4.0 and `espressif/esp_lvgl_port` pinned to 2.7.0.
- Mark both dependencies public because generated headers and view headers include LVGL types.
- After this file exists, do not expect the build to use managed LVGL until the local `components/lvgl/` component is removed or disabled.

### 2. Remove or stub out local LVGL-related components

- Remove `components/lvgl/` from the component search path, or move it outside `components/`. Keeping its CMake files under `components/` can keep the old local component active.
- Re-run dependency resolution after the local component is gone so `dependencies.lock` records managed `lvgl/lvgl` and `espressif/esp_lvgl_port`.
- There is no local `components/esp_lvgl_port/` in this tree. If a later branch adds it, remove it or move it outside `components/` before depending on the managed component.
- Do not import starter-project BSP, display, touch, or RP2040 driver code as part of this step.

### 3. Identify all LVGL v8 to v9 API changes needed

Audit and classify every compile error into one of these buckets:

- Simple compatibility aliases for generated code:
  - `lv_img_*` to `lv_image_*`.
  - `lv_img_dsc_t` to `lv_image_dsc_t`.
  - `LV_IMG_CF_*` to `LV_COLOR_FORMAT_*`.
  - `lv_scr_*` names to `lv_screen_*` names.
  - `lv_disp_*` names to `lv_display_*` names.
  - `lv_indev_get_act()` to `lv_indev_active()`.
  - `lv_obj_clear_flag()` to `lv_obj_remove_flag()`.
  - `lv_obj_clear_state()` to `lv_obj_remove_state()`.
  - `lv_coord_t` to `int32_t`.
  - `zoom` APIs to `scale` APIs and `angle` APIs to `rotation` APIs.
- Porting-layer rewrites:
  - `lv_disp_drv_t` and `lv_disp_draw_buf_t` are removed.
  - `lv_disp_draw_buf_init()` becomes `lv_display_set_buffers()` with buffer size in bytes.
  - `lv_disp_flush_ready()` becomes `lv_display_flush_ready()`.
  - `lv_disp_flush_is_last()` becomes the LVGL 9 display equivalent, or is handled through `esp_lvgl_port`.
  - `lv_indev_drv_t` is removed; use `lv_indev_create()`, `lv_indev_set_type()`, and `lv_indev_set_read_cb()`.
  - `lv_task_handler()` becomes `lv_timer_handler()` if the project keeps a custom task loop.
- Generated-code hazards that should not be hidden behind broad macros:
  - direct `lv_arc_t` field access in `ui_helpers.c`.
  - image descriptor layout and color format fields.
  - any generated font descriptor layout mismatch.
  - private refresh access in `lv_port_direct_mode_copy()`.

### 4. Create `lv_api_map_v8.h` compatibility shim for `main/ui/`

- Add a local shim such as `main/ui/lv_api_map_v8.h`.
- Prefer forcing that shim into only generated UI sources via CMake instead of editing SquareLine-generated files.
- Keep the shim narrow: aliases for old generated names, image names, screen names, and state/flag names only.
- Do not use the shim to preserve internal struct access. Replace direct internal field reads with public getters in a later targeted patch.
- Re-run the API scan after the shim is introduced and keep reducing it; the shim is a migration bridge, not a permanent abstraction.

### 5. Update `main/lv_port.c` for the new `esp_lvgl_port` v2 API

- Decide whether to replace most of the custom LVGL task, lock, display, tick, and input setup with `esp_lvgl_port`, or keep `lv_port_sem_take()` and `lv_port_sem_give()` as wrappers around the port's lock API.
- Preserve the public `lv_port_init()`, `lv_port_sem_take()`, and `lv_port_sem_give()` names at first so domain files do not all change at once.
- Map the existing BSP LCD callbacks to the display registration API. If the current BSP does not expose `esp_lcd_panel_handle_t` and panel IO handles, keep a smaller custom LVGL 9 display registration path and defer full `esp_lvgl_port` adoption.
- Preserve current behavior for PSRAM/full-screen buffers, `CONFIG_LCD_AVOID_TEAR`, full refresh, direct mode, rotation, touch coordinate transforms, and keypad mapping.
- Remove reliance on `_lv_refr_get_disp_refreshing()` and LVGL private display-driver fields. If direct-mode dirty-area copying is still needed, reimplement it using LVGL 9 public display events or the port's supported direct/full refresh paths.

### 6. Update each domain's view files for v9 API changes

- Keep `indicator_view.c` focused on lock and `ui_init()` sequencing.
- In `display_view.c`, migrate state APIs (`clear_state` to `remove_state`) and verify text area and slider APIs still match LVGL 9.
- In `sensor_view.c`, verify label updates are still safe under the migrated lock API.
- In `wifi_view.c`, migrate `lv_indev_get_act()` to `lv_indev_active()`, `lv_scr_act()` to `lv_screen_active()`, `lv_img_set_src()` to `lv_image_set_src()` if not covered by the shim, and `lv_obj_del()` to `lv_obj_delete()` if the project chooses direct v9 names in handwritten code.
- Include `main/ha/ha_switch.c` and `main/ha/ha_config.c` in the same pass because they currently touch LVGL outside the `*_view.c` naming pattern.

## Risk Assessment

- High: generated SquareLine 1.4.1 code targets LVGL 8.3.11 and may not compile cleanly under LVGL 9 without a shim or regeneration.
- High: generated image descriptor layout uses LVGL 8 fields and color-format constants. If compatibility macros are not enough, regenerate assets with LVGL 9-compatible tooling or patch descriptors mechanically.
- High: `main/lv_port.c` uses removed LVGL 8 driver structs and private refresh internals.
- Medium: `esp_lvgl_port` may require `esp_lcd` panel handles that the current BSP abstraction may not expose cleanly.
- Medium: LVGL 9 color handling changed; verify RGB565, alpha images, byte order, and direct/full refresh behavior on hardware.
- Medium: event behavior is mostly similar, but gesture code relies on active input-device APIs that changed names.
- Low: most object, label, slider, text area, state, and style calls are straightforward renames or still available through compatibility mapping.

## Rollback Plan

- Keep the migration in a dedicated branch and do not delete the local LVGL 8 component until the managed-component branch has a working build.
- If dependency resolution or compilation fails early, remove `main/idf_component.yml` and keep `components/lvgl/` active to return to the current build path.
- If LVGL 9 builds but display behavior regresses, keep the new dependency manifest but restore the old `lv_port.c` branch state while isolating BSP/display integration.
- If generated UI compatibility becomes too brittle, regenerate `main/ui/` from SquareLine or LVGL tooling targeting LVGL 9 on a separate branch, then compare generated API and assets before replacing the current generated files.
- Use `python3 scripts/dev_check.py --skip-build` after each source patch and `./dev build` once source changes begin. This first sub-step only adds the dependency manifest and this plan, so it is not expected to produce a successful LVGL 9 build by itself.
