# ef - the first tale — 存档数据 / 配置文件格式分析

> 本文描述原版存档/配置事实。移植版采用 [所有内容默认开放](PRODUCT-DECISIONS.md) 的产品要求，不以原版解锁表、通关或已读记录限制章节与鉴赏入口。


目标目录：`/home/lrxin/Downloads/ef - the first tale/save_cn/minori/ef - the first tale.(AA)/`
游戏：**ef - the first tale**，minori 引擎（"nnl" / *nnl_ft*），可执行文件 `ef_first_en_AA.exe`
（英文 MangaGamer 版）+ 中文化补丁（`ef_first_cn_AA.dll`），r18 补丁资源位于 `r18_cn/`。

分析约束：游戏目录按只读处理；仅使用 `bash / python3 / xxd / strings`；
**未调用任何 REA MCP 工具，未做反汇编，未运行游戏**。所有产物写入 `analysis/save/` 下。
下文每一项结论要么是**观察事实**（可复现的精确字节/哈希），要么是**推断**（已标注）。

---

## 0. 摘要（TL;DR）

| 文件 | 真实格式 | 状态 |
|---|---|---|
| `ef00XX.sav` | `u32 tag=0` + `"nnl_ft 1.00\0"` + `comment\0` + `u32 0` + **明文 zlib** → 文本 payload：`<begin variables>…<end variables>` + `<<begin backlog>>…<<end backlog>>` | 已完全解码 |
| `*.tbr` | `u32 tag=0` + **明文 zlib** → 固定的 **8192 字节（65536 位）已读文本位图**；位下标 = 每个脚本内的文本行 ID `L0`；位为 1 = 该行已读 | 已完全解码并交叉验证 |
| `ef00XX.png` | 标准 PNG，100×75，8-bit RGBA（颜色类型 6），保存时场景的缩略图；与同名 `ef00XX.sav` 一一对应 | 已验证 |
| `cgv.dat` | `u32 tag=0` + **明文 zlib** → 按字母序排列、以换行分隔的列表，共 **2236 个图像文件名**（CG/图鉴注册表，"CG view"） | 容器已解码；精确的解锁语义为推断/未知 |
| `sysparams.txt` | 纯文本，CRLF，`key<TAB>value`，值为 CP936/GBK；**51 个键**的引擎配置 | 已完全解码 |
| `system.dat` | `u32 tag=0` + 123 字节高熵二进制；**格式未知**（非 zlib/gzip/bz2/lzma/raw-deflate/TEA 对齐/XOR） | 未解决 |

本游戏没有任何存档/配置文件使用 TEA/XXTEA/XOR 加密。`.paz` 归档是另一种容器
（以 16/24 字节的 key 字符串开头，例如 `ef_first_en_AA\0`）；**存档文件完全不使用该方案**——
它们就是明文 zlib + 文本，可以直接用标准 zlib 工具编辑。

## 0.1 移植速答（面向 Linux 原生移植的关键确认）

1. **`.sav` 是否含脚本指针？** 是。变量区里有三个字段精确决定“存档位置”：
   * `!script_Filename` — 脚本文件名，如 `216_04.sc`（只有文件名，没有 label 名、没有路径）；
   * `!script_ID` — 当前文本行 ID（即 backlog 的 `L0`，也是 `.tbr` 的位下标），通常是 `100+10k`；
   * `!script_Pointer` — **编译后脚本的指令/操作码下标**（不是 label 名；label 不存盘）。
   10/11 个存档满足 `!script_Pointer == 最后一条 backlog 的 CS2`、`!script_ID == 最后一条 backlog 的 L0`；
   唯一例外是自动存档 `ef0000`（写盘时正在场景中间：`ID=230` 而最后一条 `L0=220`，`Pointer=22` 而 `CS2=19`）。
   证据：`CS2` 随文本行递增但不连续（例：`216_04.sc` 的 `(L0,CS2)` = `(100,5),(110,6),(120,9),(130,10),(140,11)…(5420,969)`），
   中间跳号就是非文本指令，故 `CS2 / !script_Pointer` 是**指令下标**而不是行号。
   → 恢复位置的最小信息 = `script_Filename + script_ID(L0) + script_Pointer`，再叠加变量区其余场景/音频状态和全局变量。
2. **文本编码？** **GBK/CP936，不是 UTF-16**。证据：全部 11 个 `.sav` 的 payload 中没有 UTF-16 BOM（`FF FE`/`FE FF` 均为 0 次），
   没有 “ASCII 字符 + 0x00 高字节” 的 UTF-16LE 模式（0 次），非 ASCII 字节占 payload 的 8.4%，且 73,273 条 backlog 的
   `L2`/`L3` 共 27,056 个值全部可用 `cp936` 无错解码。结构部分（键名、数字、文件名）是 ASCII。
   `.tbr`/`cgv.dat` 是纯二进制/ASCII；`sysparams.txt` 除两个字体名外是 ASCII，字体名是 GBK。
3. **`.tbr` 是缩略图吗？** **不是**。`.tbr` = 每个脚本一份的 **read-text（已读文本）位图**：`u32 tag=0` + zlib →
   **固定 8192 字节 = 65536 bit**；bit 下标 = 该脚本的文本行 ID（`L0`），bit 置 1 = 已读；跨周目持久保存，用于“跳过已读文本”。
   缩略图是另一套：`ef00XX.png` = **100×75 像素、8-bit/通道、RGBA（colour type 6 = RGBA8）、非隔行** 的标准 PNG，
   与同名 `.sav` 一一对应（保存时截取的游戏画面）。`.tbr` 渲染成 1-bpp 图像只是稀疏点阵（见 `samples/06_…REJECTED_view.png`），
   不是任何图像格式。

---

## 1. 目录清单（86 个文件）

```
save_cn/minori/ef - the first tale.(AA)/
├── ef0000.sav, ef0010.sav … ef0019.sav      11 save files  (ef0001…ef0009 are absent)
├── *.tbr                                    61 read-text bitmap files
├── ef0000.png, ef0010.png … ef0019.png      11 save thumbnails (1 per .sav)
├── cgv.dat                                   1 gallery/CG registry
├── sysparams.txt                             1 plain-text config
└── system.dat                                1 unknown 127-byte binary
```
11 + 61 + 11 + 3 = 86 个文件 ✔（任务说明写的是“*ef0000.sav .. ef0019.sav*（20 个槽位？）”；
**实际只存在 11 个**：自动存档 `ef0000` 以及存档第 1 页槽位 `ef0010..ef0019`）。

目录重定向（观察，来自 `ef_first_cn_AA.dll` 的 ASCII 字符串）：
`save_cn\`、`r18_cn`、`ef_first_en2cn.dll`。
英文 exe 想要使用的是 `save\` 和游戏目录 `ef - the first tale.(AA)`（exe 字符串偏移
1538016 `save\`、1538076 `ef - the first tale.(AA)`）；中文 DLL 把
存档根重定向到 `save_cn\`、把 r18 补丁资源根重定向到 `r18_cn\`。所以本目录是中文补丁的
存档根，而不是英文原版的存档根。

存档文件命名已由 exe 确认：格式字符串 **`ef%04d`** + **`.sav`** 位于
`ef_first_AA.exe` 的文件偏移 1576976 与 1576984，旁边是存档列表的日期格式
`%04d/%02d/%02d %02d:%02d`（偏移 1576672）和备注输入框占位符 `sample comment.`（偏移 1576960）。

槽位命名（推断）：`ef0000` = 自动存档；`ef0010..ef0019` = 存档第 1 页，槽位 0..9
（即编号 = page*10 + slot + 10）。快速存档槽（`ef0001..ef0009`）在本快照中不存在；
`sysparams.txt` 中 `quickSaveFileNumber=1`。

---

## 2. `.sav` — 格式

> 移植实现见 §13.2（读档位置恢复）与 §13.8（最小读写伪代码）。

### 2.1 容器结构

偏移量均从文件开头算起（绝对偏移）。全部 11 个文件都严格符合下表。

| 偏移 | 大小 | 类型 | 值 / 含义 |
|---|---|---|---|
| 0x00 | 4 | u32 LE | `0x00000000` — tag / 格式版本（所有文件中均为 0） |
| 0x04 | 12 | char[12] | magic `"nnl_ft 1.00\0"` — 引擎 `nnl`、游戏标记 `ft`、格式版本 `1.00` |
| 0x10 | 变长 | char[] | **存档备注/标题**，以 NUL 结尾，ASCII 或 CP936（引擎默认字符串为 `"Auto save"`、`"Quick save"`；`"sample comment."` 是 exe 中输入框的占位符） |
| 0x10+len+1 | 4 | u32 LE | `0x00000000`（全部 11 个文件中恒为 0——保留字段/标志，用途未知） |
| … | 变长 | zlib 流 | 起始字节 `78 9c`，标准 RFC-1950 zlib，默认压缩级别；流一直延续到文件末尾（每个文件的 `unused_data == 0`） |

示例（hexdump）：

```
ef0000.sav  (comment "Auto save", zlib @ 0x1E)
00000000: 0000 0000 6e6e 6c5f 6674 2031 2e30 3000  ....nnl_ft 1.00.
00000010: 4175 746f 2073 6176 6500 0000 0000 789c  Auto save.....x.

ef0010.sav  (comment "t", zlib @ 0x16)
00000000: 0000 0000 6e6e 6c5f 6674 2031 2e30 3000  ....nnl_ft 1.00.
00000010: 7400 0000 0000 789c ed9c 6d4f db58 16c7  t.....x...mO.X..

ef0011.sav  (comment "2", zlib @ 0x16)
00000000: 0000 0000 6e6e 6c5f 6674 2031 2e30 3000  ....nnl_ft 1.00.
00000010: 3200 0000 0000 789c ecbd eb73 db66 9eef  2.....x....s.f..

ef0019.sav  (comment "14", zlib @ 0x17)
00000000: 0000 0000 6e6e 6c5f 6674 2031 2e30 3000  ....nnl_ft 1.00.
00000010: 3134 0000 0000 0078 9cec bdeb 73db 669e  14.....x....s.f..
```

压缩相关观察：11/11 个文件都能用 `zlib.decompress` 干净解压，`unused_data == 0`；
解压后 payload 为 19,306 … 5,173,258 字节（压缩比约 10:1）。除 zlib 自身的
Adler-32 之外没有任何 CRC/加密包装。**不存在 TEA/XXTEA/XOR 混淆**（已测试；解压后就是明文文本）。

### 2.2 Payload = 文本文件

解压后 payload 是一个**文本文档**（LF 行尾、无 CR、值按字节透明保存）。两个区段按以下顺序出现，
在所有观测到的文件中都存在：

```
<begin variables>\n
!angBG  0000000000000000\n
!bgm_Filename   efBGM019.ogg\n
…
favMiyako       2\n
<end variables>\n
<<begin backlog>>\n
LC      0\n
L0      4560\n
L1      kei-216_04-0251\n
L2      景\n
L3      「我只是想创造一个契机，让自己不再犹豫。」\n
CS1     216_04.sc\n
…
ZZ\n
…            (one block per backlog entry, each terminated by a line "ZZ")
<<end backlog>>\n
```

* 区段标记是字面字符串：`<begin variables>`、`<end variables>`、
  `<<begin backlog>>`、`<<end backlog>>`。（exe 字符串中四个都出现过，可确认。）
* 键与值之间的分隔符是**单个 TAB（`0x09`）**；没有空格/引号。
* 值是原始字节，除脚本正文的内联控制指令外不做转义（见 2.5）。
* `ef0000.sav` 的 payload = 38,913 字节；最大的（`ef0015.sav`）= 5,173,258 字节
  （完整一周目 backlog；见 §2.6）。

### 2.3 Variables 区段

行语法：`key<TAB>value\n`。键分两类：

1. **引擎状态变量**——以 `!` 开头。观察到 43 个不同的键（`!script_Filename`、
   `!script_ID`、`!stage_BGFilename`、`!bgm_Filename`、`!font_Color`、…）。名字/字段已由
   exe 字符串表确认（例如 `!script_Filename`、`!script_Pointer`、`!script_ID`、
   `!font_Color`、`!font_FontSize`、`!font_RubySize`、`!panel_Mode`、`!panel_Filename`、
   `!stage_FGFilename`、`!stage_BGFilename`、`!stage_BGX`、`!stage_BGY`、`!stage_FGX`、`!stage_FGY`、
   `!stage_StandFilename%d`、`!stage_StandPos%d`、`!stage_StandOffs%d`、`!frameFilename`、
   `!frameParam`、`!overlay_FGFilename/BGFilename/STFilename`、`!screen_*`、`!bgH/S/V`、`!stH/S/V`、
   `!bgm_Filename/Volume`、`!se1_Filename`、`!se2_Filename`、`!eff_Method/Filename/Count/Speed/Param`、
   `!tr_Method/RuleFilename/Param`、`!angBG`、`!angFG`、`!rotBG_*`、`!rotFG_*`、`!chL_%d`）。
   只写入非默认/处于激活状态的条目（例如 `!stage_FGFilename` 只在部分存档中出现），
   所以每个存档的变量数量在 37–43 之间变化。
2. **游戏/全局标志变量**——没有 `!` 前缀，值由脚本设置。本批存档中观察到：
   `favMiyako`（宫子好感度计数器，值 1/2）、`favMiyako_a`（1）、`finderCounter`
   （45…2814；"finder" 相关的游戏进度计数器）。这些是存档里最接近“标志块”的东西：
   **游戏进度标志就是脚本变量，原样存放在本区段中**，而不是打包成位图。

部分取值示例（来自 `ef0019.sav`，第 2 章的一个存档）：

| 键 | 值 | 含义（依据 exe 名称 + 行为的推断） |
|---|---|---|
| `!script_Filename` | `216_04.sc` | 当前脚本文件 |
| `!script_ID` | `790` | 当前文本行 ID（与 `.tbr` 位图使用同一套编号） |
| `!script_Pointer` | `111` | 当前脚本指针（指令下标） |
| `!bgm_Filename` / `!bgm_Volume` | `efBGM054.ogg` / `100` | 当前 BGM / 其音量 0–100 |
| `!stage_BGFilename` | `ev02_163a05.png` | 当前背景/CG 图像 |
| `!panel_Mode` | `3` | 消息面板模式 |
| `!tr_Method`、`!tr_Param` | `0`、`40` | 画面转场状态 |
| `!eff_Method` | `fadeout` | 特效状态 |
| `!font_Color` / `FontSize` / `RubySize` | `16777215`（=0xFFFFFF）/ `22` / `10` | 文本风格 |

**存档中不包含**：游玩日期/时间、游玩时长、已读文本位图、BGM/SE/语音音量设置、
CG 图鉴标志、存档页码。（存档列表界面显示的日期/时间由
`%04d/%02d/%02d %02d:%02d` 格式化——推断：取自 `.sav` 的文件时间戳；全部 11 个槽位的 `.sav`
与 `.png` 修改时间相差不超过约 20 ms，例如 `ef0011` 为 2025-11-04 16:36:54。）

### 2.4 Backlog 区段

backlog 是**当前周目已读全部文本的明文历史**。11 个存档合计 72,273 条记录。
一条记录 = 若干 key/value 行，以一行 `ZZ` 结束。

固定基础键（47 个，始终存在，且始终按以下顺序）：

| 键 | 含义（证据） | 观测值 |
|---|---|---|
| `LC` | 本条消息的**行数**（0 = 单行；N = 多行） | 0（73,150）、1（63）、2（17）、3（9）、4（25）、5（9） |
| `L0` | 脚本内的**文本行 ID**；也是 `.tbr` 使用的位下标 | 100、101、102、… 6380；多数为 `100+10k`，分支/特殊行的 ID 不规律（共观察到 698 个不同值） |
| `L1` | 语音文件名（空 = 无语音） | `kei-216_04-0251`、`miy-112_01-0095`、… |
| `L2` | 说话人显示名，**CP936**，可能带控制前缀（`@`、`#`） | `景`（Kei）、`宫子`、`京介`、`优子`、`纮`、`瑞希`、`泉`、`大村`、… |
| `L3` | 消息正文，**CP936**（中文翻译），内联转义（`\n`、`\a`、`\v`、`\w`）按字面保存 | 中文对白 |
| `CS1` | 脚本文件名 | `216_05.sc` |
| `CS2` | 脚本指针（行索引，不等于 L0/10） | 0…688 |
| `CS3` | **= `L0`**（全部 73,273 条记录均验证 `CS3 == L0`） | |
| `CF1` | 字体颜色（`!font_Color`） | 仅 `16777215` |
| `CF2` | 字体大小（`!font_FontSize`） | 仅 `22` |
| `CF3` | 注音（ruby）大小（`!font_RubySize`） | 仅 `10` |
| `CP1` | 面板模式（`!panel_Mode`） | 0、1、2、3 |
| `CP2` | 面板文件名（`!panel_Filename`） | 空 |
| `CT1` | 舞台 FG 图像（`!stage_FGFilename`） | `vista_window.png`、`st_kei11b01-.png`、… |
| `CT2` | 舞台 BG 图像（`!stage_BGFilename`）——背景与事件 CG | 2003 个不同值：`bgc_*`、`bg_up_*`、`ev01_*`、`ev02_*` … |
| `CT3`、`CT4`、`CT5` | 舞台/滚动数值状态（位置/偏移） | `0`、`400`、`600`、`781`、`1200`、`10`、… |
| `CT6`、`CT7` | 通用立绘/场景数值状态 | `CT6=0` 恒定；`CT7` 为 0…12 |
| `Frm` | 边框图像（`!frameFilename`） | 空、`finder.png` |
| `FrP` | 边框参数（`!frameParam`） | `-1`、`2570`、`2337`、… |
| `CO0`、`CO1`、`CO2` | 覆盖层图像（`!overlay_{BG,FG,ST}Filename`） | 空、`or_*` |
| `Cs0`、`Cs1`、`Cs2` | 屏幕图像（`!screen_{BG,FG,ST}Filename`） | 空、`sc_*` |
| `bgH`、`bgS`、`bgV` | 背景 HSV 调整 | 仅 `0` |
| `stH`、`stS`、`stV` | 立绘/角色 HSV 调整 | `0`、`5`、`-5`、`-25` |
| `CB1` | BGM 文件（`!bgm_Filename`） | `efBGM019.ogg`、… |
| `CB1v` | BGM 音量 | 仅 `100` |
| `CE1`、`CE2` | SE1/SE2 文件（`!se1_Filename`、`!se2_Filename`） | `AT_umi.ogg`、… |
| `Ce1` | 特效方法（`!eff_Method`） | `end`、`fadeout`、`WScroll2`、`Snow`、`SnowH`、`Cutin` |
| `Ce2` | 特效图像（`!eff_Filename`） | 空、`ev02_018.png` … |
| `Ce3`、`Ce4`、`Ce5` | 特效数量/速度/参数（顺序依据 exe 变量表推断） | `-1`、`60`、`1000`、`-2`、`500`、… |
| `Ct1` | 转场方法（`!tr_Method`） | 0…17 |
| `Ct2` | 转场规则图像（`!tr_RuleFilename`） | `fadeSide.png`、`fadeGray.png`、… |
| `Ct3` | 转场参数（`!tr_Param`） | 20、30、40、80、100、400 |

可选键（仅当一条消息跨多行文本、`LC > 0` 时出现）：
`L1_<i>`、`L2_<i>`、`L3_<i>`、`L4_<i>`（i = 0 … LC−1，每行一组），以及立绘数组
`CT6_<i>`、`CT7_<i>`、`CT7Y_<i>`（i = 0,1；角色图像 / x 位置 / y 偏移；
例如 `CT6_0=st_kei09g03.png, CT7_0=550, CT7Y_0=0`）。`L4_i` 是数值，尚未完全理解
（i=0 时恒为 `12`；后续子行为 140、172、204、236、268、300、364——可能是文本排版坐标）。
多行记录的键会替换基础的 `L1/L2/L3` 三元组。

顺序：记录按游玩顺序排列。脚本成段出现；把 `ef0015.sav`（57 个脚本、13,546 条记录）中
脚本的首次出现按顺序连起来，就得到游玩顺序：

```
000_01mugai → 100_01 → 101_01 → 102_01 → 102_02 → 102_04 → 103_01 → 104_01 → 105_01 → 106_01
→ 107_01 → 107_02 → 107_04 → 107_05 → 108_01 → 108_02 → 109_01 → 110_01 → 111_01 → 111_02
→ 111_04 → 111_05 → 111_06 → 112_01 → 113_01 → 113_02 → 113_03 → 114_01 → 115_01 → 115_02
→ 116_01 → 117_01 → 700_01mugai → 200_01 → … → 212_01 → 212_02 → 212_04 → 213_01 → 214_01
→ 215_01 → 215_02 → 215_03 → 216_01 → 216_02 → 216_04 → 216_05
```

### 2.5 编码与转义

* 脚本/对白文本值（`L2`、`L3`、人名、部分 `CT*` 文件名）是 **CP936（GBK）** 字节，
  即由中文汉化补丁产生；ASCII 部分仍为 1 字节。容器本身不声明编码——它是原始字节流，
  因此请按 GBK 解码这些值。
* `L3` 会把脚本的内联控制转义保留为字面的两字符序列：
  在整个存档集合中观察到反斜杠后的 `\n`（788 次）、`\w`（216 次）、`\a`（172 次）、`\v`（18 次）。
  `\n` 是换行；`\a`、`\v`、`\w` 是脚本的文本/语音/等待标记（本文未解码其语义）。
* 其他区段内容（键、数字、文件名）是 ASCII。

### 2.6 槽位表（已解码）

`entries` = 该文件中的 backlog 记录数。`script/ID/ptr` = 当前位置（`!script_*` 变量）。
`payload` = 解压后大小。

| 文件 | 大小（B） | 备注 | zlib@ | payload（B） | backlog 记录 | 当前脚本 | 行 ID | ptr | 已读脚本数 | BGM | mtime |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ef0000.sav | 4292 | `Auto save` | 0x1E | 38,913 | **100** | 216_05.sc | 230 | 22 | 2 | efBGM019 | 2025-11-10 13:58:25 |
| ef0010.sav | 2460 | `t` | 0x16 | 19,306 | 52 | 000_01mugai.sc | 580 | 193 | 1 | – | 2025-11-04 16:09:41 |
| ef0011.sav | 63052 | `2` | 0x16 | 639,979 | 1681 | 102_01.sc | 1400 | 216 | 4 | – | 2025-11-04 16:36:54 |
| ef0012.sav | 138145 | `4` | 0x16 | 1,425,670 | 3680 | 107_04.sc | 770 | 116 | 13 | efBGM022 | 2025-11-04 16:41:08 |
| ef0013.sav | 192572 | `5` | 0x16 | 2,000,431 | 5211 | 111_01.sc | 2190 | 339 | 19 | – | 2025-11-04 16:49:12 |
| ef0014.sav | 218289 | `11` | 0x17 | 2,298,087 | 6015 | 112_01.sc | 1990 | 285 | 24 | efBGM023 | 2025-11-04 17:45:29 |
| ef0015.sav | 502884 | `3` | 0x16 | 5,173,258 | **13546** | 216_05.sc | 1150 | 184 | 57 | efBGM019 | 2025-11-10 13:58:45 |
| ef0016.sav | 222688 | `13` | 0x17 | 2,335,753 | 6147 | 113_02.sc | 610 | 87 | 26 | efBGM019 | 2025-11-04 17:50:40 |
| ef0017.sav | 408295 | `12` | 0x17 | 4,225,516 | 10993 | 212_01.sc | 3870 | 688 | 46 | efBGM054 | 2025-11-05 16:58:55 |
| ef0018.sav | 479004 | `13` | 0x17 | 4,938,340 | 12871 | 216_01.sc | 990 | 152 | 54 | efBGM054 | 2025-11-05 17:08:17 |
| ef0019.sav | 482693 | `14` | 0x17 | 4,962,987 | 12977 | 216_04.sc | 790 | 111 | 56 | efBGM054 | 2025-11-06 15:09:14 |

观察：
* `ef0000.sav` 是**自动存档**：其备注为 `"Auto save"`，并且 backlog 恰好只保留
  **100 条记录**——与 `sysparams.txt :: autoSaveBacklogLimit=100` 吻合。它也是唯一一个
  当前位置*超前于*最后一条 backlog 记录的存档（`ID=230` 对最后 `L0=220`），即它是在
  场景中途写入的（自动存档每 `autoSaveCycle=200` 行文本触发一次）。
* 手动存档保留**当前周目的完整 backlog**（最多 13,546 条 / 5.1 MB）；
  `autoSaveBacklogLimit` 对它们不生效。
* 备注字符串为 1–2 个字符（`t`、`2`、`4`、`5`、`11`、`3`、`13`、`12`、`13`、`14`）——
  是玩家输入的存档备注（`sample comment.` 是 exe 中的占位符），不是章节号
  （它们与游玩进度不单调对应）。`"Auto save"`/`"Quick save"` 是引擎默认值。

---

## 3. `.tbr` — 每脚本的已读文本位图

> 移植实现见 §13.3。

### 3.1 容器结构

全部 61 个文件均为：0x00 处 `u32 tag = 0`，随后 0x04 处开始 **zlib 流**（`78 9c`），
解压后恰好 **8192 字节**（65,536 位），并在 EOF 结束。文件大小 45–98 字节；没有其他字段。

```
100_01.tbr (61 bytes)
00000000: 0000 0000 789c edcf b111 0010 1444 c13f  ....x........D.?
00000010: 22a1 1268 4565 4a17 aa00 c3ec 062f bdb9  "..hEeJ....../..
00000020: 8825 a718 f587 f4cd 1be5 f641 6947 8602  .%.........AiG..
00000030: 0000 0000 0000 e019 13e3 6468 9c         ..........dh.
```

### 3.2 位图语义

* 位序 = **MSB 优先**（Windows/MFC 1-bpp 约定）：
  `bit_index = byte_index*8 + k`，掩码 `0x80 >> k`。
* `bit_index` 是**文本行 ID**——与存档 backlog 中保存的 `L0` 是同一个值。
  行 ID 通常是 `100, 110, 120, …`（编译器给每行预留 10 个 ID），但分支/特殊行会得到
  不规律的 ID（例如 `101`、`231`、`455`、`1705`、`5575`），所以大多数 `.tbr` 文件都含有
  少量位于非 10 的倍数位置上的位。
* 位置 1 = 该文本行**已读**（跨周目持久保存，用于“跳过已读文本”功能——
  `sysparams.txt` 中的 `messageSpeedTBR` 就是跳过已读文本的速度）。

### 3.3 验证（观察）

对最完整存档 backlog 中出现的每一个脚本（共 57 个），`.tbr` 的位集合与该脚本在
`ef0015.sav` 中的 `L0` 值集合**完全相等**（61 个中有 56 个精确相等；细节见下表）。
合计：61 个 `.tbr`、14,668 个置位、698 个不同的行 ID；所有存档 backlog 的并集 = 13,546 条记录。

| 结果 | 数量 | 说明 |
|---|---|---|
| `.tbr` 位 == backlog 的 `L0` 集合 | 56 / 61 | 对该格式的独立交叉验证 |
| `216_05.sc` | 513 位 vs 106 | `ef0015.sav` 是在读 216_05 途中保存的（当时只读了 106 行）；`.tbr` 之后仍在继续更新 |
| `112_02`、`216_06`、`217_01`、`218_01` | 117 / 76 / 274 / 248 位 vs 0 | 这些场景是在更早的周目读的，但那次周目的存档没有保留下来；`.tbr` 是累积的，存档是每周目独立的 |

这同时解释了 `100_01.tbr` 中 `1705/2475/5575` 这类“游离”位：它们是只存在于某条分支上的
行的真实 `L0` 值。

### 3.4 脚本映射 / 章节结构

61 个 `.tbr` = 61 个游玩过的脚本文件（`.tbr` 文件名 = 脚本文件名去掉 `.sc`）。
观察到的脚本（在游戏/backlog 中均带 `.sc` 后缀）：

* `000_01mugai` — 序章（说话人：优子 / 夕）
* `100_01, 101_01, 102_01, 102_02, 102_04, 103_01, 104_01, 105_01, 106_01, 107_01, 107_02,
  107_04, 107_05, 108_01, 108_02, 109_01, 110_01, 111_01, 111_02, 111_04, 111_05, 111_06,
  112_01, 113_01, 113_02, 113_03, 114_01, 115_01, 115_02, 116_01, 117_01` — **第 1 章**
  （说话人：纮 = Hiro、宫子 = Miyako、瑞希 = Mizuki、景 = Kei …）
* `700_01mugai` — 两章之间的短幕间（说话人：优子 / 夕）
* `200_01 … 211_01, 212_01, 212_02, 212_04, 213_01, 214_01, 215_01, 215_02, 215_03, 216_01,
  216_02, 216_04, 216_05` — **第 2 章**（说话人：京介 = Kyousuke、景 = Kei、优子 = Yuuko …）
* 已游玩集合中的编号空缺（`102_03`、`107_03`、`111_03`、`112_03?`、`212_03`、`216_03`、…）
  是**录制周目中未走到的分支场景**（攻略文件 `中文攻略.txt` 证实存在多选项/BAD END）。
  章节标签 Prologue/Chap1/Chap2 也出现在 exe 字符串中，用于“Memories（回忆）”菜单。

`_mugai`（`無害`，"harmless"）后缀 = 序章/幕间的全年龄版本；r18 补丁会提供
H 场景脚本（见 §9）——因此会有这种命名。

每文件数据（类似 README 的表）：`samples/07_tbr_script_map.tsv`
（`tbr_file, script, set_bits, min_L0, max_L0, ef0015_backlog_L0, matches_ef0015`）。

已排除的假设：`.tbr` **不是**缩略图/DDS/PNG（把 8192 字节缓冲区按 1-bpp
256×256 渲染出来只是稀疏点阵，见 `samples/06_…REJECTED_view.png`），也**不是** backlog 文本；
它是纯粹的“每行 1 位”的已读登记表（上文的精确 backlog 匹配已证明）。

---

## 4. `ef00XX.png` — 存档缩略图

* 合法的 PNG：签名 + `IHDR` + 1–3 个 `IDAT` + `IEND`，无调色板，无文本/时间块。
* `IHDR`：宽 **100**、高 **75**、位深 **8**、颜色类型 **6（RGBA）**、无隔行。
* 解压后的原始图像数据 = 75 ×（1×100×4 + 1 个 filter 字节）= 30,075 字节；IDAT 按
  8192 字节边界切分（libpng `png_set_write_fn`/写缓冲的典型行为）。
* 内容 = 保存时场景的小截图（不是带文字/日期的 UI 叠加图）。
* 按文件名一一对应：`ef0000.png ↔ ef0000.sav`、`ef0010.png ↔ ef0010.sav`、…；两者的修改时间
  相差不超过约 20 ms（一起写入）。
* 这些就是读档/存档界面显示的缩略图（exe 中的 `saveloadBase*.png` 等）。

---

## 5. `cgv.dat` — CG 浏览 / 图鉴注册表

> 移植实现见 §13.4。

容器：0x04 处 `u32 tag = 0` + **zlib** → 39,550 字节 = **2236 行**，每行一个图像文件名
（以 `\n` 分隔，末尾空行不计入），**按字母序排序且无重复**。

```
00000000: 0000 0000 789c 859d d972 1cbb 8e45 dffb  ....x....r...E..
00000010: 633a 0a9c f935 8aca 41c3 b165 392c 0f7d  c:...5..A..e9,.}
…
ago_base_20001a.png
ago_base_20001b.png
…
ev01_… / ev02_… / evh01_… / evh02_… / ed01_… / ed02_… / bgw_… / bgs_… / bgc_… / bg_…
kei_… / ago_… / prologue_… / terop_… / black.png / white.png / movie.png / music.png /
planner.png / produce.png / sound.png / wazatogazouarimasen.png / vista_white.png
```

* 格式确切地就是“排序后的字符串集合”，zlib 压缩（与 `.sav`/`.tbr` 同一容器家族，
  但它是全局文件，不是每槽位一份）。
* 语义推断：**“CG view” = 已经向玩家显示过的图像集合**（图鉴注册表）。支撑证据：
  * exe 字符串中有 `NOADDCGV`（脚本命令“不要把当前图像加入 CG view”）和
    `cgv.dat`，说明图像是在*显示时*被加入的；
  * 文件是一个集合（没有标志字节），所以“存在/不存在”就编码了解锁状态；
  * exe 中还硬编码了一份 **1885 个事件 CG 文件名的目录**，其中 459 个带
    `*` 前缀（exe 文件偏移 1542136 … 1574729，紧跟在 Memories 菜单标签
    Prologue/Chap1/Chap2/Movie/Music/Event 之后）；
    1885 个中有 1682 个已出现在 `cgv.dat` 中，203 个尚未登记——这与“接近通关”的状态一致
    （本批存档读了游戏 14,668 行文本）。
  * `cgv.dat` 中有 554 个条目不在该事件目录里：它们是背景（`bgw_`、`bgs_`、
    `bgc_`、`bg_`）、制作人员/职员图像和系统图像（`black.png`、`white.png` …），即
    引擎也会登记屏幕上显示过的背景。
  * **未知**：这些行到底是*已解锁*集合，还是目录与解锁状态的合并结果；文件是排序的，
    这符合退出时序列化 `std::set` 风格注册表的做法。exe 目录中 `*` 前缀的确切含义
    尚未证明（在同时出现在 `cgv.dat` 中的 459 个里，有 426 个带 `*`）。

完整解码列表：`samples/09_cgv.dat_entries_all.txt`。

---

## 6. `sysparams.txt` — 引擎配置

> 移植字段对照表见 §14.2；移植实现见 §13.5。

* 纯文本，**CRLF**，`key<TAB>value` 行，末尾有 CRLF；**51 个键**。除两个字体名之外的值都是
  ASCII，两个字体名是 **CP936/GBK**（`fontname = 汉仪文黑-85W`、`symbolFontname = 黑体`）。
* 其中 44 个键是引擎的配置表（exe 中以有序字符串块的形式包含同名键，从 `screenEffect`
  开始、到 `modeNNL` 结束）；另有 7 个 3 字母键（`etc`、`hir`、`kei`、`kyo`、`mid`、`miy`、`yuk`）
  不在该表中，但确实以 3 字节字符串的形式存在于 exe 中（靠近 "Sound test"/`AppendPlaying`/
  `evh01_003.png` 代码以及 `configBase.png`/`BGMtest.wav`/`VOICEtest.wav` 资源）。它们的
  确切含义**未知**；看起来像是按角色/额外解锁标志（6 个角色标签：`kei`、`kyo`、`miy`、`mid`、
  `yuk`、`hir`）。观测到的值除 `cls=0` 外均为 1。

| 键 | 值 | 含义 |
|---|---|---|
| animation | 1 | 动画特效 |
| autoSaveBacklogLimit | 100 | **自动存档**中保留的 backlog 条数（已确认：`ef0000.sav` 恰好 100 条） |
| autoSaveCycle | 200 | 每读取 200 行文本自动存档一次 |
| backlogVoicePlayback | 1 | 回看时播放语音 |
| bgCacheCount | 3 | 背景缓存大小 |
| bltOnce | 1 | blit 优化 |
| cls | 0 | 引擎配置键（用途未知） |
| cursorControl | 0 | 鼠标指针处理 |
| effectDrawInterval | 10 | 特效更新间隔 |
| etc / hir / kei / kyo / mid / miy / yuk | 1 | 额外的 3 字母标志（见上文） |
| fontname | 汉仪文黑-85W | 消息字体（GBK） |
| fullScreenMode | -1 | 自动 |
| joystickUsage | 1 | 启用手柄 |
| lastSavePageNumber | 0 | 读档/存档界面最后显示的页（0 基） |
| messageSpeedAutoPlay | 12 | 自动播放速度 |
| messageSpeedRead | 16 | 已读文本速度 |
| messageSpeedTBR | 7 | 配合 `.tbr` 已读标志使用的跳过/快进速度 |
| modeNNL | 1 | nnl 引擎模式标志 |
| muteBGM / muteSE / muteVoice | 0 / 0 / 0 | 静音开关 |
| playMode | 1 | 播放模式 |
| progressInBackground | 1 | 窗口非激活时继续推进 |
| quickSaveFileNumber | 1 | 快速存档槽号 |
| screenEffect | 1 | 屏幕特效 |
| screenMode | 2 | 屏幕模式 |
| scrollDrawInterval | 2 | 卷动绘制间隔 |
| setWindowPriority | 0 | 窗口优先级 |
| shadow | 1 | 文字阴影 |
| shakeDrawInterval | 3 | 震屏间隔 |
| skipShowing | 1 | 显示“跳过中”提示 |
| stCacheCount | 10 | 立绘图像缓存 |
| stopVoiceAtNext | 0 | 下一行停止语音 |
| stretchAA / stretchAcc | 1 / 1 | 抗锯齿 / 高精度屏幕拉伸 |
| symbolFontname | 黑体 | 符号/注音字体（GBK） |
| transitionDrawInterval | 5 | 转场绘制间隔 |
| volumeBGM / volumeSE / volumeVoice | 17 / 50 / 50 | 音量 0–100 |
| windowHeight / windowWidth | 634 / 808 | 窗口尺寸（像素） |
| windowMode | 0 | 窗口模式 |
| windowXpos / windowYpos | 944 / 285 | 窗口位置 |

完整解码转储：`samples/11_sysparams.decoded.txt`（原始字节副本：`10_sysparams.txt.raw_copy`）。

---

## 7. `system.dat` — 未解决

> 移植时原样复制、不要删除（§13.1）。

* 127 字节：`u32 tag = 0` + 123 字节高熵数据；不是文本。
* 已测试且失败：zlib（偏移 0–5）、gzip、bz2、lzma-alone/auto、raw deflate、单字节 XOR
  （包括搜索 zlib magic）、用候选 key 的 RC4/XOR——候选包括 `ef_first_en_AA`、exe 中的
  8 字符字符串（`102ycz1s`、`52AUwF8B`、`a7b77qgx`、`qmzj4Y9C`、`mvnsskip`）、`system.dat`、
  游戏标题。
* 不是分组密码对齐的（127 不能被 8 整除，也不是 4 字节字的整数倍），所以在显而易见的分组布局下
  使用普通 TEA/XXTEA 的可能性不大。
* 假设：exe 写的每用户“系统存档”（窗口/系统状态或图鉴配置）（`system.dat` 在 exe 中紧邻
  `test.tst` 和那组 8 字符 key 被引用）；也可能是一个小的加密/压缩数据块。**未知**——
  需要通过 exe/DLL 的代码级分析来定论。
* Hexdump：`samples/12_system.dat_hexdump.txt`。

---

## 8. 编码汇总

| 数据 | 编码 |
|---|---|
| `.sav` 容器的 magic/键/数字 | ASCII |
| `.sav` 备注、`.sav` 的 `L2`/`L3` 以及部分脚本文本 | **CP936 / GBK**（中文汉化）；英文原文部分为 ASCII |
| `L3` 内部的内联换行/控制标记 | 字面反斜杠转义 `\n`、`\w`、`\a`、`\v` |
| `.tbr` | 仅二进制位集 |
| `cgv.dat` | ASCII 文件名 |
| `sysparams.txt` | ASCII 键/数字 + `fontname`、`symbolFontname` 的 **CP936/GBK** |
| `.png` | 二进制 RGBA 图像 |

---

## 9. 补丁相关发现（中文汉化 + r18 补丁）

* **中文化**：根目录的 `ef_first_cn_AA.dll` 包含 `save_cn\`、`r18_cn` 和
  `ef_first_en2cn.dll`；它把存档根重定向到 `save_cn\`、把 r18 资源重定向到 `r18_cn\`。
  汉化本身不改变存档数据格式；只有 `.sav` 内部的文本字节（backlog）和 `sysparams.txt` 中的
  字体名是中文。
* **r18 补丁**：exe 本身包含注册表键 `SOFTWARE\minori\ef_first_en_r18patch`
  和 r18 资源目录名 `r18_cn`（`r18_cn/` 下有：`bg.paz`、`scr.paz`、`st.paz`、`sys.paz`、
  `voice.paz`）。exe 字符串池还嵌入了一张 H 场景素材表：
  `H_kei01/H_kei02/H_miya01/H_miya02`，以及脚本名 **`111_05`、`113_02`、`215_02`、`216_05`**
  （正好在 exe 目录中的 H-CG 名字之前）——推断：这 4 个脚本就是 r18 补丁的
  H 场景（Kei / Miyako），而 `_mugai` 脚本（`000_01mugai`、`700_01mugai`）是没有补丁内容时
  使用的全年龄版本。
* **补丁对存档数据的影响**：`.sav`、`.tbr`、`cgv.dat`、`sysparams.txt` 中都不存在补丁/启用标志——
  补丁状态存在于 Windows 注册表中。因此打了补丁和未打补丁的运行共用同一个存档目录。
  **注意**：`.tbr` 的位和 backlog 的 `L0` ID 是“每脚本文本行 ID”；如果 r18 补丁替换/追加了
  这些脚本并改变了行布局，那么一个版本的已读标志和 backlog 记录可能无法与另一个版本对齐
  （未验证——补丁版 `scr.paz` 是加密的，而运行游戏会修改游戏目录，这在本次任务中被禁止）。

---

## 10. 未知项 / 待解问题

1. `system.dat` 的格式与用途（见 §7）。
2. `.sav` 备注字段之后那个恒为 0 的 `u32` 的用途。
3. `sysparams.txt` 中 7 个额外键（`etc`、`hir`、`kei`、`kyo`、`mid`、`miy`、`yuk`）的确切含义。
4. 多行 backlog 记录中 `L4_<i>` 的含义。
5. `cgv.dat` 的确切语义（已解锁集合 vs. 目录），以及 exe 硬编码 CG 目录中 `*` 前缀的含义。
6. 存档槽文件在 "`ef%04d.sav`" 之外的精确编号方案：哪些编号对应快速存档
   （`ef0001..ef0009` 不存在）、多页槽位（第 2 页及以后未测试）。
7. r18 补丁脚本的 `.tbr` 行 ID 是否与全年龄脚本一致（已读标志/backlog 回看的补丁
   不兼容风险）。
8. 其余 backlog 状态字段（`CT3/CT4/CT5`、`CT6/CT7`、`Ce3/Ce4/Ce5`）的完整含义——
   名字可从 exe 变量表得知，但缩写到字段的映射部分是推断。

---

## 11. 复现 / 工具

```bash
# dump any file (auto-detects .sav / .tbr / cgv.dat / sysparams.txt / system.dat)
python3 analysis/save/save_dump.py "save_cn/minori/ef - the first tale.(AA)/ef0000.sav" --tail 2
python3 analysis/save/save_dump.py "save_cn/minori/ef - the first tale.(AA)/100_01.tbr" --bits
python3 analysis/save/save_dump.py "save_cn/minori/ef - the first tale.(AA)/cgv.dat"
python3 analysis/save/save_dump.py "save_cn/minori/ef - the first tale.(AA)/sysparams.txt"
python3 analysis/save/save_dump.py --dir "save_cn/minori/ef - the first tale.(AA)"      # all 86 files
```

分析期间用过的一行式命令：

```python
import zlib
b=open(path,'rb').read()
off=b.find(b'\x78\x9c',16)              # .sav: zlib offset (16+len(comment)+1+4)
payload=zlib.decompress(b[off:])        # .sav / .tbr / cgv.dat all use plain zlib
bitmap=zlib.decompress(b[4:])           # .tbr: fixed 8192-byte bitmap
bits=[i*8+k for i,v in enumerate(bitmap) for k in range(8) if v&(0x80>>k)]  # read line IDs
```

## 12. 样本文件（`analysis/save/samples/`）

| 文件 | 内容 |
|---|---|
| 01_ef0000.sav_header_hexdump.txt | `.sav` 头部 + payload 开头的带注释 hexdump |
| 02_ef0000.variables.txt | 解码后的 variables 区段 |
| 03_ef0000.backlog_first_entries.txt | 前几条 backlog 记录（GBK→UTF-8 解码） |
| 04_100_01.tbr_hexdump.txt / 05_…_bitmap_bits.txt | `.tbr` hexdump + 全部置位行 ID |
| 06_…REJECTED_view.png | 把 8192 字节 `.tbr` 缓冲区按 1-bpp 图像渲染（用于排除假设） |
| 07_tbr_script_map.tsv | 61 个 `.tbr` ↔ 脚本、置位数、L0 范围、backlog 匹配情况 |
| 08_cgv.dat_head_hexdump.txt / 09_cgv.dat_entries_all.txt | `cgv.dat` 头部 + 全部 2236 条目 |
| 10_sysparams.txt.raw_copy / 11_sysparams.decoded.txt | 原始与解码后的配置 |
| 12_system.dat_hexdump.txt | 未知二进制的 hexdump |
| 13_slot_table.tsv | 全部 11 个 `.sav` 解码汇总 |
| 14_readtext_validation.tsv | 每个脚本的 `.tbr` 位 vs backlog L0 集合（关键证明） |
| 15_backlog_key_dictionary.tsv | 全部 backlog 键的含义/类型/观测值（移植字段字典） |
| 16_sysparams_to_settings.tsv | `sysparams.txt` 51 键 → 移植版 settings 字段/类型/默认值 对照表 |
| 17_script_pointer_evidence.tsv | 11 个存档的 `!script_Filename/ID/Pointer` vs backlog 末条 `CS1/L0/CS2`（读档定位证据） |


## 13. 移植时需要实现什么（Linux 原生移植指南）

> 结论先行：**原始存档可以在移植版里 100% 读写**——四个格式全部是“u32 tag + zlib + 纯文本或位图”，
> 没有任何 Windows 专属加密/注册表依赖（注册表只影响 r18 补丁资源选择，见 §14）。
> 真正难的不是存档解析，而是 `!script_Pointer` 指向的是**原版 exe 内编译脚本的指令下标**，移植版若用自己重写的脚本虚拟机，
> 必须建立一个“原版指令下标 ↔ 移植版脚本位置”的映射（见 13.2）。

### 13.1 建议的移植数据布局

```
<game-data>/save/                     # Linux 下用 XDG: $XDG_DATA_HOME/ef_first_tale/
├── ef0000.sav                        # 自动存档（保持原格式 = 兼容）
├── ef0010.sav … ef0019.sav           # 第 1 页手动存档（保持原格式）
├── ef00XX.png                        # 100×75 RGBA8 缩略图（可重新生成）
├── <script>.tbr                      # 每脚本已读位图（保持原 8192B 布局 = 兼容）
├── cgv.dat                           # 全局 CG 注册表（保持原格式 = 兼容）
├── sysparams.txt                     # 设置（保持原格式 = 兼容；另存一份新版 settings）
└── system.dat                        # 格式未知；原样透传/忽略（见 §7）
```
最小实现：**只做 `.sav` + `.tbr` + `cgv.dat` + `sysparams.txt` 四个读写器**，PNG 用任意 100×75 RGBA 编码器即可；
`system.dat` 无法解析，移植版不要删除它，原字节复制即可（丢了没有已知副作用，但保留最安全）。

### 13.2 `.sav`：精确恢复存档位置（最重要）

需要写一个 **原版脚本虚拟机（或等价翻译层）**，因为 `.sav` 的定位是 `script_Filename + script_ID(L0) + script_Pointer`：

* `!script_Filename`：定位脚本资源 `scr.paz` 内的 `NNN_NN.sc`；
* `!script_ID`：文本行 ID（`L0`，`.tbr` 位图下标），每行通常 +10，分支/特殊行不规律（`101,231,455,1705,5575…`）；
  这是**跨版本最稳定的语义锚点**；
* `!script_Pointer`：原版编译脚本的指令下标，`CS2` 同义；文本行之间会跳号（非文本指令），所以它≠行号。
  若移植版用原版脚本字节码，直接跳 `Pointer` 即可；若移植版重新编译/翻译脚本（很可能，因为要重新排版中文），
  必须记录原版 `Pointer → 新脚本位置` 的映射，或者**按 `script_ID(L0)` 在新脚本的行表里查找**（推荐主用 L0，Pointer 作校验）。
* 证据表：`samples/17_script_pointer_evidence.tsv`；
* 恢复顺序（与原版行为一致的最小实现）：
  1. 解析变量区 → 设置 `!` 引擎变量（当前脚本、场景图 `!stage_*`、BGM `!bgm_*`、字体/面板/转场/特效状态）；
  2. 恢复全局变量（无 `!` 前缀，如 `favMiyako`、`finderCounter`）——脚本逻辑直接依赖，必须原样保留；
  3. 用 `script_Filename + L0/Pointer` 跳行，重建 backlog 历史（用于“回看/跳过”），
     最后一条 backlog 的 `CT*/CF*/CP*/CO*/Cs*/CB*/CE*/Ce*/Ct*` 就是当时的完整画面/音频快照（可用来还原屏幕）；
  4. 从该行继续执行。
* 写入：手写 `u32 0` + `"nnl_ft 1.00\0"` + `comment\0` + `u32 0` + `zlib.compress(payload)` 即可；
  `comment` 是玩家备注（默认 `"Auto save"` / `"Quick save"`）；payload 语法见 §2.2–2.5。
  **注意**：写回时文本必须继续用 GBK 编码，且保留 `\n/\a/\v/\w` 这类反斜杠转义（GBK 双字节的尾字节可能是 `0x5C = '\\'`，
  所以解析/生成必须**按字节**而不是按 Unicode 字符处理，否则会破坏中文字节）。
* 自动存档 vs 手动存档（移植版必须复刻的行为）：
  * `ef0000.sav` 自动存档：backlog 只保留最后 **autoSaveBacklogLimit=100** 条（实测恰好 100 条），每 **autoSaveCycle=200** 行文本自动写一次；
  * 手动存档：保留**当前周目的完整 backlog**（最大实测 13,546 条 / 5.1 MB）。写盘时不要截断手动存档。
* 寻址规则：`ef%04d.sav`；`ef0000` = 自动存档，`ef0010..ef0019` = 第 1 存档页 10 个格子（推断：index = page*10 + slot + 10）；
  快速存档 `ef0001..ef0009` 本次快照中不存在（`quickSaveFileNumber=1`）。

### 13.3 `.tbr`：已读文本 / “跳过已读”

* 读：`u32 tag` + `zlib.decompress` → 8192 B；bit 下标 = `L0`，MSB-first（`mask = 0x80>>(i%8)`）；
* 写：同上，`zlib.compress(8192B)`；文件名为 `<script>.tbr`（脚本名去掉 `.sc`）；
* 语义：**跨周目累积**的“该行已读”集合，用于跳过/快进已读文本（`messageSpeedTBR` 是跳过速度）。
* 移植建议：内部用 `set<lineID>` 或同尺寸 bitset，落盘时照原格式写，保持与原版存档互通；
  若不想一脚本一文件，可合并成一个索引文件，但**导出/导入原格式**是兼容关键。
* 可推导性：单个 `.sav` 的 backlog 只含**当前周目**已读行，所以只能推出 `.tbr` 的**子集**；
  多个存档取并集可近似还原；完整 `.tbr` 只能来自历史 `.tbr` 文件本身（或移植版自己的已读记录）。

### 13.4 `cgv.dat`：CG 图鉴解锁

* 读/写：`u32 tag=0` + `zlib` → 按行 split，排序去重后写回（原文件就是字母序、无重复、2236 条）；
* 语义（推断）：显示过的 CG/背景/系统图被“加入 CG view”，除非脚本执行 `NOADDCGV`；即**图鉴解锁集合**；
* 移植建议：维护 `set<std::string> seen_images`；每当显示 CG/背景（除 `NOADDCGV`）就 insert，退出/存档时按原格式落盘；
* 可推导性：**不能**从 `.sav` 推导（`.sav` 没有 CG 位图/解锁位）；只能来自旧 `cgv.dat` 或移植版自己的显示日志。
  引擎侧另有 1885 条事件 CG 目录硬编码在 exe 中（§5），可用于“图鉴列表过滤”，但解锁状态必须读 `cgv.dat`。

### 13.5 `sysparams.txt`：设置

* 读/写：纯文本 CRLF、`key<TAB>value`；**写回时保持 CRLF + TAB + 字体名 GBK**，未知键原样保留（不要丢 `etc/hir/kei/kyo/mid/miy/yuk`）；
* 具体字段 → 移植版 settings 对照见 §14；
* 可推导性：音量/文字速度/窗口位置等**不可**从游戏数据推导，必须持久化；首次启动可用观测默认值
  （`volumeBGM=17, volumeSE=50, volumeVoice=50`，`messageSpeedTBR=7, Read=16, AutoPlay=12`，窗口 808×634 @ (944,285)）或合理默认；
  `autoSaveCycle=200`、`autoSaveBacklogLimit=100` 是行为参数，建议同样可配置。

### 13.6 `ef00XX.png`：缩略图

* 标准 PNG：100×75、bit depth 8、colour type 6（**RGBA8**，非隔行，filter 0–4 标准），无 tEXt/tIME/PLTE；
  raw = 75×(1+100×4)=30,075 B；IDAT 被按 8192 B 分块（libpng 默认写缓冲的痕迹）。
* 移植实现：`stb_image_write`/libpng 直接写 100×75 RGBA；内容 = 保存时画面截图（不含 UI 文字/日期）。
  文件名与 `.sav` 同名；即使不生成缩略图，`.sav` 仍可正常读取（PNG 缺失时存档列表显示 notsaved 占位即可）。

### 13.7 可从游戏数据推导 / 不可推导的字段清单

| 数据 | 能否从现有文件推导 | 说明 |
|---|---|---|
| 当前存档位置（脚本名/L0/指令下标） | ✅ 从 `.sav` 变量区 | `!script_Filename / !script_ID / !script_Pointer` |
| 场景/音频/字体状态（读档瞬间画面） | ✅ 从 `.sav` 变量区 + backlog 末条 | `!stage_*`、`!bgm_*`、backlog 的 `CT*/CF*/…` |
| 周目内已读文本历史（backlog） | ✅ 从手动 `.sav` | 自动存档只有最后 100 条 |
| 跨周目“已读”标记（跳过用） | ⚠️ 部分 | `.tbr` 是权威来源；单存档只能给子集，多存档并集近似 |
| CG 图鉴解锁 | ❌ | 只有 `cgv.dat`；`.sav` 不含 |
| 游戏进度 flag（好感度/计数器等） | ✅ 从 `.sav` 全局变量 | 以脚本变量名/值原样存储，语义由脚本决定（如 `favMiyako`、`finderCounter`） |
| 音量、文字速度、窗口位置、字体、消音 | ❌ | 只有 `sysparams.txt` |
| 自动存档周期/backlog 上限、快速存档号、上次存档页 | ❌ | `sysparams.txt` 配置（200/100/1/0） |
| 存档页大小 / 页数 | ⚠️ 部分 | 页大小=10 从文件名/UI 数组可见；总页数未知（exe 有 `Page %3d/%3d` 字符串，页数由槽位表决定） |
| r18 补丁是否启用 | ❌ | 只在 Windows 注册表（§14），存档/配置文件中没有该标志 |

### 13.8 `.sav` 最小读写伪代码

```python
# ---- read ----
b = open(sav, 'rb').read()
assert b[:4] == b'\0\0\0\0' and b[4:16] == b'nnl_ft 1.00\0'
nul = b.index(b'\0', 16)
comment = b[16:nul]                       # ASCII/GBK 玩家备注
assert b[nul+1:nul+5] == b'\0\0\0\0'      # 保留 u32 = 0
payload = zlib.decompress(b[nul+5:])      # 整个文件剩余部分就是一个 zlib 流
vars_, backlog = parse_text_sections(payload)   # 见 §2.2

# ---- write ----
payload = build_text_sections(vars_, backlog)   # 键序、TAB、\n、GBK、'ZZ' 结尾
blob = b'\0\0\0\0' + b'nnl_ft 1.00\0' + comment + b'\0' + b'\0\0\0\0' + zlib.compress(payload, 9)
open(sav, 'wb').write(blob)
```

---

## 14. Windows 配置项 → 移植后 settings 字段对照表

### 14.1 注册表 / Windows 专属项（exe 字符串中可见）

| Windows 项（exe 字符串） | 出处（exe 偏移） | 作用 | Linux 移植对应 |
|---|---|---|---|
| `SOFTWARE\minori\ef_first_en_r18patch` | 1538036 | r18 补丁检测/开关（决定是否加载 `r18_cn/` 资源与 H 场景脚本） | **settings 字段** `r18_patch_enabled: bool`（或按 `r18_cn/` 资源是否存在自动判定）；存档格式本身无此标志 |
| `InstallDir` | 1538024 | 安装路径（注册表值） | 不需要；用可执行文件/数据目录搜索或 XDG 配置 |
| `Global\`（CreateMutex 命名空间） | — | 单实例互斥 | 用文件锁/DBus 单实例 |
| `TaskbarCreated` | 1577030 附近 | 托盘图标重建消息 | 不需要（无托盘） |
| `minori ef_first_en for Windows` | 1538060 附近 | 窗口类/注册表显示名 | 不需要 |

### 14.2 `sysparams.txt` 键 → 移植版 settings（建议字段名/类型/默认值）

> 同内容机器可读版：`samples/16_sysparams_to_settings.tsv`；backlog 字段字典：`samples/15_backlog_key_dictionary.tsv`。

| sysparams 键 | 观测值 | 类型 | 建议 settings 字段 | 说明 |
|---|---|---|---|---|
| animation | 1 | bool | `gfx.animation` | 动画特效开关 |
| autoSaveBacklogLimit | 100 | int | `save.autosave_backlog_limit` | 自动存档保留的 backlog 条数（实测 ef0000=100 条） |
| autoSaveCycle | 200 | int | `save.autosave_cycle_lines` | 每 200 行文本自动存档 |
| backlogVoicePlayback | 1 | bool | `audio.backlog_play_voice` | 回看时播放语音 |
| bgCacheCount | 3 | int | `gfx.bg_cache_count` | 背景缓存数 |
| bltOnce | 1 | bool | `gfx.blt_once` | blit 优化（可忽略） |
| cls | 0 | int | `engine.cls`（保留） | 引擎键，语义未知，原样保留 |
| cursorControl | 0 | bool | `input.cursor_control` | 鼠标指针控制 |
| effectDrawInterval | 10 | int | `gfx.effect_draw_interval` | 特效更新间隔 |
| fontname | 汉仪文黑-85W（GBK） | string | `text.font_name` | 消息字体；Linux 需映射到可用 CJK 字体（如 Noto Sans CJK），保留原字符串便于回写 |
| symbolFontname | 黑体（GBK） | string | `text.symbol_font_name` | 注音/符号字体 |
| fullScreenMode | -1 | int | `display.fullscreen_mode` | -1=自动（枚举未知） |
| joystickUsage | 1 | bool | `input.joystick` | 手柄 |
| lastSavePageNumber | 0 | int | `ui.last_save_page` | 存档界面上次页码（0 基） |
| messageSpeedAutoPlay | 12 | int | `text.speed_autoplay` | 自动播放速度 |
| messageSpeedRead | 16 | int | `text.speed_read` | 已读文本速度 |
| messageSpeedTBR | 7 | int | `text.speed_skip_read` | 配合 `.tbr` 的跳过已读速度 |
| modeNNL | 1 | int | `engine.mode_nnl`（保留） | nnl 引擎模式 |
| muteBGM / muteSE / muteVoice | 0 / 0 / 0 | bool | `audio.mute_bgm` / `mute_se` / `mute_voice` | 静音开关 |
| playMode | 1 | int | `engine.play_mode` | 播放模式（语义未知，保留） |
| progressInBackground | 1 | bool | `engine.progress_in_background` | 窗口失焦继续推进 |
| quickSaveFileNumber | 1 | int | `ui.quick_save_slot` | 快速存档槽号 |
| screenEffect | 1 | bool | `gfx.screen_effects` | 屏幕特效 |
| screenMode | 2 | int | `display.screen_mode` | 屏幕模式 |
| scrollDrawInterval | 2 | int | `gfx.scroll_draw_interval` | 卷动间隔 |
| setWindowPriority | 0 | int | `display.window_priority` | 窗口优先级 |
| shadow | 1 | bool | `text.shadow` | 文字阴影 |
| shakeDrawInterval | 3 | int | `gfx.shake_draw_interval` | 震屏间隔 |
| skipShowing | 1 | bool | `ui.skip_indicator` | 显示“跳过中”提示 |
| stCacheCount | 10 | int | `gfx.stand_cache_count` | 立绘缓存数 |
| stopVoiceAtNext | 0 | bool | `audio.stop_voice_at_next` | 下一行停语音 |
| stretchAA | 1 | bool | `gfx.stretch_aa` | 抗锯齿拉伸 |
| stretchAcc | 1 | bool | `gfx.stretch_acc` | 高精度拉伸 |
| transitionDrawInterval | 5 | int | `gfx.transition_draw_interval` | 转场间隔 |
| volumeBGM / volumeSE / volumeVoice | 17 / 50 / 50 | int 0–100 | `audio.volume_bgm` / `volume_se` / `volume_voice` | 音量；BGM=17 是玩家/测试设置，不是默认值 |
| windowHeight / windowWidth | 634 / 808 | int | `display.window_height/width` | 窗口尺寸（像素） |
| windowMode | 0 | int | `display.window_mode` | 窗口模式 |
| windowXpos / windowYpos | 944 / 285 | int | `display.window_xpos/ypos` | 窗口位置 |
| etc / hir / kei / kyo / mid / miy / yuk | 均 1 | int/bool | `settings.extra_flags`（字典原样保留） | exe 字符串池里的 3 字母标志（疑似按角色/Sound-test/AppendPlaying 解锁位），语义未知；**必须原样 round-trip** |

### 14.3 兼容策略建议

1. **优先原格式**：存档、`.tbr`、`cgv.dat` 按原字节格式读写 → 玩家可以把 Windows 版存档目录直接拷进 Linux 移植版继续玩，反之亦然；
2. `sysparams.txt` 可原格式读写以兼容，但内部请用独立 settings 结构（字段见 14.2），未知键保存在 `extra` 字典里；
3. 新增的移植版专属设置（分辨率缩放、渲染后端、语言、字体文件路径、r18 资源路径…）**不要**写进 `sysparams.txt`，
   放进自己的 `settings.toml`/`settings.json`；
4. `system.dat` 原样保留不解析（§7），`ef00XX.png` 可重新生成；
5. 读档时以 `!script_ID(L0)` 为主锚点、`!script_Pointer` 为校验，可最大限度兼容原版存档与重新编译的脚本；
6. 文本一律按 **GBK 字节** 处理/落盘；移植版内部若用 UTF-8，需要在读写边界做转换，并注意 GBK 尾字节 `0x5C`（`\`）与转义序列的冲突。

（本文为 analysis/save/SAVE_FORMAT.md 的完整中文译本）
