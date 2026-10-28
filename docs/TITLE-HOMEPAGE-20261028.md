# 原版首页接入（2026-10-28）

默认启动使用 `topmenu2.png` 的夕阳城市首页，以及 `topmenu2Over.png` 的局部高亮；保持逻辑画布 800×600、等比显示与黑边。普通窗口请求 4×，最大化 / Borderless 全屏请求 8×，缺少高清档时沿用资源层的低档回退。首页底图和悬停图先用本机备用 Real-ESRGAN x4plus-anime / ncnn Vulkan 补齐 4×（3200×2400）lossless WebP 并导入 content；随后远端 UI 批次补齐 8×（6400×4800），当前运行包两档齐全，见 [全量高清接入](HD-CONTENT-20261028.md)。这是原图超分，未改构图 / 按钮坐标，未宣称手工重绘。

五个原版按钮均可操作：New Game 进入序章；Load Game 打开现有读档菜单；System 打开中文设置；Memories 打开章节、CG 图鉴、最近存档继续；Exit 进入退出确认。方向键 / Home / End 选择，Enter / Space 确认，鼠标需在同一热区按下与释放。移出热区清除高亮，失焦或鼠标离窗取消未完成的按下操作。首页快捷键 C 继续最近兼容存档。

中文提示在选中时显示于底部，保留原图自带的英文按钮。设置支持窗口 / 无边框全屏、75%～150% 字号、恢复默认字体、文字描边 / 阴影、下一次新游戏的文字速度；拖入字体与 F5～F9 仍然可用。字体配置沿用现有原子保存，文字速度仅在当前进程内生效。设置和鉴赏子页目前是移植功能布局，不声称复刻了原版这些子页。

## 原版观察与移植选择

只读目标 `ef_first_AA.exe`，SHA-256 `9824732313d13136cb2300f33038a655ba480ae01cefc2749028d1b1748b4f9e`。REA / Ghidra 12.1.4 构造函数 `FUN_004b45a0` 中观察到 `topMenu`、`topMenu2` 及派生的 `Over.png` 资源名，并以如下 RECT 建立按钮。边界为半开区间，既用于截图图集裁切，也用于输入命中。

| 按钮 | `[left, top, right, bottom]` |
|---|---|
| NewGame | `[680,43,800,71]` |
| LoadGame | `[680,71,800,102]` |
| System | `[680,102,800,132]` |
| Memories | `[680,132,800,157]` |
| Exit | `[680,157,800,186]` |

依据：构造函数证据 `ev_5af9062e99a0f9e98101c26f1c56b396c22b601a98e5d80eaa34dc257c6d6a80`；字符串查找 `ev_c84a45682b20e9aeafb6392473c672446311dad48573d738a63bc0f267e90ca6`。`FUN_004b3e30` 派发五个动作，`FUN_004b4460` 创建对应场景或退出。构造函数还观察到原版依赖进度切换两个标题变体及 Memories 可用性。

**移植选择**：始终使用带 Memories 的 `topmenu2` 变体，不检查已读、通关或旧存档；章节及图鉴全部开放。底部中文提示、C 快捷键、设置 / 鉴赏子页为移植功能，不是原 exe 的复原结论。

布局和中文标签放在 `examples/title.ui.json`，导入为独立内容 blob，由脚本索引 `title_ui` 引用。运行时仅读 content，不访问原版目录。已有包运行：

```bash
python3 tools/import_scripts.py --refresh-ui
./build/dev/ef_native --content content
```

旧包没有 `title_ui` 时使用同样的已确认坐标；合成 / 最小包缺少首页图时保留可操作的文字回退。取证 bundle 仅在 `work/re/title-ui-20261028/` 保留，已导出并关闭原版分析会话。

## 验证与剩余边界

窗口回归包括合成图集高亮范围、跨按钮按下 / 释放不触发、实际字体配置变化、全屏往返；既有标题流程覆盖四章、继续、读档、返回和电影中释放会话；图鉴窗口回归覆盖从新首页进入和反复返回。实际包的首页 / 设置及无边框全屏往返已通过，800×600 直接 GL 截图已检查；真实包四章 / 继续 / 读档 / 新游戏 / 退出的窗口回归也通过。真实图鉴从新首页进入、翻页、差分、4× / 8× 切换与反复返回同样通过。

本机 dev 全量 **40/40 PASS**（`TMPDIR=/tmp ctest --preset dev --output-on-failure`）；导入改动后的 M0 独立验证 **PASS、failures=0**。原版目录无写入。截图、日志和合成存档仅保留在 build / work。

原版启动动画、菜单音效 / 标题音乐、原版子页外观、中文按钮高清重绘及 Wine 运行截图逐像素校准仍待完成；本次静态观察不用于推断尚未确认的音轨或时序。

## 首页 4× 补齐

此前 UI 类全部 93 张都只有 1×，全量美术超分不包括 UI；计划按去重有 82 项 `upscale_ui_or_redraw`、10 项 `procedural_regen`。本次仅补 `topmenu2.png` 与 `topmenu2Over.png` 两张首页使用图，其他 91 张 UI 尚无高清档。原版未启用的 `topmenu` 两张变体未补。

本机备用命令（两张分别运行，输入原图只读）：

```bash
/home/lrxin/文档/realesrgan-ncnn-vulkan/realesrgan-ncnn-vulkan \
  -i assets/raw/sys/topmenu2.png -o work/title-homepage/upscale/topmenu2-4x.png \
  -s 4 -t 128 -m /home/lrxin/文档/realesrgan-ncnn-vulkan/models \
  -n realesrgan-x4plus-anime -g 1 -j 1:1:1
```

PNG 无损编码为 `assets/hd/sys_4x/topmenu2.webp` / `topmenu2Over.webp`；编码前后逐像素一致，按既有 `import_image` 导入并在包锁内原子更新 manifest。原图、原始归档不变；资源和校验报告不入 Git。完整重新打包会自动发现这两张 4× 资源，无需运行时特例。
