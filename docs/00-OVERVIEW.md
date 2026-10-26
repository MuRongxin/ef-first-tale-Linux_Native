# ef - the first tale — Linux 原生移植准备包（RE 成果总览）

目标：以**原始游戏数据**（不加 Wine）运行游戏的 Linux 原生引擎。
本包提供：格式规格、可复用工具、资源清单、引擎行为证据、剩余未知与攻克路线。

## 1. 目标版本（已确认）
- 发行：minori「ef - the first tale.」英文 Steam 版，`ef_first_en_AA`（**AA = All Ages 全年龄版**），引擎代号 **musica**，exe Ver 1.10（2019-08-11 构建，PE32/i386）。
- 当前目录 = **中文汉化版**（根目录 PAZ 已被替换为 GBK 文本+`_ero` 脚本）+ `r18_cn/` 附加覆盖集 + `ef_first_cn_AA.dll` 汉化运行时补丁。
- 原始分辨率 **800×600**（`.movie ... 800 600`、`BLACK800x75.png`、`.transition` 参数均印证）。

## 2. 已完成（有证据、可复用）
| 子系统 | 状态 | 交付 |
|---|---|---|
| PAZ 封包（13 个，12,289 条目） | ✅ 完全破解（Blowfish-ECB 变体 + RC4 + zlib） | `port/docs/PAZ_FORMAT.md`（中文）、`port/tools/paz.py` |
| 资源格式 | ✅ PNG/Ogg Vorbis/WAV PCM/AVI/MPEG-PS/PSD 均为标准格式；`.ani`/`.sc` 自定义 | `port/docs/EXTRACTION_REPORT.md`（中文） |
| 脚本格式 | ✅ 纯文本 GBK/CRLF，94 个文件、25,912 行、46 条命令类 | `port/docs/02-SCRIPT-REFERENCE.md`、`port/tools/sc_parse.py` |
| 字体 | ✅ exe RCDATA #162 内嵌 TrueType "Merriweather MG" 已提取 | `port/assets/MerriweatherMG.ttf` |
| 存档/配置 | ✅ 无加密：u32+zlib / 纯文本；脚本指针可映射 | `analysis/save/SAVE_FORMAT.md`、`analysis/save/save_dump.py` |
| 汉化补丁机制 | ✅ 67 处运行时内存补丁（SJIS→GBK 文本管线、字体→黑体、UI 资源重定向、目录重定向）；移植版不需要该 DLL | `port/docs/07-汉化补丁机制.md`（中文）、`port/docs/patch_table_中文.tsv`、`port/docs/03-*.md §6` |
| 资源完整性 | ✅ 脚本引用的 3,150 个资源 **全部存在，0 缺失** | `port/docs/asset-audit.json` |

## 3. 尚未完全攻克（移植时按优先级处理，详见 06-RE-ROADMAP.md）
1. 46 个脚本命令的**精确执行语义**（尤其渲染类：`.transition` 类型码、`.panel`、`.screen`、`.frame`、`.scrollAcc`、`.rotate*`、`.cutin`、`.effect` 名称表、`.HSVfilter` 参数含义）。
2. `.ani` 自定义动画格式（与同名 `.png` 配对，头 `00 01 02 00`/`00 01 04 00`）。
3. UI 布局坐标（菜单/存档/图鉴/回想，硬编码在 exe；手册证实 100 存档槽=10 页×10 + 9 快存 + 自动存档）。
4. 文字排版常量（字号、行距、ruby 偏移、自动换行宽度）。
5. `[e].`/`[j].` 双前缀的语义（两种 timing 变体）。
6. `system.dat`（123B 高熵未知格式）、`sysparams.txt` 中 7 个三字母键。

## 4. 工具
- 中文文档索引：`port/文档索引.md`；正式文档均为中文，英文原稿为同名 `.en.md`。
- `port/tools/paz.py` — PAZ list/extract（依赖 pycryptodome）。
- `port/tools/sc_parse.py` — `.sc` → JSON（命令/变体/参数）。
- `port/tools/sc_asset_audit.py` — 脚本资源引用 ↔ 封包清单审计（当前 0 缺失）。
- `analysis/save/save_dump.py` — 存档/配置/图鉴/已读位图解析。
- `port/assets/MerriweatherMG.ttf` — 提取出的内嵌字体（OFL）。

## 5. 移植策略结论
- **离线数据层可直接复用**：现有 PAZ 工具负责导入，GBK 剧本一次转 UTF-8；运行时只读工程自己的 `content/`，不携带 PAZ 解密器。
- **脚本层是文本解释器**，不需要字节码 VM：移植实现一个命令解释器 + 场景状态机即可。
- 汉化版额外需求：GBK 解码、CJK 字体（汉化配置使用系统字体「汉仪文黑-85W」，移植可换任意 CJK 字体并在设置里暴露字体选择）、《`censored` 全局变量门控 r18 内容》。
- **全部 UI 尽量复刻原版**：逆向硬编码坐标并用开发期 Wine 截图对齐，形成外置布局 JSON。
- 原生框架已可运行（C++20/SDL3/OpenGL/FreeType、4×/8× 图像切换），构建与实现边界见 [NATIVE-RUNTIME.md](NATIVE-RUNTIME.md)。
