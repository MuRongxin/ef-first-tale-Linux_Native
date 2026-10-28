# ef - the first tale 移植准备包 — 中文文档索引

> 全部正式文档以中文为主。英文原稿保留为同名 `.en.md`（供对照原文用）。

## 先读（5 分钟掌握全局）
0. `AGENTS.md` — **AI/协作者必读**：工程目的、最新产品决策、硬性规则、验证方式。
1. `README.md` — 公共仓库说明：构建、准备数据、运行、测试。
2. `docs/PROGRESS.md` — **当前实现进度快照**：HEAD、已验证数字、工作区进行中项、剩余项与下一步。
3. `docs/00-OVERVIEW.md` — 已确认的版本/引擎事实、已完成与未完成清单、移植策略。
4. `docs/05-PORT-PLAN.md` — 模块划分、M1~M4 里程碑与验收标准。
5. `docs/NATIVE-RUNTIME.md`、`docs/SCRIPT-RUNTIME.md` — 当前运行时能力与未实现边界。

## 实现规格（写代码时查）
| 文档 | 内容 | 读者 |
|---|---|---|
| `docs/PAZ_FORMAT.md` | **PAZ 封包完整格式**：头部/目录结构、Blowfish 变体与 `rev4` 修正、RC4/zlib 数据管线、全部归档密钥与类型口令、伪代码、未知项 | 数据层实现者（必读） |
| `docs/01-PAZ-AND-ASSETS.md` | 13 个归档清单与资源类型；PNG/Ogg/WAV/AVI/MPEG/PSD 标准格式说明；`.ani` 自定义动画；字体提取 | 数据层/资源管线 |
| `docs/EXTRACTION_REPORT.md` | 抽取验证报告：每个归档条目数、抽取成功率、每类资源「格式/大小/可解码性」表 | 验收/对拍 |
| `docs/02-SCRIPT-REFERENCE.md` | **`.sc` 文本脚本**：GBK/CRLF、`[e]`/`[j]` 变体、46 条命令语法与推断语义、语料统计、控制流与变量 | 脚本解释器实现者（必读） |
| `docs/03-TEXT-AND-RENDERING.md` | 800×600 画面与图层模型、文字渲染管线（FreeType 方案）、汉化补丁的 GBK 改造复刻清单 | 渲染/文字实现者（必读） |
| `docs/04-SAVE-AND-CONFIG.md` | 存档/配置/已读位图/CG 图鉴格式要点与移植映射 | 存档/设置实现者 |
| `docs/SAVE_FORMAT.md` | **存档完整格式**（780 行）：容器、payload 语法、变量/backlog 字段、`.tbr` 已读位图、`cgv.dat`、`sysparams` 51 键、脚本指针定位 | 存档实现者（必读） |
| `docs/07-汉化补丁机制.md` | 汉化 DLL 做了什么：67 条内存补丁分类、SJIS→GBK 文本引擎改造、字体/枚举、IAT 与目录重定向、签名溯源、移植结论 | 理解汉化版行为/文本处理 |
| `docs/patch_table_中文.tsv` | 67 条补丁逐条表（RVA/长度/原字节→新字节/作用） | 核对汉化细节 |
| `docs/06-RE-ROADMAP.md` | 仍未攻克的项与具体地址/攻法（命令 Execute、`.ani`、UI 坐标、排版常量） | 后续逆向 |
| `docs/MOVIE-20261026.md` | `.movie` 命令参数/阻塞语义/DirectShow 播放链取证与移植方案（含 FFmpeg 依赖） | 演出/视频实现者 |
| `docs/WINDOW-LAYOUT-20261026.md` | 窗口自适应、自由缩放、letterbox、尺寸/最大化记忆与 `window.json` | 运行时/UI 实现者 |
| `docs/ASSET-PIPELINE.md` | **图像资源全量解包 + HD 高清化清单**（5,989 文件/1.87GB、分类、去重、超分计划） | 资源管线/高清化 |
| `docs/UPSCALE-REMOTE-PIPELINE.md` | **远端 PyTorch 4x/8x 流水线的输入/输出适配**（源/输出映射、命令、部署、边界） | 高清化执行 |
| `docs/UPSCALE-TOOLCHAIN.md` | **高清化工具链**（本机 Real-ESRGAN ncnn-vulkan 实测、模型选型、逐条命令、倍率建议） | 高清化/渲染 |
| `docs/08-技术栈推荐.md` | **推荐技术栈**（C++20+SDL3+OpenGL+FreeType+FFmpeg）、Rust/Godot 备选、三平台打包、许可证、起步骨架 | 立项/架构决策（必读） |
| `docs/NATIVE-RUNTIME.md` | 原生框架/content v1/窗口档位/图像与中文渲染/验证细节与边界 | 运行时实现者（必读） |
| `docs/SCRIPT-RUNTIME.md` | 剧本导入、基础执行/文字校阅、语料审计与交互验收 | 剧本运行时实现者（必读） |
| `docs/PROGRESS.md` | **当前实现进度快照**：HEAD、已验证数字、进行中项、剩余项、下一步 | 所有人 |

## 工具与数据
- `tools/paz.py` — PAZ 列目录/解包（基准实现，依赖 pycryptodome）。
- `tools/paz_fast.py` — 加速实现（与基准逐字节对拍）。
- `tools/toc_scan.py` — 13 归档 TOC 导出。
- `tools/extract_assets.py` / `tools/extract_videos.py` — 图像/视频抽取。
- `tools/build_image_inventory.py` — 分类/去重/HD 计划生成。
- `tools/prepare_content.py` — 从原游戏/HD 产物/原音频生成 `content/` 运行时包。
- `tools/package_content.py`、`tools/import_scripts.py`、`tools/content_resources.py` — 打包、剧本导入、类型化引用审计。
- `tools/sc_parse.py` — `.sc` 解析为 JSON；`tools/sc_asset_audit.py` — 脚本资源引用审计。
- `tools/save_dump.py` — 存档/配置/图鉴/已读位图解析。
- `assets/fonts/MerriweatherMG.ttf` — 原始拉丁字体（OFL）。
- `docs/asset-audit.json`、`docs/script_command_stats.json`、`docs/script-command-grammar.md` — 语料统计/审计原始数据（JSON/自动生成）。
- 数据位置：`assets/raw/`（不入库）、`assets/hd/`（不入库）、`assets/manifest/`（本地生成）、`content/`（不入库）、验证报告 `work/verify/`。

## 阅读顺序建议
实现进度：`PROGRESS` → `NATIVE-RUNTIME`/`SCRIPT-RUNTIME`；
规格：`PAZ_FORMAT` → `01` → `02` → `03` → `04`/`SAVE_FORMAT` → `05` 方案 → `06` 后续；
规则：`AGENTS.md`（最新产品决策与硬性约束）。

## 2026-10-28 存档与菜单

- [原生存档 v1](NATIVE-SAVES-20261027.md)：状态捕获、候选会话恢复与单文件入口。
- [普通/快速槽位](SAVE-SLOTS-20261027.md)：100 普通槽、9 快速槽轮换、列表与命令行。
- [菜单布局取证与模型](SAVE-MENU-LAYOUT-20261028.md)：原版坐标/鼠标热区/导航、90 原版普通槽与100槽移植扩展；窗口菜单尚未接入。
