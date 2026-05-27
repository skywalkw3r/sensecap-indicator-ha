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
- [x] 架构扫描工具 architecture_scan.py（持续验证无 ui.h 泄漏）

---

## 进行中

- [ ] **main/app/ 架构整理** — 当前遗留问题：
  - 6 个空 stub .c 文件（indicator_wifi_model/view、sensor_model/view、display_model/view）
  - 4 个 shim .h 文件（indicator_wifi/sensor/display/ha）
  - 剩余待迁移模块：btn、cmd、mqtt、storage
  - rp2040 已迁移至 main/rp2040/ ✓

- [x] **Build 问题修复**（2026-05-27）
  - indicator_display.h / indicator_sensor.h shim guard 碰撞已修复
  - wifi_model.c 补充 esp_wifi.h / esp_netif.h 已修复
  - fullclean build 通过 ✓

---

## 待办

- [ ] **app/ 剩余模块迁移**（逐步，每步 fullclean build 验证）
  - [ ] mqtt → main/mqtt/
  - [ ] btn → main/btn/
  - [ ] storage → main/storage/
  - [ ] cmd → main/cmd/
- [x] rp2040 → main/rp2040/（2026-05-27，build 通过）

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

1. **垂直切片**：每个域（ha/wifi/sensor/display）自包含 model + view + screen 组件
2. **ui.h 隔离**：每个域只有 `*_view.c` 可以 `#include "ui.h"`
3. **事件总线**：域间通信只通过 `view_event_handle`（VIEW_EVENT_BASE）
4. **architecture_scan.py**：每次改动后必须通过，CI 门槛
5. **Agent 友好**：每个文件的职责单一，改动范围可预测

## 文件布局

```
main/
  ha/          ← HA 域（mqtt/sensor/switch/config）
  wifi/        ← WiFi 域（model/view/list_screen/connect_screen）
  sensor/      ← Sensor 域（model/view）
  display/     ← Display 域（model/view）
  app/         ← 剩余通用层（btn/cmd/mqtt/storage/rp2040）【待整理】
  ui/          ← Squareline 导出（只读，不改）
  util/        ← 工具函数
```
