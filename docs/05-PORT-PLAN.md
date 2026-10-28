# 05 — Linux 原生移植方案与里程碑

以 `AGENTS.md §5` 产品决策为准。当前可运行框架的命令与边界见
[NATIVE-RUNTIME.md](NATIVE-RUNTIME.md)，技术选择背景见 [08-技术栈推荐.md](08-技术栈推荐.md)。

## 1. 技术与数据边界

- C++20 + CMake，SDL3 窗口/输入，OpenGL 3.3 Core 渲染；逻辑坐标 800×600，按物理像素输出。
- 图像：PNG（开发回退）与 WebP；普通窗口选 4×，最大化/Borderless 选 8×；素材按 `design_size` 映射。
- 文字：FreeType，包内 CJK 矢量字体；导入器先完成 GBK→UTF-8，运行时只处理 UTF-8。
- 运行时仅访问 `content/`。PAZ/Blowfish/RC4 留在离线 Python 工具，不进入 C++ 运行时。
- 音频：原版 Ogg/WAV 按位导入，stb_vorbis + miniaudio 混音，BGM 整曲循环；尚待接入。
- 视频：H.264 重制片（建议 4K），时长/帧率对齐原片，FFmpeg 解码；尚待接入。
- 新版本化存档 + 一次性原版迁移，主定位锚点为 script + L0。
- 全部 UI 尽量复刻原版：静态坐标逆向 + 开发期 Wine 截图对齐 → 外置布局/状态/九宫格 JSON。
- **全部内容默认开放**：章节/CG/回想/音乐鉴赏不设通关、已读或跨周目解锁门槛；已读仅作跳过等体验用途。见 [产品决策](PRODUCT-DECISIONS.md)。

## 2. 模块与已建目录

```text
src/
  app/          SDL3 生命周期、事件循环、预览入口
  core/         档位、视口、固定步长、UTF-8
  data/         content manifest、PNG/WebP 解码与校验
  render/       OpenGL 图层、alpha 合成、纹理缓存
  text/         FreeType 与字形缓存
  scene/        外置工程预览场景
tools/
  prepare_content.py    少量素材独立预览包
  paz.py / paz_fast.py  离线数据来源（已有）
tests/          核心单测、导入测试、原生渲染像素验证
```

`script/` 已加入；后续加入 `audio/`、`video/`、`save/`、`ui/`，不放置会假报成功的空实现。
框架场景 JSON 与原版 `.sc` 是不同层次。真实剧本已可文字校阅，但原版演出未接通；见 `SCRIPT-RUNTIME.md`。

## 3. 里程碑

**M0 图像解包与高清清单 — 已完成**

5,989 文件、去重/覆盖/HD 计划及独立验证；高清流水线正在远端生成 4×+8×。

**M1 原生框架与数据层 — 框架/图像路径已完成，完整导入待补**

- 已有可运行 SDL3/OpenGL 程序、content v1、PNG/WebP、矢量中文、图层、窗口档位切换。
- 已有预览打包、核心/导入测试及真实帧缓冲像素检查，CI 只使用合成素材。
- 后续：完整脚本/音频/视频导入、全量校验、WebP→KTX2 BC7/ASTC 安装缓存、预取。
- 完整验收：离线导入与 Python PAZ 基准对拍、全部图像/音频解码、脚本资源引用无缺失。

**M2 脚本解释器与场景运行时 — 基础控制流/文字校阅已实现，演出与音频待补**

- 核心命令：`.message .wait .label .goto .if .select .set .setGlobal .chain .end .playBGM .playSE .playVoice .stage .overlay .transition .panel`。
- 状态机、背景/立绘/色调/文字图层、消息推进、已读位图、音频。
- 验收：94 个脚本解析，`test.sc`→`000_01mugai.sc` 起始流程可运行，资源审计 0 缺失。

**M3 流程/UI/存档 — 待实现**

标题、设置、选项、存读档、backlog、回想、图鉴、音乐与 r18 开关；
新格式往返一致，原版存档一次性迁移；UI 布局须对应逆向与截图证据。

**M4 保真与收尾 — 待实现**

遮罩转场/效果、滚动/旋转/震屏、`.ani`、视频、文字时序与布局、性能及分发。

## 4. 风险与对策

| 风险 | 对策 |
|---|---|
| 脚本命令语义未确认 | 按 `06-RE-ROADMAP.md` 补证据；明确记录近似，不能假报完整支持 |
| `.ani` 未解完 | 保留配对静态 PNG 回退，待格式证据后导出帧序列/anim.json |
| UI 坐标未知 | 逆向 + Wine 开发截图，不能用自行设计的工程预览替代正式复刻 |
| 高清超纹理边长/显存 | 按真实像素验证，记录降级；后续加入 KTX2 缓存与分块/场景预算 |
| 字号/行距/ruby 未知 | 参数化并对照原版证据；当前简单排版只用于工程验证 |
| 同名不同图像 | manifest 明确 tag，不在框架中猜 bg/st 的覆盖优先级 |

## 5. 验证入口

`ctest --preset dev`、`ef_native --validate-content`、`tests/render_smoke.py`；
修改数据层或工具后仍需 `python3 work/verify/verify.py`。
具体命令与环境说明见 `NATIVE-RUNTIME.md`。
