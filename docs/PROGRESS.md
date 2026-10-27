# 实现进度快照（PROGRESS）

> 快照时间：**2026-10-26 18:50**（Asia/Shanghai）
> 数据来源：`git log` / 工作区检查 / `docs/NATIVE-RUNTIME.md`、`docs/SCRIPT-RUNTIME.md`、`docs/UPSCALE-REMOTE-PIPELINE.md` / 最近实现记录（`*-2026102x.md`）。
> 本文件是**进度快照**；能力边界以 `NATIVE-RUNTIME.md` / `SCRIPT-RUNTIME.md` 和最近实现记录为准，格式规格以 `PAZ_FORMAT.md` / `02-SCRIPT-REFERENCE.md` / `SAVE_FORMAT.md` 为准。

## 1. 当前 HEAD 与工作区

- 最新提交：本次电影播放改为 worker 线程解码（质量优先保留 4K；见 [MOVIE-20261026.md](MOVIE-20261026.md)）；此前窗口自适应与尺寸记忆（[WINDOW-LAYOUT-20261026.md](WINDOW-LAYOUT-20261026.md)）、`6e8352a feat: 将运行时剧本迁移为类型化 JSON`、`.movie` 阻塞播放、`13499a7`/`dbaa84d`，资源优先级基线 `ce35020`。docs/ 已随文档提交入库；`AGENTS.md` 仍为本地协作说明不提交，`assets/`、`work/`、`analysis/`、`content/` 继续不入库。工作区另有用户未提交的剧本 JSON/演出 WIP，勿混入本快照。
- 近期提交（2026-10-25/26，尾部）：
  - `06ca3da` 接通基础场景演出与原版音频播放
  - `a6ebf06` 全屏文本续写与分组清页
  - `6a10911` 可持久化自定义字体与中文回退
  - `bb8cd28` 可配置文字描边阴影与基线偏移
  - `e9c7661/. . .` 消息面板/音频交叠/色带/真实后端
  - `41777f0` 步行与切入演出、全角分号制作备注
  - `b3a0b2e` 实现特殊转场 type2/14/16/17
  - `aba8c78` 转场计数器统一整数步进并标注采样依据
  - `3004b44` speed>255 转场对所有类型立即完成
  - `ce35020` 按原版优先级解析 bg/st 同名立绘
- 本机 `build/dev` 自动测试当前 **30/30 PASS**，core ASan **21/21 PASS**；接入 FFmpeg 开发包的构建为 **31/31**（额外 `movie_decode` 解码冒烟）；最近实现记录文档：
  `RUNTIME-STAGE-AUDIO-20261025.md`、`MESSAGE-UI-20261025.md`、`PANEL-TRANSITION-20261025.md`、`NVL-FLOW-20261025.md`、`AUDIO-CROSSFADE-20261026.md`、`SCREEN-20261026.md`、`SHAKE-OVERLAY-20261026.md`、`HSV-MASK-20261026.md`、`CUTIN-WALKING-20261026.md`、`SPECIAL-TRANSITION-20261026.md`、`MOVIE-20261026.md`、`WINDOW-LAYOUT-20261026.md`（文档已纳入 docs/；原始证据与运行产物不入库）。

## 2. 里程碑状态

| 里程碑 | 状态 | 说明 |
|---|---|---|
| 逆向准备包（PAZ/脚本/存档/汉化补丁/技术栈） | ✅ 完成 | 规格文档齐备（中英） |
| M0 图像解包 + HD 清单 | ✅ 完成 | 5,989 文件 / 1,869.8 MB；独立验证 PASS |
| HD 4×/8× 超分（远端 P40） | ✅ 全量完成 | 6,250 文件 / ~35 GB（磁盘 36 GB）；尺寸校验 6,224 OK，本地/远端 SHA-256 一致 |
| M1 原生框架 + content 数据层 | ✅ 完成 | SDL3/OpenGL/FreeType、content v1、PNG/WebP 解码、4×/8× 窗口档位、`--validate-content/--audit-scripts/--audit-resources` |
| M2 剧本解释器 + 场景运行时 | 🟨 进行中（主体已落地） | 已提交：控制流/文字校阅、基础 `.stage` 演出、开场雪花、原版音频播放（stb_vorbis + 四通道 mixer + 交叠淡化 + SDL 输出）、消息窗模式 1/2/3、全屏续写、文字描边/阴影/自定义字体、`.panel` 交叠切换、`.screen` 色带滤色、`.shakeScreen`/`.overlay`、`.HSVfilter`、`.transition` type0/1/2/14/16/17、`.effect` Cutin/WScroll2/end、bg/st 同名立绘优先级、`.frame`、`.stage` 逗号位置注解、`.movie`（FFmpeg 可选依赖）；剩余：转场 type3..13/15/18、`.movie2` 与未用指令、保真收尾等 |
| M3 流程/UI/存档 | 🟨 部分 | 消息窗/文字系统已实现；**完整 UI 复刻**（标题/设置/存读档/回想/图鉴/音乐/确认框/字体选择）与存档未开始 |
| M4 保真收尾 | ⬜ 未开始 | `.ani` 动画、视频播放、图鉴/音乐鉴赏、KTX2 缓存 |

## 3. 已验证的数据规模（content 全量包，不入库）

| 项 | 数值 |
|---|---|
| 图像/档位 | 3,758 张图像 / 10,008 个档位（1×/4×/8× 组合） |
| 剧本 | 94 个 UTF-8 `.sc`（基础 + r18） |
| 音频 | 6,196 个（29 BGM + 89 SE + 6,078 voice），与基准解包逐字节一致，未重编码 |
| 视频 | 4 个 H.264（3840 宽，MKV） |
| 高清档 | 6,250 个已完成 4×/8× 文件全部接入 |
| 原图回退 | 633 张仅 1×：mask 26 / UI 93 / bg 1 / thumb 466 / tint 47（`.ani` 2,226 **未**伪装成已支持） |
| 引用审计 | Python/C++ 一致：14,942 次 / 9,157 种类型化引用；缺失 0；歧义 0（原 19，bg/st 同名立绘已按原生 st 优先解析）；包内丢失文件 0 |

## 4. 已验证的运行时行为

- 启动：`./build/dev/ef_native --content content`，只读 `content/`，不访问原游戏/`assets/`；
- 窗口：普通窗口 4×、最大化/全屏 8×，缺高档资源记录回退；
- 图像：PNG/WebP 解码、`design_size` 映射、物理像素视口/留黑、分层 `screen` 滤色（原生整数公式 + 底对齐色带采样）；
- 文字：FreeType 实际像素字形（Noto Sans CJK TTC face 可配置）、自定义/回退字体、描边与阴影、基线偏移、CJK 禁则、预排版 + 逐码点揭示、全屏续写/分组清页；
- 消息窗：模式 1/2/3 几何与姓名规则、长消息上移、推进条 12 帧、面板交叠切换（约 160 ms 原版时钟）；
- 影片：`.movie` 阻塞播放，FFmpeg 解码在独立 worker 线程完成（有界帧队列 + 音频环背压），纹理按目标矩形绘制、音频混入 SDL 输出、可跳过；4K 软解实测约 58–75 fps（24 fps 原片），“质量优先”保留 4K 不做固定 800 转码；`opening.sc` 静音墙钟完成验证，PipeWire 后端实测 `ef_native` 48 kHz float32 音频流、音频时钟正常；FFmpeg 开发包缺失时严格未支持；
- 窗口：默认按显示器可用区域选最大 4:3 档位、自由拖动缩放/居中 letterbox；退出保存尺寸+最大化，损坏/越界配置回退自适应；`--window-config` 可控路径；验证见 WINDOW-LAYOUT 文档；
- 演出/音频：基础 `.stage`（背景/裁切/前景底对齐）、开场雪花、BGM/SE/SE2/voice 播放、跨曲交叠淡化与退场声部、`.shakeScreen`/`.overlay`/`.HSVfilter`、`.transition` type0/1/2/14/16/17（含 200 项缩放表叠影历史帧）、`.effect` Cutin/WScroll2/end；
- 自动化：ctest 25/25（含音频/合成/转场像素回归）、core ASan 18/18 通过；Xvfb 渲染/键盘/EGL 检查 PASS；真实素材 4×→8×→4× 与 100 条消息追踪 PASS；e/j 开场严格 trace 1694 消息基线无回归，8 个转场脚本 24/30 点位真实执行通过。

## 5. 下一步建议（按依赖顺序）

1. **补齐演出命令**：`.frame`（`dbaa84d`）、`.stage` 逗号位置注解、**`.movie`（已实现，FFmpeg 可选依赖，见 MOVIE-20261026.md）** 均已完成；下一步按 `SPECIAL-TRANSITION-20261026.md` 扩 type3..13/15/18；已实现的 `.transition` type0/1/2/14/16/17 只需原版逐帧对拍收尾；bg/st 同名立绘优先级已解决（st 优先、bg 回退，审计歧义归零）；`.movie2` 与未用指令类待取证；
2. **完整 UI 复刻（M3，全部 UI）**：按已逆向的消息窗经验，逐屏逆向标题/设置/存读档/回想/图鉴/音乐/确认框/字体选择；UI 图 AI 超分 + **去烧死文字**，文字全部 FreeType 重绘；
3. **`.ani` 动画（M4 前置）**：逆向内部帧结构 → PNG 序列 + `anim.json` + 引擎播放器（坐标随配对 PNG 倍率缩放）；
4. **视频播放**（H.264 已在 content 中）与 **KTX2 缓存**（方案 A：WebP 分发 + 首次/后台转码）；
5. **存档/设置**：新格式 + 一次性迁移（规格见 `SAVE_FORMAT.md`）；
6. **打包**：Linux AppImage/Flatpak；Windows/macOS 后续。

## 6. 已知阻塞/待确认

- `.ani` 内部帧结构未解码（2,226 个、8 种头部变体；当前仅静态配对 PNG）；
- 2 张 `bg ↔ st` 同名异图立绘，加载顺序未知 → 19 次引用歧义（`06-RE-ROADMAP.md`）；
- 部分演出命令的 type 表/参数语义、原版 timing 边界（`\a/\w/\v` 等）未完全对拍；
- 原版 UI 除消息窗外的坐标/状态未逆向；
- `[e]/[j]` 在汉化版中的选择规则未确认（当前默认 `e`，可 `--variant` 覆盖）；
- `system.dat` 123 B 未知格式（可忽略/保留）。

## 7. 相关文档

- `NATIVE-RUNTIME.md` / `SCRIPT-RUNTIME.md` — 运行时能力与边界；
- `RUNTIME-STAGE-AUDIO-20261025.md`、`MESSAGE-UI-20261025.md`、`PANEL-TRANSITION-20261025.md`、`NVL-FLOW-20261025.md`、`AUDIO-CROSSFADE-20261026.md`、`SCREEN-20261026.md` — 最近实现记录（含逆向证据与限制）；
- `ASSET-PIPELINE.md` / `UPSCALE-REMOTE-PIPELINE.md` — 解包与 4×/8× 超分；
- `PAZ_FORMAT.md` / `02-SCRIPT-REFERENCE.md` / `SAVE_FORMAT.md` / `03-TEXT-AND-RENDERING.md` — 格式与行为规格；
- `06-RE-ROADMAP.md` — 剩余逆向任务与地址线索；
- `AGENTS.md` — agent 硬性规则与最新产品决策。
