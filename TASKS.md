# SenseCAP Indicator HA — 任务追踪

分支：`refactor/vertical-slice`

---

## 已完成

- [x] Phase 2a — 事件 manifest（view_data.h）、ARCHITECTURE.md、ha/README.md
- [x] Phase 2c — ha.h 拆分为 4 个域头文件
- [x] Phase 2d — scripts/test_ha_switch_protocol.py（11 个 MQTT 协议测试）
- [x] Phase 3 — HA 域垂直切片（main/ha/）
- [x] Phase 3 — WiFi 域垂直切片（main/wifi/）
- [x] Phase 3 — Sensor 域垂直切片（main/sensor/）
- [x] Phase 3 — Display 域垂直切片（main/display/）
- [x] Phase 3 — RP2040 域垂直切片（main/rp2040/）
- [x] Phase 3 — btn/mqtt/storage/cmd 域垂直切片
- [x] main/app/ 空 stub .c 文件全部删除（2026-05-27）
- [x] Build 问题修复（shim guard 碰撞、missing wifi headers）
- [x] 架构扫描工具 architecture_scan.py（持续验证无 ui.h 泄漏）

---

## 进行中

- [ ] **清理 main/app/ 过渡 shim .h 文件**
  - 将所有 `#include "indicator_xxx.h"` 调用方改为直接 include 新域头文件
  - 删除 app/ 下所有 shim .h 文件
  - 若 app/ 仅剩 AGENTS.md，从 CMakeLists DIRECTORIES_TO_INCLUDE 移除 "app"

---

## 待办

- [ ] **Phase 1** — LVGL v9 + ESP Component Manager
  - 用 `idf_component.yml` 替换本地 components/lvgl/
  - 引入 espressif/esp_lvgl_port v2.8.0
  - 用 lv_api_map_v8.h 做临时桥接
  - 需要全面 UI 重构

- [ ] **Phase 4** — 导航重构 → lv_tileview
  - 替换当前 Squareline 跳转式切换（_ui_screen_change）
  - 实现跟手滑动（swipe UX）

- [ ] **合并到 main**
  - 完整 build 验证通过后 PR merge

---

## 架构原则（供 Agent 参考）

1. **垂直切片**：每个域自包含，互不依赖实现细节
2. **ui.h 隔离**：每个域只有 `*_view.c` 可以 `#include "ui.h"`
3. **事件总线**：域间通信只通过 `view_event_handle`（VIEW_EVENT_BASE）
4. **architecture_scan.py**：每次改动后必须通过，CI 门槛
5. **Agent 友好**：每个文件的职责单一，改动范围可预测

## 文件布局

```
main/
  ha/       ← HA 域（mqtt/sensor/switch/config）
  wifi/     ← WiFi 域（model/view/list_screen/connect_screen）
  sensor/   ← Sensor 域（model/view）
  display/  ← Display 域（model/view）
  rp2040/   ← RP2040 UART/COBS 域
  btn/      ← 按钮 GPIO 域
  mqtt/     ← MQTT 总线域
  storage/  ← NVS 存储域
  cmd/      ← UART 控制台域
  app/      ← 仅剩 shim .h（待清理）+ AGENTS.md
  ui/       ← Squareline 导出（只读，不改）
  util/     ← 工具函数
```
