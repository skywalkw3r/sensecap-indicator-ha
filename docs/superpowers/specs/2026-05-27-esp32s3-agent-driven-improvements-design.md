# ESP32S3 Agent-Driven Improvement Design

Date: 2026-05-27
Scope: ESP32S3 screen-side firmware in this repository.

## Context

This project is the Home Assistant firmware for SenseCAP Indicator. The ESP32S3 side owns the screen, LVGL UI, Wi-Fi, MQTT, Home Assistant control flow, command console, display settings, and the UART ingress from RP2040 sensor data.

The starter project is useful as an Agent-Driven reference, but its driver code should not be copied into this repository. For display, touch, board initialization, RP2040 communication, MQTT, Home Assistant behavior, and SquareLine-generated UI, this repository remains the source of truth unless a later test proves a specific replacement is necessary.

The useful starter ideas are about engineering workflow:

- Give agents a clear project knowledge base before they edit code.
- Make module boundaries explicit enough that agents can modify one area without rereading the whole firmware.
- Add low-cost checks that catch architecture drift and unsafe UI/threading patterns.
- Build toward no-hardware verification, but do not block the first improvements on a full desktop simulator.

## Current Shape

The project already has useful foundations:

- `main/main.c` initializes board, LVGL, a dedicated `view_event_handle`, view modules, then model modules.
- `main/indicator_view.c` creates SquareLine UI and registers view-side handlers.
- `main/indicator_model.c` wires storage, button, display, RP2040, sensor, command, Wi-Fi, MQTT, and Home Assistant model modules.
- `main/view_data.h` defines the shared event base, event ids, and payload structs.
- `main/app/` contains domain modules for Wi-Fi, display, sensors, MQTT, HA, storage, commands, buttons, and RP2040 communication.

The main problem is not missing functionality. The problem is that the boundaries are not explicit enough for safe multi-agent work:

- `view_data.h` mixes event ids, payloads, sensor definitions, UI-facing state, BSP include dependencies, and logging includes.
- View handlers directly touch LVGL from event callbacks using `lv_port_sem_take()` / `lv_port_sem_give()` without a single documented rule.
- HA/MQTT/sensor/display events share one broad `VIEW_EVENT_BASE`, so agents must inspect many modules to know which payload belongs to which event.
- Build coverage exists, but there is no project-specific agent check that scans for architectural regressions.
- There is no root `AGENTS.md` or app-layer `AGENTS.md` to make the intended architecture legible to agents.

## Goals

1. Preserve current product behavior and current driver/runtime choices.
2. Make the ESP32S3 screen-side architecture legible to human and AI agents.
3. Add low-risk guardrails before larger refactors.
4. Reduce accidental cross-layer coupling, especially direct LVGL access from model/service code.
5. Create a path toward host-side UI verification without making simulator migration the first step.

## Non-Goals

- Do not copy starter BSP/display/touch driver code.
- Do not migrate LVGL version or replace SquareLine-generated UI as part of this work.
- Do not redesign Home Assistant topics or MQTT payloads.
- Do not change RP2040 firmware or Grove sensor ownership.
- Do not introduce a page registry/linker-section system until the current UI migration need is proven.
- Do not rewrite all event handling in one large patch.

## Recommended Approach

Use a three-stage convergence.

### Stage 1: Agent Knowledge Base And Checks

Add repository-local guidance and lightweight checks without changing runtime behavior.

Deliverables:

- Root `AGENTS.md` explaining stack, boot sequence, module map, ESP32S3/RP2040 boundary, and verification checklist.
- `main/AGENTS.md` for screen-side constraints: LVGL threading, SquareLine UI ownership, event loop usage, storage, display, Wi-Fi, MQTT, HA, sensor ingress.
- `main/app/AGENTS.md` for app module conventions: view modules own LVGL objects, model modules own business logic, event payloads are typed, no direct UI access from background tasks.
- `scripts/architecture_scan.py` to catch the first class of unsafe changes:
  - model/service files including `ui.h` unless explicitly allowlisted;
  - direct LVGL calls outside view/UI files;
  - new event ids added without documented payload comments;
  - BSP includes leaking into shared event/data headers;
  - service-local callback patterns that bypass the shared event loop.
- `scripts/dev_check.py` that runs architecture scan and firmware build by default.

This stage should be the first implementation because it improves every later agent run and has minimal runtime risk.

### Stage 2: Minimal Event And UI Boundary

Introduce small foundations that let existing code converge gradually.

Deliverables:

- `main/app/app_events.h` as the single source for app event base declarations, event ids, and fixed-size payload structs. It can initially mirror existing `VIEW_EVENT_BASE` behavior to avoid a disruptive migration.
- `main/app/app_ui.h` / `app_ui.c` as the documented UI-thread boundary:
  - existing direct `lv_port_sem_take()` sections may remain temporarily;
  - new cross-task UI work should use the shared helper;
  - event handlers should copy payloads before deferring UI work.
- Move pure event/payload definitions out of `view_data.h` over time, leaving compatibility aliases while modules migrate.
- Document event ownership:
  - Wi-Fi model posts Wi-Fi state/list/connect results.
  - Sensor model posts built-in sensor values received from RP2040.
  - HA model owns MQTT entity state and saved switch state.
  - HA view owns screen controls and user input.
  - Display model owns brightness/sleep configuration.

This stage should be incremental. Each module migration should preserve behavior and build after the local patch.

### Stage 3: No-Hardware Verification Surface

Add verification after the architecture is better named.

Deliverables:

- Expand `scripts/dev_check.py` with optional checks:
  - `--skip-build` for fast architecture-only checks;
  - future `--ui-smoke` when host UI support exists.
- Add diagnostics commands or logs that make state visible during serial monitor sessions:
  - Wi-Fi state;
  - MQTT connection state and broker URL;
  - HA switch state snapshot;
  - latest built-in sensor values;
  - heap summary.
- Investigate a host-side UI path only after Stage 1 and Stage 2:
  - reuse SquareLine-generated UI where possible;
  - avoid replacing current LVGL/device drivers;
  - prefer widget-tree or state assertions before screenshot goldens.

## Architecture Rules

These rules should become the project-level agent contract.

- ESP32S3 owns screen, LVGL, Wi-Fi, MQTT, HA control logic, display settings, command console, and RP2040 UART ingress.
- RP2040 owns onboard/Grove sensor acquisition. ESP32S3 receives sensor values; it should not grow direct Grove drivers.
- View files may include `ui.h` and call LVGL APIs.
- Model/service files should not include `ui.h` or hold LVGL object pointers.
- Background tasks and event handlers must not update LVGL objects without using the documented UI boundary.
- Event payload structs must be fixed-size POD data unless there is a documented lifetime rule.
- MQTT topics and Home Assistant payload compatibility are product behavior and must be protected by tests or explicit manual verification.
- The starter project can inspire structure and checks, but this repository's working firmware code wins when behavior conflicts.

## Testing Strategy

Stage 1 verification:

- `python3 scripts/architecture_scan.py`
- `python3 scripts/dev_check.py --skip-build`
- `sh build.sh` or an equivalent ESP-IDF build command

Stage 2 verification:

- Run Stage 1 checks after each migrated module.
- Manually verify at least one event path per changed module:
  - sensor value arrives and updates UI/MQTT;
  - HA switch command updates UI and publishes state;
  - broker IP change persists and restarts/reconfigures MQTT;
  - display brightness changes and persists;
  - Wi-Fi scan/connect flow still renders.

Stage 3 verification:

- Add serial diagnostics first.
- Add host UI checks only after the UI boundary is stable enough to simulate without hardware.

## Open Questions

- Should the first implementation include only `AGENTS.md` files, or include the initial architecture scan scripts in the same patch?
- Should `app_events.h` keep the name `VIEW_EVENT_BASE` for compatibility, or introduce a new `APP_EVENT_BASE` with aliases during migration?
- Which module should be migrated first in Stage 2: HA/MQTT, display settings, Wi-Fi, or sensor value flow?
- Should `build.sh` keep deleting `build/` on every run, or should `dev_check.py` use a non-clean build path for faster agent iteration?

## Recommended First Patch

Start with Stage 1:

1. Add root/app `AGENTS.md` files.
2. Add `scripts/architecture_scan.py`.
3. Add `scripts/dev_check.py`.
4. Run architecture scan.
5. Run firmware build if ESP-IDF is available in the current environment.

This first patch should not modify runtime C code. It creates the guardrails needed for the later event/UI convergence.
