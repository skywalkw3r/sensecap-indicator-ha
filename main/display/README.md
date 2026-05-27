# Display Domain

The Display domain is a vertical slice for LCD backlight brightness, sleep mode
configuration, and display settings callbacks.

## File Responsibilities

| File | Responsibility |
| --- | --- |
| `display.h` | Umbrella header for the Display domain. Its include guard remains `INDICATOR_DISPLAY_H` for compatibility with existing feature checks. |
| `display_model.h` | Display state type and model API for backlight, sleep timer, and config access. |
| `display_model.c` | LEDC PWM backlight control, sleep mode `esp_timer`, NVS config persistence, and Display view-event handling. |
| `display_view.h` | LVGL settings-screen callback declarations. |
| `display_view.c` | LVGL callbacks for brightness and sleep-mode settings. This is the only Display file that may include `ui.h`. |

## Init Sequence

`indicator_display_init()` is called from model initialization before
`ui_init()`. It restores persisted config, initializes the LCD backlight and
sleep timer, posts the current display config, and registers model event
handlers.

Squareline's `ui.c` registers the Display view callbacks at `ui_init()` time
when it creates the settings widgets.

## Data Flow

Slider or toggle changes in the settings screen call the LVGL callbacks in
`display_view.c`.

`brighness_cfg_event_cb()` posts `VIEW_EVENT_BRIGHTNESS_UPDATE`, which the model
handles by applying the LEDC backlight duty and updating the cached config.

`display_cfg_apply_event_cb()` posts `VIEW_EVENT_DISPLAY_CFG_APPLY`, which the
model handles by applying config, saving it to NVS, and restarting the sleep
timer.

## Squareline Callbacks

`brighness_cfg_event_cb()` and `display_cfg_apply_event_cb()` are declared in
`ui/ui_events.h`, which is Squareline-managed. They are called directly from
generated `ui.c`, so they must remain non-static globally visible symbols.
