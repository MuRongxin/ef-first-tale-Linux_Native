# PAZ 抽取报告 — `ef - the first tale`（英文 Steam 版 + 中文/r18 补丁数据）

> **注（2026-10 更新）**：本文是早期"按需抽样"的抽取报告（788 个文件）。
> 图像资源的**全量解包结果（5,989 文件 / 1.87 GB）与 HD 高清化清单**见
> [`ASSET-PIPELINE.md`](ASSET-PIPELINE.md)。



工具：`paz.py`（本目录）。格式规格：`PAZ_FORMAT.md`。机器可读
统计数据：`re_tools/archive_stats.json`、`re_tools/extract_stats.json`、
`re_tools/toc_check.json`。复现命令：`python3 re_tools/verify_extracted.py`。

下文所有抽取结果均由以下命令产生：

```
python3 paz.py list-all
python3 paz.py extract <archive>.paz extracted/<name> [--limit N] [--filter REGEX]
```

没有失败：**全部 13 个归档都能解密并列出**（共 12,289 个 TOC 条目），且
**每个被抽取的文件都通过了类型检查**（0 个魔数/解码失败）。

---

## 1. 已验证的归档清单（TOC 层面，13/13 个归档）

| 归档 | game id | 条目数 | zlib 压缩数 | 文件字节数 | 解包字节总和 | 数据起点 | 首条目偏移 | 末条目结束 |
|---|---|---|---:|---:|---:|---:|---:|---:|
| bg.paz | ef_first_en_AA | 3457 | 1333 | 1,272,342,060 | 1,379,693,457 | 143164 | 143164 | = 文件大小 |
| bgm.paz | ef_first_en_AA | 29 | 0 | 51,733,692 | 51,732,464 | 1132 | 1132 | = 文件大小 |
| mov.paz | ef_first_en_AA | 4 | 0 | 51,003,452 | 51,002,980 | 460 | 460 | = 文件大小 |
| scr.paz | ef_first_en_AA | 75 | 68 | 405,268 | 1,115,536 | 2612 | 2612 | = 文件大小 |
| se.paz | ef_first_en_AA | 89 | 0 | 6,494,884 | 6,490,889 | 3676 | 3676 | = 文件大小 |
| st.paz | ef_first_en_AA | 1033 | 506 | 121,894,852 | 144,464,192 | 43708 | 43708 | = 文件大小 |
| sys.paz | ef_first_en_AA | 572 | 0 | 20,932,372 | 20,905,364 | 24972 | 24972 | = 文件大小 |
| voice.paz | ef_first_en_AA | 5830 | 0 | 217,589,044 | 217,335,168 | 233252 | 233252 | = 文件大小 |
| r18_cn/bg.paz | ef_first_en_AA_R18Patch | 868 | 388 | 288,256,132 | 331,650,781 | 35884 | 35884 | = 文件大小 |
| r18_cn/scr.paz | ef_first_en_AA_R18Patch | 19 | 12 | 79,868 | 230,158 | 716 | 716 | = 文件大小 |
| r18_cn/st.paz | ef_first_en_AA_R18Patch | 2 | 1 | 125,180 | 151,835 | 124 | 124 | = 文件大小 |
| r18_cn/sys.paz | ef_first_en_AA_R18Patch | 63 | 0 | 1,540,276 | 1,537,107 | 2932 | 2932 | = 文件大小 |
| r18_cn/voice.paz | ef_first_en_AA_R18Patch | 248 | 0 | 13,178,484 | 13,167,589 | 9972 | 9972 | = 文件大小 |

结构校验（全部 13 个，见 `re_tools/toc_check.json`）：
`toc_len == align8(4 + [mov 为 256] + Σ(name_len+1+24))`、`首条目 offset ==
0x24 + toc_len`、`末条目结束 == 文件大小`。这是 TOC 布局以及每个条目摆放位置
正确性的主要证据。

按文件扩展名的内容类型普查（来自各 TOC）：

| 归档 | png | ani | ogg | wav | sc | avi/mpg | psd/txt/tmp/其他 | 无扩展名 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| bg | 2124 | 1333 | | | | | | |
| bgm | | | 29 | | | | | |
| mov | | | | | | 1 mpg + 3 avi | | |
| scr | | | | | 75 | | | |
| se | | | | | | | | 89 ogg¹ |
| st | 527 | 506 | | | | | | |
| sys | 560 + 2 个 `.png-` + 1 个 `.png.tmp` | | | 6 | | | 3 psd | |
| voice | | | | | | | | 5830 ogg¹ |
| r18_cn/bg | 480 | 386 | | | | | 2 txt | |
| r18_cn/scr | | | | | 19 | | | |
| r18_cn/st | 1 | 1 | | | | | | |
| r18_cn/sys | 63 | | | | | | | |
| r18_cn/voice | | | | | | | | 248 ogg¹ |

¹ 无扩展名的条目；全部嗅探为 `OggS`/Vorbis。引擎把 `bgm/se/voice` 归档中的
每个条目都当作音频处理。

---

## 2. 已完整抽取的内容（位于 `extracted/`）

| 路径 | 内容 | 文件数 | 字节数 |
|---|---|---:|---:|
| `extracted/scr/` | scr.paz 的**全部**脚本（`.sc`，原始 GBK 文本） | 75 | 1,115,536 |
| `extracted/sys/` | **全部** UI/配置/字体/表格图像、音效、PSD | 572 | 20,905,364 |
| `extracted/r18_scr/` | **全部** r18 补丁脚本 | 19 | 230,158 |
| `extracted/r18_sys/` | **全部** r18 补丁 UI 图像 | 63 | 1,537,107 |
| `extracted/bg/` | bg.paz 的前 20 个条目（`.png` + `.ani` 样本对的混合） | 20 | 1,715,938 |
| `extracted/st/` | st.paz 的前 20 个条目（按 TOC 顺序的 10 个 PNG + 10 个 ANI） | 20 | 4,385,910 |
| `extracted/se/` | 5 个 OGG 音效样本 | 5 | 269,870 |
| `extracted/bgm/` | 5 个 OGG 音乐样本 | 5 | 7,875,051 |
| `extracted/voice/` | 5 个 OGG 语音样本 | 5 | 167,073 |
| `extracted/mov/` | 2 个小视频样本：`op_logo.mpg`（MPEG-PS）+ `ed_moji_test.avi`（AVI） | 2 | 675,908 |
| `extracted/r18_bg_notes/` | 来自 r18_cn/bg.paz 的 `names.txt`（CP932 名称表）+ `log.txt`（打包器遗留的目录列表） | 2 | 30,146 |
| `extracted/scr_examples/` | 可读示例：原始 ASCII 脚本 + GBK→UTF-8 转码版本 | 7 | ~ |
| **合计** | | **788** | **~39 MB** |

巨型归档（bg/st/voice/bgm/mov）按任务要求**故意不**完整抽取；
只抽取上述条目以及全部 scr/sys。

---

## 3. 解码后的资源格式与标准库可解码性

每个被抽取的文件都用标准解码器打开过（Pillow / Python `wave` /
`ffprobe` / 文本解码）。结果（`re_tools/extract_stats.json`）：0 个失败：

| 类别 | 扩展名 | 内部格式 | 已抽取实例数 | 大小（解包后） | 标准解码器？ | 验证方式 |
|---|---|---|---|---|---|---|
| 图像 | `.png`、`.png-`、`.png.tmp` | PNG（RGBA/RGB，多为 800×600；sys 中还有 999×600 的 CG、107×80 的小缩略图、1×601 的条状图） | 20（bg）+ 20（st）+ 572（sys）+ 63（r18 sys）含 PNG 在内的可见文件总数 | 1.5 KB … 3 MB（最大 2,999,897 B） | **可以**（Pillow/ImageMagick/libpng） | 所有 PNG 的 `Image.open()/load()` 均成功 |
| 动画 | `.ani` | minori 自定义 “ANI”（与同主文件名的 `.png` 配对；头为 `00 01 02 00 00 00 00 00` 或 `00 01 04 00 00 00 00 00`；10 KB … 283 KB） | 6（bg）+ 9（st） | 10,290 … 283,636 B | **无标准格式**（自定义；若移植动画则需逆向；是立绘的眼/嘴/眨眼数据） | 头部普查见 `extract_stats.json` |
| 音频 | `.ogg` | Ogg Vorbis | 89（se）、29（bgm）、5830（voice）、248（r18 voice） | se 5.7 K…220 K；bgm 0.9–2.3 MB；voice 典型约 13–47 K | **可以**（libvorbis/ffmpeg） | 样本：`vorbis`，`se` 22050 Hz 立体声，`bgm` 44100 Hz 立体声，`voice` 44100 Hz **单声道**——全部探测通过 |
| 音频 | `.wav` | RIFF/WAVE PCM | 6（sys） | 4.6 K … 7,356,460 B | **可以**（Python `wave`） | 全部 6 个可解析：单声道/立体声，22050/44100 Hz，PCM16 |
| 视频 | `.mpg` | MPEG-1 节目流（MPEG-1 视频 + MP2 音频），800×600，4 秒 | 1（mov） | 446,468 B | **可以**（ffmpeg） | ffprobe：mpeg1video+mp2，800×600，3.997 秒 |
| 视频 | `.avi` | RIFF AVI，MPEG-4 视频，800×600 | 3（mov） | 229,440 B … 39,668,126 B | **可以**（ffmpeg） | `ed_moji_test.avi`：mpeg4 800×600，6.83 秒；另外两个为相同容器 |
| 图像/源文件 | `.psd` | Adobe Photoshop 8BPS（原样存储，无 RC4——文件名不匹配 png/ogg/sc/avi 规则） | 3（sys：`icons.psd`、`msgPanel.psd`、`gaiji.psd`） | 20,711 / 26,156 / 37,088 B | **可以**（Pillow 的 PSD 插件） | 全部 3 个 `Image.open()/load()` 均成功 |
| 脚本 | `.sc` | 纯文本，**GBK/CP936**，CRLF；引擎命令语言（见 §5） | 75 + 19 | 77 B … 180 KB | **不可以**（自定义文本格式；需要脚本解释器，但人类可读） | 94/94 个文件严格 GBK 解码成功 |
| 名称表 | `.txt`（r18） | CP932/Shift-JIS 文本，`st_miyako<TAB>みやこ` 映射 | 1 | 495 B | 可以（纯文本） | CP932 解码成功 |
| 日志 | `.txt`（r18） | 打包机器（2011 年）上的 ASCII `dir` 列表 | 1 | 29,651 B | 可以 | 文本解码成功 |
| 无扩展名语音 | *（无）* | Ogg Vorbis | 数千个 | 约 5 K…150 K | 可以（嗅探 `OggS`） | 样本 ffprobe 通过 |

给移植者的有用数字：绝大多数图像资源是 PNG（libpng 可直接解码），
全部音频是 Ogg Vorbis / PCM WAV，视频是标准容器中的 MPEG-1/MPEG-4。
**唯一**的自定义二进制资源格式是 `.ani`（动画增量数据）和 `.sc`（脚本文本）；
`.psd` 是真正的 PSD。

---

## 4. r18 补丁（`r18_cn/`）分析

相同的容器/密钥，`game_id = ef_first_en_AA_R18Patch`，独立的数据目录
（exe 中有注册表标志 `SOFTWARE\minori\ef_first_en_r18patch`）。它与基础数据的
名称级别关系如下：

| r18 归档 | 条目数 | 名称也出现在*对应的*基础归档中 | 名称出现在**任意**基础归档中 | 完全新的名称 | 备注 |
|---|---:|---:|---:|---:|---|
| bg | 868 | 0（基础 bg 只有背景图） | 1（`st_yuko09f01_bokashi.png` 覆盖 st.paz） | 867 | 480 个 PNG + 386 个 ANI 的 R18 CG/立绘（`evh01–evh05`、`ev01–ev02`），2 个 txt |
| scr | 19 | 13 | 13 | 6 | 13 个**被覆盖**的脚本（大小不同；r18 变体）+ 6 个新 R18 脚本（`113_02_ero.sc`、`216_05*.sc` 等） |
| st | 2 | 0 | 0 | 2 | `st_kei06g05.png`+`.ani` |
| sys | 63 | 4 | 4 | 59 | 4 个被覆盖的 `MemoriesSelect*` 图像 + 59 个新 CG 缩略图（`thumb\...`） |
| voice | 248 | 0 | 0 | 248 | 额外的 R18 语音文件 |

解读（依据内容 + exe 标志推断）：该补丁是一个附加式（additive）
覆盖层，在 R18 标志开启时按文件名合并；它*不是*游戏数据的完整拷贝。
本安装中的基础 `scr.paz` 已经包含中文翻译（GBK）以及部分 `_ero` 脚本；
`r18_cn/scr.paz` 则提供其余被覆盖的/新的 r18 脚本（`test.sc` 设置全局变量
`censored = 0`；r18 脚本使用 `.setGlobal H_kei01 = 1` 等，并用
`[e].if censored == 1 skipit` 来门控露骨场景）。

`r18_cn/bg.paz` 还携带两个普通文件（普通 PAZ 条目）：`names.txt`
（CP932 的立绘 ID → 日文名映射表，供引擎 UI 使用）和 `log.txt`
（原始制作机器上遗留的 2011 年 `dir` 列表，列出 `.psd` 源文件——证明
`sys.paz`/补丁中的 `.psd` 条目是制作过程中的产物）。

---

## 5. 脚本格式（`scr.paz`，与移植关系最大的资源）

* **文件**：`scr.paz` 中 75 个，`r18_cn/scr.paz` 中 19 个；扩展名 `.sc`。
* **大小**：77 B … 180 KB（根目录合计 1,115,536 B；25,912 行）。
* **编码**：文本，**CRLF**，**GBK/CP936**（根目录归档已应用中文
  汉化：68/75 个文件可严格按 GBK 解码且 0 错误，
  按 Shift-JIS 解码失败；7/75 个为纯 ASCII）。原日文版和英文版
  将这些脚本存为 Shift-JIS；中文补丁把文本重新编码为 GBK。
* **压缩**：68/75 个在归档中为 zlib 压缩（`paz.py` 透明处理）；7 个原样存储。
* **行语言**（所有命令/操作符均为 ASCII；只有消息/选项文本是
  本地化的）：

```
[e].transition 0 * 40                  ; 带可选层前缀 [e] / [j] 的命令
[j].transition 0 * 20
.stage * ed02_001a01.png 0 0           ; 背景 / 前景层
.panel 3
.message 100 kei-218_01-1003  在进入暑假后，又过了几天——
.message 110 hir-218_01-0003 纮 「——这我也明白的，可是……」
.playBGM efBGM022.ogg
.playSE hiro_bag_naguru.ogg f 0 *
.wait 150
.shakeScreen R 10 10
.setGlobal censored = 0
.select 继续聊天:label1 去追宫村:label2 ; 选项文本 : 跳转标签
.if favMiyako_a > 0 miyako_a            ; 条件跳转
.goto miyako_c
.label label1                           ; 标签定义
.chain 100_01.sc                        ; 继续执行另一个脚本文件
#pragma enable_control                  ; 引擎 pragma
#include 111_05_ero.sc                  ; 脚本 include（类似预处理）
.end
```

* **命令使用统计**（根目录 scr，29 种不同命令，25,838 个非空行）：

  | 命令 | 使用次数 | 命令 | 使用次数 |
  |---|---:|---|---:|
  | `.message` | 14073 | `.playVoice` | 12 |
  | `.transition` | 5254 | `.goto` | 10 |
  | `.stage` | 4076 | `.movie` | 8 |
  | `.panel` | 495 | `.select` | 7 |
  | `.playBGM` | 383 | `.endScroll` | 43 |
  | `.wait` | 360 | `.label` | 40 |
  | `.playSE` | 230 | `.set` | 39 |
  | `.screen` | 177 | `.vscroll` | 35 |
  | `.overlay` | 148 | `.end` | 28 |
  | `.effect` | 88 | `.scroll` | 28 |
  | `.chain` | 76 | `.pragma` | 26 |
  | `.frame` | 26 | `.if` | 23 |
  | `.hscroll` | 18 | `.shakeScreen` | 17 |
  | `.setGlobal` | 16 | `.HSVfilter` | 14 |
  | `.playSE2` | 12 | | |

* **层前缀**：许多命令带有 `[e]` 或 `[j]` 前缀（`[e].transition
  0 * 40` / `[j].transition 0 * 20` 成对出现且时间参数不同；`[j]`
  也以 `[j]:.wait 300` 以及 `[j]；（…）` 形式的日文制作注释出现）。
  假设（待 VM 分析验证）：`[e]`/`[j]` 选择的是
  英文或日文文本模式下的命令流——英文版携带两套时序变体。
  语义**未知**，此处仅作为观测记录。
* **消息格式**：`.message <编号> <语音 ID>  <文本>` —— `<voice-id>` 选择
  `voice.paz` 中的某个文件（例如 `kei-218_01-1003`），文本可以为空或包含
  反斜杠转义（例如 `\a`）；有些消息没有语音 ID
  （`.message 101   \a`）。
* **在 exe 中找到的命令名表**（`.rdata` 文件偏移 0x176c00–0x176f40，
  转储见 `re_tools/exe_script_command_tokens.txt`）还包含
  ef 未使用但本引擎支持的命令：`.movie2`、`.setFG`、`.char`、
  `.animation`、`.scrollFG`、`.scrollAcc`、`.rotateBG`、`.rotateFG`、`.cutin`、
  `.wscroll`、`.deleteVar`、`.setTimer`、`.waitTimer`、`.waitAnimation`、
  `.waitBGM`、`.exec`、`.debug`，以及角色层 token
  （`bgS/bgV/stH/stS/stV/aBG/rBGdx/aFG/…`、`@define/@pos/@move/@load/@stage/@update/
  @finish`）。这是脚本 VM 分析的自然起点。
* **已保存的示例**：`extracted/scr_examples/`（`test.sc`、`opening.sc`、
  `movie.sc` 为原始文件；`218_01.sc.utf8.txt`、`216_02.sc.utf8.txt`、
  `r18_216_05_ero.sc.utf8.txt` 已转码为 UTF-8 便于阅读）。

---

## 6. 怪癖、边界情况、失败情况

* **失败条目：无。** 全部 12,289 个 TOC 条目都能解密；788 个被抽取文件
  全部通过检查。
* **`st.paz` 的 `.ani` +8 怪癖**：压缩的 `.ani` 解压后为 `original_size + 8`
  字节，末尾附带 8 个 NUL 字节（st.paz 中 506/506，r18_cn/st.paz 中 1/1；bg.paz 的 `.ani`
  则正常）。引擎和打包器只使用 `original_size` 字节；`paz.py`
  会截断并报告该调整。其余所有压缩条目都精确匹配。
* **`sys.paz` 中的非标准扩展名**：`pauseAnime.png-`、
  `iconson.png-`（PNG 内容，打包器保留了末尾的 `-`）、`MemoriesBaseOver.png.tmp`
  （PNG 内容），以及 `voice.paz` 中完全没有扩展名的条目（Ogg Vorbis）。移植时
  必须嗅探内容，而不能相信扩展名。
* **`.psd` 条目**以*原样*存储（未压缩，也没有 RC4，因为 `.psd`
  不匹配任何类型口令）——它们来自制作源文件；Pillow 可以
  读取它们，但引擎未必真的加载它们。
* **混合命名空间**：r18 补丁把立绘放在 `bg.paz`、把 CG
  缩略图放在 `sys.paz`；条目名称可能包含反斜杠（`thumb\th_*.png`）。
  分类资源时应使用归档内的文件命名空间，而不是归档名。
* **`mov.paz` 恰好有 4 个条目**（整个归档 = 51 MB）：`op_logo.mpg`、
  `ef_first_sound.avi`（39.7 MB）、`ef_ED_sound.avi`（10.7 MB）、`ed_moji_test.avi`。
  已抽取样本：前两个（较小），其余两个仅列出。
* **根目录归档已包含中文翻译数据**（GBK 脚本、部分带 r18 性质的
  `_ero.sc` 文件）——本安装处于中文汉化状态，而不是
  原始英文版。`r18_cn/` 是附加的 R18 补丁数据集。

---

## 7. 已知未知项（供移植参考）

1. `.ani` 格式内部结构（自定义动画增量数据；尚未解码——只观测到
   与配对文件的关系和头部字节）。
2. `[e]`/`[j]` 前缀、`.stage` 参数、消息编号、
   `@…` 角色层命令以及 VM 本身的脚本语义（下一阶段，由 Lead 负责）。
3. `.psd`/`names.txt`/`log.txt` 是否被运行时使用。
4. r18 在运行时的合并顺序/优先级（按名称推断，未观测执行）。
5. `simple_key != 0` 以及多分卷 `.pazA` 归档（本处不存在）。
6. `.ani` +8 怪癖的确切原因（打包器填充产物）。
