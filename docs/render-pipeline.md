# Display Render Pipeline Tuning

Reference for the ESP32-S3 LVGL render pipeline on the SenseCAP Indicator's
**480x480 RGB565** panel, and for the render-profile A/B experiments wired up
(commented) in `sdkconfig.defaults`. Nothing here changes the shipping default;
it documents the alternates and how to measure them on hardware.

Owning file for the port logic: `main/lv_port.c` (`lv_port_disp_init`).
Config lives in `sdkconfig.defaults`. The knobs resolve through
`components/bsp/Kconfig.projbuild` (LCD render mode) and the LVGL component
Kconfig (`managed_components/lvgl__lvgl/Kconfig`).

> These profiles affect the **firmware** build only. The desktop simulator has
> its own `sim/lv_conf.h` / `sim/stubs/sdkconfig.h` and is unaffected.

---

## How the panel is driven

- Panel: 480x480, RGB565, RGB (parallel) interface, pixel clock 18 MHz
  (`CONFIG_LCD_EVB_SCREEN_FREQ=18`).
- `CONFIG_LCD_AVOID_TEAR=y` (shipping): `esp_lvgl_port` does **not** allocate its
  own LVGL draw buffers. It takes the RGB peripheral's **two internal
  framebuffers** via `esp_lcd_rgb_panel_get_frame_buffer(panel, 2, ...)`. The
  BSP allocates those framebuffers in PSRAM (`flags.fb_in_psram = 1`,
  `components/bsp/src/peripherals/bsp_lcd.c`) plus a bounce buffer.
- Each framebuffer is 480 x 480 x 2 B = **450 KiB**; two of them = **~900 KiB**
  of Octal PSRAM @ 120 MHz. LVGL renders into the back buffer and the driver
  swaps buffers on VSYNC, which is what removes tearing.

Because the panel is continuously scanning one full framebuffer out of PSRAM,
there is a constant PSRAM **read** load (~450 KiB per refresh). Whatever the
renderer does adds PSRAM **write** load on top of that shared bus. That shared
PSRAM bus is the key to the honest performance expectations below.

---

## The three profiles

| Profile | sdkconfig knobs | What it changes |
|---|---|---|
| **A. Full refresh** (shipping default) | `CONFIG_LCD_AVOID_TEAR=y` + `CONFIG_LCD_LVGL_FULL_REFRESH=y` | Whole 480x480 frame re-rendered every frame |
| **B. Direct mode** | `CONFIG_LCD_AVOID_TEAR=y` + `CONFIG_LCD_LVGL_DIRECT_MODE=y` | Only invalidated areas re-rendered (into both buffers) |
| **C. Dual-core SW render** | `CONFIG_LV_OS_FREERTOS=y` + `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2` | Rasterization split across both CPU cores; composes with A or B |

### Internal-consistency rules (important)

- **A and B are mutually exclusive.** `LCD_LVGL_FULL_REFRESH` and
  `LCD_LVGL_DIRECT_MODE` are a Kconfig `choice`, so exactly one may be selected.
  To switch to B, **comment out** `CONFIG_LCD_LVGL_FULL_REFRESH=y` and
  **uncomment** `CONFIG_LCD_LVGL_DIRECT_MODE=y`. Never set both.
- **Both A and B require `LCD_AVOID_TEAR=y`** (the choice `depends on` it) and
  `LCD_EVB_SCREEN_ROTATION_0`.
- **C requires an OS.** `LV_DRAW_SW_DRAW_UNIT_CNT > 1` with `LV_USE_OS == NONE`
  is a hard compile `#error` in LVGL
  (`managed_components/lvgl__lvgl/src/draw/sw/lv_draw_sw.c:33`). Always pair the
  draw-unit count with `CONFIG_LV_OS_FREERTOS=y`.
- `main/lv_port.c` needs no edits to switch profiles: it already sets
  `.full_refresh` / `.direct_mode` from these symbols, and because they are a
  choice, both `#if` blocks can never be true at once.

---

## Full refresh vs direct mode

**Full refresh (A):** every frame LVGL redraws all 230,400 pixels regardless of
how little changed, into the back framebuffer, then swaps. A one-pixel clock
tick still repaints the whole screen.

- Pro: simplest and artifact-free; no cross-buffer bookkeeping.
- Con: constant maximum render + PSRAM-write cost every frame. FPS is capped by
  how fast the SW renderer + PSRAM can push a full frame, even for tiny updates.

**Direct mode (B):** LVGL renders only the invalidated rectangles, painting them
at absolute coordinates directly into the framebuffer. Because two framebuffers
are swapped, LVGL replays each dirty region into **both** buffers over two
frames to keep them consistent -- roughly 2x the dirty area per change, but
still far less than a full 230,400-pixel frame when the changed area is small.

- Pro: for typical UI motion -- a clock digit, a toggle, an arc/slider nudge --
  it renders and writes a fraction of the pixels, so lower CPU and higher FPS.
- Con: for full-screen changes (page transitions, full repaints) it is **no
  faster** than full refresh, because everything is dirty and gets drawn twice
  across the two buffers. Slightly more complex invalidation.

**Honest expectation:** direct mode is the most promising win for *this* UI. The
overhaul's motion direction is explicitly "animate small objects, never
full-screen pushes," which is exactly the workload direct mode optimizes. Expect
a meaningful FPS/CPU improvement during small animations and idle-with-clock,
and roughly parity during swipes/page changes. It is not a guaranteed win in
every scene -- measure before shipping.

---

## Dual-core software rendering (Profile C)

`CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2` makes LVGL spawn a second software render
worker thread; with `CONFIG_LV_OS_FREERTOS=y` these are FreeRTOS tasks
("swdraw"). The draw dispatcher splits each frame's draw tasks between the two
units so both ESP32-S3 cores rasterize in parallel.

Cleanly supported here at the config/compile level:
- LVGL provides the `#error` guard so an inconsistent config fails loudly rather
  than silently.
- `esp_lvgl_port` is agnostic to draw-unit count: it owns its own recursive
  mutex (`lvgl_port_lock`) and never references `LV_USE_OS`, `lv_lock`, or the
  draw units. The worker threads are created and synchronized entirely inside
  LVGL's draw core. So this is a config-only change -- no port code involved.

**Honest expectation: gains are likely modest on this board, and this is the
least-certain of the three profiles.** Reasons:
- The two framebuffers are in a single shared Octal PSRAM. Full-frame RGB
  rendering is dominated by PSRAM **write** bandwidth (and competes with the
  panel's continuous scan-out read). Two cores contending for one PSRAM bus give
  sub-linear speedup -- nothing like 2x for fill-heavy frames.
- It helps most when frames are **CPU-bound** rather than bandwidth-bound:
  gradients, shadows, opacity/blended layers, anti-aliased arcs. Flat fills
  benefit least.
- Best composed with **direct mode**: smaller dirty regions mean the per-region
  rasterization work parallelizes better against the fixed scan-out cost.
- Costs: ~2x `LV_DRAW_THREAD_STACK_SIZE` (default 8 KB) of **internal** RAM for
  the worker stacks, plus a second core busy during draw (it also runs the
  network stack in this firmware). Tune `LV_DRAW_THREAD_PRIO` (0-4, default 3)
  and `LV_DRAW_THREAD_STACK_SIZE` if you see starvation or stack overflow.

Treat Profile C as an experiment to validate on hardware, not a free win.

---

## Perf monitor

Enabled in `sdkconfig.defaults` **for the UI overhaul only** (flip OFF for
release):

```
CONFIG_LV_USE_SYSMON=y            # required dependency
CONFIG_LV_USE_PERF_MONITOR=y      # on-screen FPS + CPU overlay (bottom-right)
# CONFIG_LV_USE_PERF_MONITOR_LOG_MODE=y   # also print FPS/CPU over serial
```

- `LV_USE_PERF_MONITOR` `depends on LV_USE_SYSMON` -- if you enable only the
  former, Kconfig silently drops it. Both are set together above.
- The overlay shows FPS and CPU %. Note LVGL's FPS counter reflects render/
  refresh activity; a static screen legitimately reports low FPS because nothing
  is being redrawn -- compare **under the same animation**, not at idle.
- `LV_USE_PERF_MONITOR_LOG_MODE` streams the same numbers over the serial
  console (`./dev monitor`), which is the easiest way to capture exact figures.
- The memory monitor (`LV_USE_MEM_MONITOR`) is unavailable: it needs
  `LV_USE_BUILTIN_MALLOC`, but this firmware routes `lv_malloc` to the
  C-library/PSRAM heap (`CONFIG_LV_USE_CLIB_MALLOC=y`).

**Release checklist:** before shipping, comment out `CONFIG_LV_USE_SYSMON`,
`CONFIG_LV_USE_PERF_MONITOR`, and (if used) `..._LOG_MODE`, and revert any
alternate render profile back to Profile A.

---

## A/B test procedure (on hardware)

FPS must be measured on the real panel -- it cannot be measured in CI or in the
simulator. Steps:

1. **Baseline (Profile A).** With the shipping default plus the perf monitor
   enabled, build, flash, and monitor:
   ```
   ./dev build --no-clean
   ./dev flash
   ./dev monitor        # Ctrl-] to exit
   ```
   Exercise a fixed, repeatable scenario: e.g. leave the clock/temperature card
   animating for 30 s, then do 5 tile swipes. Record the FPS overlay (and the
   serial FPS/CPU lines if `LOG_MODE` is on). Note both the idle-animation FPS
   and the swipe FPS.

2. **Profile B (direct mode).** In `sdkconfig.defaults`, comment out
   `CONFIG_LCD_LVGL_FULL_REFRESH=y` and uncomment `CONFIG_LCD_LVGL_DIRECT_MODE=y`.
   A render-mode change alters generated config, so do a clean reconfigure:
   ```
   ./dev fullclean && ./dev build
   ./dev flash && ./dev monitor
   ```
   Repeat the exact same scenario and record FPS/CPU. Watch for visual artifacts
   during swipes and partial updates.

3. **Profile C (dual-core), optionally on top of A or B.** Uncomment
   `CONFIG_LV_OS_FREERTOS=y` and `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`, then
   `./dev fullclean && ./dev build && ./dev flash && ./dev monitor`. Compare the
   same scenario. Also watch the serial log for task-watchdog or stack-overflow
   warnings; bump `LV_DRAW_THREAD_STACK_SIZE` if needed.

4. **Decide and revert.** Keep whichever profile wins on your scenarios. Then
   restore Profile A as the committed default (or commit the winner
   deliberately), and **disable the perf monitor** before release.

Compare like-for-like: same scene, same duration, same interactions. A single
average FPS number at idle is misleading -- capture idle-animation FPS and
swipe/transition FPS separately, since the profiles trade off differently
between those two workloads.
