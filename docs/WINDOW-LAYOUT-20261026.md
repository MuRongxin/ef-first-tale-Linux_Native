# 窗口几何、自适应与尺寸记忆（2026-10-26）

## 决策

- **不固定模拟器窗口为原版 800×600**；原版的“窗口大小”只作为历史约束，保留的是：
  800×600 逻辑画布、4:3 画面比例、居中 letterbox、设计坐标输入映射。
- 窗口可自由拖动缩放；不强制窗口保持 4:3（避免与用户拖动冲突），黑边由视口处理。
- 启动尺寸自适应显示器可用区域；用户调整后的尺寸与最大化状态在退出时记忆，下次恢复。

## 启动尺寸优先级

1. `--width/--height`：显式覆盖（必须成对，校验报错）；单边参数不再允许。
2. 记忆尺寸：来自 `window.json`，且必须仍能放进当前显示器可用区域（否则回退自适应）。
3. 自适应：`SDL_GetDisplayUsableBounds`（已扣除任务栏/Dock）的 90% 内，选最大的 4:3 档位：

| 档位 | 尺寸 | 典型屏幕 |
|---|---|---|
| 1× | 800×600 | 1366×768 等 |
| 1.5× | 1200×900 | 1920×1080 |
| 2× | 1600×1200 | 2560×1440 |
| 2.5× | 2000×1500 | 2560×1600 等 |
| 3× | 2400×1800 | 3840×2160 |
| 4× | 3200×2400 | 更大/高缩放屏 |

更小的可用区域按 4:3 缩到最低 320×240。若窗口管理器不提供 usable bounds，回退 display bounds。

## 记忆文件

- 路径：`$XDG_CONFIG_HOME/ef-native/window.json`；无 `XDG_CONFIG_HOME` 时 `~/.config/ef-native/window.json`。
- `--window-config FILE` 覆盖路径（测试、便携、隔离环境）。
- 格式（version 1）：
  ```json
  {"version":1,"width":1200,"height":900,"maximized":false}
  ```
- 宽度范围 320..16384，高度 240..16384；原子写入（临时文件 + rename），大小上限 16 KiB。
- 缺失 → 使用自适应；损坏/版本不符/越界 → `stderr` 告警后忽略并使用自适应，不阻止启动；
  退出时写回合法文件。保存失败只告警，不影响退出。
- 全屏状态**不**持久化；最大化状态持久化。

## 几何跟踪规则

- 只保存“窗口态”尺寸：最大化/全屏期间不把最大化/全屏尺寸当成窗口尺寸。
- 事件：`WINDOW_RESIZED / MAXIMIZED / RESTORED / MINIMIZED / DISPLAY_CHANGED`、
  `ENTER_FULLSCREEN / LEAVE_FULLSCREEN`。
- 进入全屏前记录当前最大化状态；离开全屏恢复。退出时若仍在全屏，保存进入全屏前的窗口态尺寸。
- 显示器变小导致记忆尺寸放不下：启动时自动回退自适应，并在退出时写入新尺寸。
- 显式 `--width/--height` 运行也会更新记忆（下次无参数启动沿用该尺寸）。

## 实现位置

| 位置 | 内容 |
|---|---|
| `src/core/display.hpp/.cpp` | `adaptive_window_size`、`resolve_window_size` 纯函数 |
| `src/settings/window_settings.hpp/.cpp` | 配置路径/加载/校验/原子保存 |
| `src/app/application.hpp` | `Options.width/height` 默认 0=auto；`window_config` |
| `src/main.cpp` | `--width/--height` 成对校验、`--window-config`、帮助 |
| `src/app/application.cpp` | 查询 usable bounds、启动优先级、窗口事件跟踪、退出保存 |

## 测试与验证

- `tests/core_tests.cpp`：768p/1080p/1440p/2160p 档位、小屏回退、显式/记忆/自适应优先级。
- `tests/window_settings_tests.cpp`：round-trip、越界拒绝、损坏/版本/超大回退、原子写无临时残留、XDG/HOME 路径。
- `tests/window_geometry_smoke.py`（xvfb，生成 mini content）：
  显式 900×700 保存并截图 900×700 → 无参数恢复 900×700 → 1000×750 覆盖 →
  损坏配置回退自适应并写回 → 记忆 2400×1800 在 1280×1024 上回退 → 单边 `--width` 报错。
- 手工：Xvfb 1920×1080 → 1200×900；1366×768 → 800×600；截图/`window.json` 与预期一致。
- 限制：窗口位置不记忆；最大化状态自动测试依赖 WM（无 openbox 的环境只验证状态与手工路径）；
  HiDPI 下记录窗口坐标（非帧缓冲像素）；全屏不持久化。

## 与原版差异

原版固定 800×600 窗口；移植保留 800×600 设计坐标与 4:3 画面，但允许现代窗口尺寸/高清资产。
需要像素级对拍时仍可用 `--width 800 --height 600` 强制 1× 窗口。
