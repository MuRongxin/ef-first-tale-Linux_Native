# 01 — PAZ 封包与资源格式（移植数据层）

完整可照抄规格：`port/docs/PAZ_FORMAT.md`（中文，字段表+伪代码+全部 key；英文原稿 `PAZ_FORMAT.en.md`）；实现参考：`port/tools/paz.py`。
唯一外部依赖：`pycryptodome`（移植时用 OpenSSL/Crypto++ 标准 Blowfish + `rev4` 字节序修正即可）。

## 1. 归档清单（13 个，12,289 条目）
| 归档 | 条目 | 压缩条目 | 内容 |
|---|---|---|---|
| bg.paz | 3457 | 1333 | 背景 PNG（800×600 及更宽/高滚动图） |
| bgm.paz | 29 | 0 | BGM Ogg Vorbis 44.1kHz stereo |
| mov.paz | 4 | 0 | 视频 AVI/MPEG-PS（`ed_moji_test.avi`, `ef_first_sound.avi`, `ef_ED_sound.avi`, …） |
| scr.paz | 75 | 68 | 脚本 `.sc`（文本，GBK/CP932） |
| se.paz | 89 | 0 | 音效 Ogg 22.05kHz |
| st.paz | 1033 | 506 | 立绘/场景 PNG + `.ani` 动画配对 |
| sys.paz | 572 | 0 | 系统 UI：153 PNG（菜单/存档/图鉴/滑块…）、6 WAV、3 PSD、thumb/ |
| voice.paz | 5830 | 0 | 语音 Ogg 44.1kHz mono（无扩展名，嗅探 `OggS`） |
| r18_cn/{bg,scr,st,sys,voice} | 868/19/2/63/248 | — | R18 补丁覆盖集（与基础集仅 18 个重名，其余新增；用同一套 key） |

## 2. 格式要点
- 头：`0x00` 32B NUL 结尾游戏 ID（`ef_first_en_AA` / `ef_first_en_AA_R18Patch`）；`0x20` u32 `toc_len_obf`（高字节为 simple XOR key，本作全为 0）。
- TOC：`0x24` 起，长度按高字节反混淆后的 `toc_len`；**Blowfish-ECB**（32B per-archive key；引擎变体：key 字大端、64bit 块小端字 → `rev4` 修正）。TOC 明文 `u32 count` + N×(CStr 文件名 CP932, u64 offset, u32 original_size, u32 stored_size, u32 aligned_size, u32 is_packed)（mov 另有 256B seed）。
- 数据：XOR(simple_key) → Blowfish-ECB(data key) → 未压缩且扩展名 png/ogg/sc/avi/mpg（或该归档为音频）时 RC4（key = `小写文件名 + " " + %08X stored_size + " " + 类型口令`）→ 若 is_packed 则 zlib inflate 至 original_size。`mov.paz` 特例：seed 派生 RC4 keystream 循环 XOR。
- 密钥来源：exe `.data` @0x1a7ae0（每归档 32B TOC + 32B DATA，存储值逐字节取反）；3 个 ef 变体完整 key 表见 `analysis/paz/PAZ_FORMAT.md` §6。

## 3. 资源格式与可解码性（实测）
| 类型 | 格式 | 移植方案 |
|---|---|---|
| 背景/立绘/UI | **PNG**（800×600 RGB 非交错；UI 小图；`.png-`/`png.tmp` 为假扩展名但内容仍是 PNG） | libpng / stb_image |
| 立绘动画 | **`.ani`（自定义）**，与同名 `.png` 配对，头 `00 01 02 00`/`00 01 04 00` | 需实现（见 06） |
| BGM/SE/语音 | **Ogg Vorbis**（bgm 44.1k stereo，se 22.05k，voice 44.1k mono） | libvorbis/stb_vorbis |
| 按键音/测试音 | WAV PCM | 直接解析 |
| 视频 | AVI / MPEG-PS | FFmpeg(libav) 或 pl_mpeg(MPEG) |
| PSD | 8BPS（3 个源文件，可忽略） | Pillow/直接跳过 |
| 脚本 | `.sc` 文本 | 自行解析（见 02） |

## 4. 字体（关键）
- exe **RCDATA #162** 内嵌 TrueType：`Merriweather MG`（28,432B，16 tables；OFL，MangaGamer 定制；已提取 `port/assets/MerriweatherMG.ttf`）。
- 引擎经 `AddFontMemResourceEx` 注册、`CreateFontA("Merriweather MG")` 选字，用 **GDI + 自绘光栅器**（`GetGlyphOutlineA/W` 取轮廓、`GetTextMetricsA`/`GetCharWidth32A` 取度量、`CreateCompatibleDC`/`SelectObject`），非直接 `TextOut`。
- 汉化版通过配置 `fontname` 使用系统 CJK 字体（存档实测为 GBK 名「汉仪文黑-85W」）；`symbolFontname` 为符号字体。移植版建议 FreeType + 可配置 CJK 字体。

## 5. r18 附加集
`r18_cn/` 为覆盖补丁：同名前缀归档在运行时优先（新增/替换脚本与图像）；脚本以全局变量 `censored` / `H_*` 门控；移植时按 `r18_patch_enabled` 设置模拟（原版仅注册表键 `SOFTWARE\minori\ef_first_en_r18patch`）。

## 6. 完整性审计
`port/tools/sc_asset_audit.py` 输出 `port/docs/asset-audit.json`：脚本引用的 3,150 个不同资源（2954 PNG / 118 OGG / 74 SC / 3 AVI / 1 MPG）在归档中**全部命中，0 缺失**。
