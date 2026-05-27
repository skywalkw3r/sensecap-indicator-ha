# ESP32S3 Agent-Driven Stage 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the Stage 1 Agent-Driven guardrails: repository knowledge files, architecture scan, and a single local development check command.

**Architecture:** This stage does not change runtime C code. It adds documentation that tells agents how to work in the existing ESP32S3 screen-side firmware, then adds Python checks that enforce the first safe boundaries while allowlisting known current debt. The scan should pass on the current codebase after implementation and fail when new files introduce the prohibited patterns.

**Tech Stack:** ESP-IDF 5.4.x, C99, LVGL 8-era project code, Python 3 standard library, Git.

---

## Scope Check

The confirmed design covers three stages. This implementation plan covers Stage 1 only:

- Agent knowledge base files.
- Lightweight architecture scanning.
- A `dev_check.py` wrapper.
- Verification and commit.

Stage 2 event/UI boundary refactors and Stage 3 no-hardware UI verification should get separate implementation plans after this stage lands.

## File Structure

- Create `AGENTS.md`: Root project knowledge base for agents. It explains what this firmware is, what not to copy from the starter project, the boot sequence, module map, ESP32S3/RP2040 boundary, and verification workflow.
- Create `main/AGENTS.md`: Screen-side firmware rules for `main/`, including LVGL locking, SquareLine ownership, model/view split, shared event loop, and boot wiring.
- Create `main/app/AGENTS.md`: App-module rules for `main/app/`, including view/model ownership, MQTT/HA compatibility, sensor ingress, display settings, and current known debt.
- Create `scripts/architecture_scan.py`: Python standard-library scanner. It reports architecture violations while ignoring explicit known current debt.
- Create `scripts/dev_check.py`: Python wrapper that runs `architecture_scan.py` and optionally the firmware build.

## Task 1: Add Root Project Knowledge Base

**Files:**
- Create: `AGENTS.md`

- [ ] **Step 1: Verify no root agent guide exists**

Run:

```bash
test ! -f AGENTS.md
```

Expected: command exits `0`.

- [ ] **Step 2: Create `AGENTS.md`**

Write this file:

```markdown
# PROJECT KNOWLEDGE BASE

Scope: SenseCAP Indicator Home Assistant firmware, with emphasis on the ESP32S3 screen-side code.

## Stack

- ESP-IDF 5.4.x
- ESP32-S3 owns display, touch, Wi-Fi, MQTT, Home Assistant control logic, command console, display settings, and UART ingress from RP2040.
- RP2040 owns onboard/Grove sensor acquisition.
- LVGL UI is currently generated from SquareLine under `main/ui/`.

## Starter Project Guidance

The starter project at `/Users/spencer/Seeed/dev/indicator/sensecap-indicator-starter` is a reference for Agent-Driven workflow, not a source of driver code for this firmware.

Use these starter ideas:

- Agent-readable project knowledge files.
- Explicit module boundaries.
- Small verification scripts.
- A path toward no-hardware checks.

Do not import starter BSP/display/touch/RP2040 driver code into this project unless a later scoped test proves a specific replacement is necessary. This repository's working firmware behavior is the source of truth.

## Boot Sequence

Start at `main/main.c`:

1. `bsp_board_init()`
2. `lv_port_init()`
3. create `view_event_handle`
4. `indicator_view_init()`
5. `indicator_model_init()`

View initialization starts SquareLine UI and view handlers. Model initialization starts storage, button, display, RP2040, sensor, command, Wi-Fi, MQTT, and Home Assistant logic.

## Where To Look

| Task | Start Here |
| --- | --- |
| Boot order | `main/main.c` |
| Runtime module wiring | `main/indicator_model.c`, `main/indicator_view.c` |
| Shared event ids and payloads | `main/view_data.h` |
| SquareLine UI objects | `main/ui/ui.h`, `main/ui/ui.c`, `main/ui/screens/` |
| LVGL port and locking | `main/lv_port.h`, `main/lv_port.c` |
| Wi-Fi model/view | `main/app/indicator_wifi_model.c`, `main/app/indicator_wifi_view.c` |
| MQTT controller | `main/app/indicator_mqtt.c`, `main/app/indicator_mqtt.h` |
| Home Assistant model/view | `main/app/indicator_ha_model.c`, `main/app/indicator_ha_view.c` |
| Display settings | `main/app/indicator_display_model.c`, `main/app/indicator_display_view.c` |
| RP2040 UART ingress | `main/app/esp32_rp2040.c` |
| Built-in sensor cache/parser | `main/app/indicator_sensor_model.c`, `main/app/indicator_sensor_view.c` |
| Local development checks | `scripts/dev_check.py` |

## Architecture Rules

- Preserve current product behavior unless a task explicitly says otherwise.
- ESP32S3 receives sensor data from RP2040. Do not add direct Grove drivers on ESP32S3.
- SquareLine-generated UI files under `main/ui/` are generated assets. Avoid hand-editing generated files unless the task is specifically about the generated UI.
- View files may include `ui.h` and call LVGL APIs.
- Model files should not gain new LVGL object ownership.
- Background tasks and event handlers must update LVGL through the documented LVGL lock/deferral pattern for the current code.
- MQTT topics and Home Assistant payload compatibility are product behavior.
- Keep runtime refactors incremental; this project should remain buildable after each patch.

## Verification

Preferred local checks:

```bash
python3 scripts/dev_check.py --skip-build
python3 scripts/dev_check.py
```

If ESP-IDF is not available, run at least:

```bash
python3 scripts/architecture_scan.py
```

The firmware build path is:

```bash
sh build.sh
```

`build.sh` currently deletes `build/` before building. Do not change that behavior as part of Stage 1 unless the user approves it.
```

- [ ] **Step 3: Review root guide**

Run:

```bash
sed -n '1,240p' AGENTS.md
```

Expected: file renders, contains no placeholder markers or instructions to copy starter driver code.

- [ ] **Step 4: Commit root guide**

Run:

```bash
git add AGENTS.md
git commit -m "docs: add root agent knowledge base"
```

Expected: commit succeeds.

## Task 2: Add `main/` And `main/app/` Agent Guides

**Files:**
- Create: `main/AGENTS.md`
- Create: `main/app/AGENTS.md`

- [ ] **Step 1: Verify no scoped agent guides exist**

Run:

```bash
test ! -f main/AGENTS.md && test ! -f main/app/AGENTS.md
```

Expected: command exits `0`.

- [ ] **Step 2: Create `main/AGENTS.md`**

Write this file:

```markdown
# ESP32S3 SCREEN-SIDE GUIDE

Scope: `main/` and child modules for the ESP32S3 side of the Home Assistant firmware.

## Current Runtime Shape

`main/main.c` initializes hardware, LVGL, and a dedicated `view_event_handle`.

`main/indicator_view.c` creates the SquareLine UI with `ui_init()` and registers view-side modules.

`main/indicator_model.c` initializes model/service modules:

- NVS storage
- button handling
- display/backlight settings
- RP2040 UART ingress
- built-in sensor model
- console commands
- Wi-Fi model
- MQTT controller
- Home Assistant model

## Rules

- Do not replace `bsp_board_init()` or `lv_port_init()` with starter project code.
- Treat `main/ui/` as SquareLine-generated UI.
- View modules may include `ui.h` and mutate LVGL objects.
- Model modules should own state, persistence, protocol parsing, MQTT, Wi-Fi, and hardware-facing logic.
- Do not add new UI object ownership to model modules.
- Keep `view_event_handle` as the current event loop until a scoped Stage 2 event migration exists.
- Keep event payloads fixed-size unless the lifetime rule is documented next to the event id.
- Prefer adding small helpers over broad rewrites.

## LVGL Threading

Current code uses `lv_port_sem_take()` and `lv_port_sem_give()` around LVGL calls. Follow the local pattern for Stage 1.

Future Stage 2 work may add an `app_ui_post()` helper. Do not invent another UI deferral API in Stage 1.

## Generated UI

SquareLine files live in:

- `main/ui/ui.h`
- `main/ui/ui.c`
- `main/ui/ui_events.c`
- `main/ui/screens/`
- `main/ui/images/`
- `main/ui/fonts/`

Do not hand-edit generated files for architecture-only tasks.

## Verification

Run:

```bash
python3 scripts/architecture_scan.py
```

Then run the build wrapper when ESP-IDF is available:

```bash
python3 scripts/dev_check.py
```
```

- [ ] **Step 3: Create `main/app/AGENTS.md`**

Write this file:

```markdown
# APP MODULE GUIDE

Scope: `main/app/`.

## Module Map

| Area | Files | Ownership |
| --- | --- | --- |
| Wi-Fi model | `indicator_wifi_model.c`, `indicator_wifi.h` | Wi-Fi scan/connect/state, NVS Wi-Fi config |
| Wi-Fi view | `indicator_wifi_view.c` | Wi-Fi list, password UI, connect feedback |
| MQTT controller | `indicator_mqtt.c`, `indicator_mqtt.h` | Shared MQTT app event loop and instance lifecycle |
| Home Assistant model | `indicator_ha_model.c`, `indicator_ha.h` | HA entities, MQTT payload handling, switch state persistence |
| Home Assistant view | `indicator_ha_view.c` | HA controls and broker IP screen interactions |
| Display model | `indicator_display_model.c`, `indicator_display.h` | Brightness, sleep mode, screen control |
| Display view | `indicator_display_view.c` | Display settings UI controls |
| Sensor model | `indicator_sensor_model.c`, `indicator_sensor.h` | Built-in sensor cache and RP2040 packet parsing |
| Sensor view | `indicator_sensor_view.c` | Sensor labels on screen |
| RP2040 link | `esp32_rp2040.c`, `esp32_rp2040.h` | UART/COBS ingress and sensor shutdown command |
| Commands | `indicator_cmd.c`, `indicator_cmd.h` | Console commands |
| Storage | `indicator_storage_nvs.c`, `indicator_storage_nvs.h` | NVS read/write helpers |

## Rules

- View files own LVGL object updates.
- Model files own state, persistence, protocol parsing, and network behavior.
- Do not add new direct `ui.h` includes to model files.
- Keep MQTT topics and Home Assistant JSON payload compatibility stable.
- Preserve existing switch indexes and sensor key mapping.
- RP2040 packet parsing should stay in the current ESP32S3 ingress path unless a scoped protocol migration exists.
- Stage 1 architecture checks may allowlist existing debt. Do not expand the allowlist without documenting why.

## Known Current Debt

- `indicator_ha_model.c` currently includes `ui.h`; Stage 1 should allowlist this instead of changing runtime behavior.
- `view_data.h` currently mixes event ids, payload structs, sensor definitions, BSP includes, and logging includes.
- Some event handlers update LVGL directly while holding the LVGL semaphore. Stage 2 should converge this behind one UI boundary.

## Safe Change Pattern

1. Identify whether the change belongs to a model file or a view file.
2. Keep runtime behavior compatible.
3. Run `python3 scripts/architecture_scan.py`.
4. Run `python3 scripts/dev_check.py` if ESP-IDF is available.
5. Document manual hardware checks when behavior changes.
```

- [ ] **Step 4: Review scoped guides**

Run:

```bash
sed -n '1,220p' main/AGENTS.md
sed -n '1,260p' main/app/AGENTS.md
```

Expected: both files render, contain no placeholders, and state that starter driver code should not be copied.

- [ ] **Step 5: Commit scoped guides**

Run:

```bash
git add main/AGENTS.md main/app/AGENTS.md
git commit -m "docs: add esp32s3 module agent guides"
```

Expected: commit succeeds.

## Task 3: Add Architecture Scanner

**Files:**
- Create: `scripts/architecture_scan.py`

- [ ] **Step 1: Write failing executable check**

Run:

```bash
python3 scripts/architecture_scan.py
```

Expected: fails with a file-not-found error because the scanner does not exist yet.

- [ ] **Step 2: Create `scripts/architecture_scan.py`**

Write `scripts/architecture_scan.py` to match the committed Stage 1 scanner in this repository. The implementation must use Python standard library only and must include these final behaviors:

- scan model/service `.c` and `.h` files for `ui.h`, `ui/ui.h`, and angle-bracket variants;
- include the central `main/indicator_model.c` in model-file detection;
- scan LVGL calls outside view/UI files;
- scan BSP includes in shared event/data headers;
- scan missing `VIEW_EVENT_*` payload comments;
- scan both `_register_cb(` and `_register_callback(` service-local callback patterns;
- keep current-debt allowlists documented and occurrence-specific where a file may later gain another violation;
- keep `--list-allowlist` output with the required summary lines plus detailed reasons.

The authoritative file content after this Stage 1 work is `scripts/architecture_scan.py`; keep allowlists narrow and tied to documented current debt.

- [ ] **Step 3: Make scanner executable**

Run:

```bash
chmod +x scripts/architecture_scan.py
```

Expected: command exits `0`.

- [ ] **Step 4: Run scanner**

Run:

```bash
python3 scripts/architecture_scan.py
```

Expected:

```text
architecture_scan: OK
```

- [ ] **Step 5: Check allowlist output**

Run:

```bash
python3 scripts/architecture_scan.py --list-allowlist
```

Expected output includes at least these summary lines, plus detailed occurrence and event allowlist entries:

```text
main/app/indicator_ha_model.c: model-ui-include
main/view_data.h: shared-bsp-include
```

- [ ] **Step 6: Commit scanner**

Run:

```bash
git add scripts/architecture_scan.py
git commit -m "chore: add esp32s3 architecture scan"
```

Expected: commit succeeds.

## Task 4: Add Development Check Wrapper

**Files:**
- Create: `scripts/dev_check.py`

- [ ] **Step 1: Write failing wrapper check**

Run:

```bash
python3 scripts/dev_check.py --skip-build
```

Expected: fails with a file-not-found error because the wrapper does not exist yet.

- [ ] **Step 2: Create `scripts/dev_check.py`**

Write this file:

```python
#!/usr/bin/env python3
"""Run local development checks for the ESP32S3 screen firmware."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def run(label: str, cmd: list[str]) -> None:
    print(f"\n==> {label}", flush=True)
    print("$ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="run architecture checks only; useful when ESP-IDF is unavailable",
    )
    args = parser.parse_args()

    run("Architecture scan", ["python3", "scripts/architecture_scan.py"])

    if not args.skip_build:
        run("Firmware build", ["sh", "build.sh"])

    print("\ndev_check: OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"\ndev_check: failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)
```

- [ ] **Step 3: Make wrapper executable**

Run:

```bash
chmod +x scripts/dev_check.py
```

Expected: command exits `0`.

- [ ] **Step 4: Run architecture-only wrapper**

Run:

```bash
python3 scripts/dev_check.py --skip-build
```

Expected output ends with:

```text
architecture_scan: OK

dev_check: OK
```

- [ ] **Step 5: Commit wrapper**

Run:

```bash
git add scripts/dev_check.py
git commit -m "chore: add local development check wrapper"
```

Expected: commit succeeds.

## Task 5: Verify Full Stage 1

**Files:**
- Read: `AGENTS.md`
- Read: `main/AGENTS.md`
- Read: `main/app/AGENTS.md`
- Read: `scripts/architecture_scan.py`
- Read: `scripts/dev_check.py`

- [ ] **Step 1: Run placeholder scan**

Run:

```bash
rg -n "T[B]D|T[O]DO|P[L]ACEHOLDER|c[o]py starter BSP|c[o]py starter display|c[o]py starter touch" AGENTS.md main/AGENTS.md main/app/AGENTS.md scripts/architecture_scan.py scripts/dev_check.py
```

Expected: no matches. `rg` may exit `1` when there are no matches; that is acceptable.

- [ ] **Step 2: Run architecture scan directly**

Run:

```bash
python3 scripts/architecture_scan.py
```

Expected:

```text
architecture_scan: OK
```

- [ ] **Step 3: Run wrapper without firmware build**

Run:

```bash
python3 scripts/dev_check.py --skip-build
```

Expected: exits `0` and prints `dev_check: OK`.

- [ ] **Step 4: Run firmware build if ESP-IDF is available**

Run:

```bash
python3 scripts/dev_check.py
```

Expected when ESP-IDF is configured: architecture scan passes and `sh build.sh` completes.

If ESP-IDF is unavailable in the current shell, record the exact failure in the handoff and keep the architecture-only verification as the completed local check.

- [ ] **Step 5: Check working tree**

Run:

```bash
git status --short
```

Expected: clean working tree after the prior task commits.

## Self-Review Checklist

- Spec coverage: This plan implements Stage 1 deliverables from the design doc: root guide, `main/` guide, `main/app/` guide, architecture scan, and dev check wrapper.
- Out of scope: This plan does not implement Stage 2 event/UI boundary code and does not add a simulator.
- Known debt: The scanner allowlists only documented current debt: the existing `indicator_ha_model.c` generated-UI include, the existing `view_data.h` BSP include, current button callback registrations, the existing `lv_port_init()` boot call in `main.c`, and existing legacy `VIEW_EVENT_*` ids without payload comments.
- Verification: Architecture-only checks are available even when ESP-IDF is not configured; firmware build remains the full check.
