# ef - the first tale — Linux 原生移植准备包

本目录是逆向分析的最终交付，面向"用原始游戏数据在 Linux 上原生运行 ef - the first tale"。

## 目录（全部为中文；英文原稿保留为同名 `.en.md`）

**先读**
- `文档索引.md` — **中文文档索引**：按实现角色标注该读哪篇、阅读顺序。
- `docs/00-OVERVIEW.md` — **先读这个**：已确认的事实、已完成/未完成清单、策略结论。
- `docs/05-PORT-PLAN.md` — 模块划分、M1~M4 里程碑与验收标准、风险对策。
- `docs/08-技术栈推荐.md` — **技术栈选型**：C++20+SDL3+OpenGL+FreeType+FFmpeg 为首选，Rust/Godot 备选，含三平台打包与许可证清单。

**实现规格**
- `docs/PAZ_FORMAT.md` — PAZ 封包完整格式（头部/TOC、Blowfish 变体与 `rev4` 修正、RC4/zlib 管线、全部密钥、伪代码）。
- `docs/EXTRACTION_REPORT.md` — 抽取验证报告：13 归档条目数、抽取成功率、资源「格式/大小/可解码性」表。
- `docs/SAVE_FORMAT.md` — 存档/配置完整格式（容器、payload、`.tbr` 已读位图、`cgv.dat`、51 个配置键、移植实现指南）。
- `docs/02-SCRIPT-REFERENCE.md` — `.sc` 文本脚本：GBK/CRLF、`[e]`/`[j]` 变体、46 条命令语法与推断语义、控制流。
- `docs/01-PAZ-AND-ASSETS.md` — 归档清单与资源类型；标准格式说明；`.ani` 自定义动画；字体提取。
- `docs/03-TEXT-AND-RENDERING.md` — 800×600 画面/图层模型、文字渲染管线（FreeType 方案）、汉化补丁 GBK 改造复刻清单。
- `docs/04-SAVE-AND-CONFIG.md` — 存档/配置要点与移植映射（详细版见 `SAVE_FORMAT.md`）。
- `docs/07-汉化补丁机制.md` + `docs/patch_table_中文.tsv` — 汉化 DLL 做了什么：67 条内存补丁、SJIS→GBK 改造、IAT/目录重定向、签名溯源。
- `docs/06-RE-ROADMAP.md` — 仍未攻克的项与具体地址/攻法（命令 Execute、`.ani`、UI 坐标、排版常量）。

**工具与数据**
- `tools/paz.py` — PAZ 列目录/解包（唯一依赖 pycryptodome）。
- `tools/sc_parse.py` — `.sc` 解析为 JSON；`tools/sc_asset_audit.py` — 资源引用审计（0 缺失）。
- `tools/save_dump.py` — 存档/配置/图鉴/已读位图解析。
- `assets/MerriweatherMG.ttf` — 从 exe 内嵌资源提取的原始字体（OFL）。
- `docs/asset-audit.json`、`docs/script_command_stats.json`、`docs/script-command-grammar.md` — 语料统计/审计原始数据。
- 解包好的资源：`../analysis/paz/extracted/`（788 个文件）；`../analysis/save/samples/`；`../analysis/dll/`（英文证据原文 + Wine 内存镜像）。

## 快速开始
```bash
cd <游戏目录>
python3 port/tools/paz.py list-all                     # 13 个归档概览
python3 port/tools/paz.py extract scr.paz /tmp/sc      # 解出 75 个脚本
python3 port/tools/sc_parse.py /tmp/sc/*.sc > /tmp/sc.json   # 结构化脚本
python3 port/tools/sc_asset_audit.py                   # 资源完整性审计（0 缺失）
python3 port/tools/save_dump.py save_cn --dir          # 存档/配置解析
```

## 已解出的数据（可直接用于移植开发）
- `../analysis/paz/extracted/`：788 个文件（scr 75、sys 572 全量、r18_scr 19、r18_sys 63、bg/st 各 20、音频样本、mov 样本）。
- `../analysis/save/samples/`：存档/配置/图鉴/已读位图解码样本。
- `../analysis/dll/`：汉化补丁 67 条内存补丁表 + Wine 运行时内存镜像（判断"汉化到底改了什么"的真值）。

## 三个关键结论
1. **数据层已 100% 打通**：PAZ 解密/解压 + 标准资源格式 + 脚本文本 + 存档格式，全部有可运行工具和验证。
2. **脚本是文本解释器，不是字节码 VM**：移植实现 46 条命令的解释器 + 场景状态机即可；语料只实际用到 29 条。
3. **移植版不需要汉化 DLL**：汉化=数据层替换 + exe 内存补丁（SJIS→GBK 文本管线、字体换黑体、目录重定向）。原生实现只需 GBK 解码 + CJK 字体（推荐 Noto Sans CJK SC）+ 本地化资源外置。
