> **英文原稿（备份，供对照）** — 正式中文译本见同目录下同名文件（去掉 `.en`）。

# ef - the first tale — save-data / config format analysis

Target: `/home/lrxin/Downloads/ef - the first tale/save_cn/minori/ef - the first tale.(AA)/`
Game: **ef - the first tale**, minori engine ("nnl" / *nnl_ft*), build `ef_first_en_AA.exe`
(English MangaGamer release) + Chinese fan translation (`ef_first_cn_AA.dll`), r18 patch assets in `r18_cn/`.

Analysis constraints: game directory treated read-only; only `bash / python3 / xxd / strings` used;
**no REA MCP tools, no disassembly, no game execution**. All artifacts written under `analysis/save/`.
Every claim below is either **observation** (exact bytes/hashes reproducible) or **inference** (marked).

---

## 0. TL;DR

| Artifact | Real format | Status |
|---|---|---|
| `ef00XX.sav` | `u32 tag=0` + `"nnl_ft 1.00\0"` + `comment\0` + `u32 0` + **plain zlib** → text payload `<begin variables>…<end variables>` + `<<begin backlog>>…<<end backlog>>` | fully decoded |
| `*.tbr` | `u32 tag=0` + **plain zlib** → fixed **8192-byte (65536-bit) read-text bitmap**; bit index = per-script text line ID `L0`; bit set = line already read | fully decoded + validated |
| `ef00XX.png` | standard PNG, 100×75, 8-bit RGBA (colour type 6), thumbnail of the scene at save time; 1:1 with the same `ef00XX.sav` | validated |
| `cgv.dat` | `u32 tag=0` + **plain zlib** → sorted, newline-delimited list of **2236 image file names** (CG/gallery registry, "CG view") | container decoded; exact unlock semantics inferred/unknown |
| `sysparams.txt` | plain text, CRLF, `key<TAB>value`, CP936/GBK values; **51 keys** of engine config | fully decoded |
| `system.dat` | `u32 tag=0` + 123 bytes high-entropy binary; **format unknown** (not zlib/gzip/bz2/lzma/raw-deflate/TEA-aligned/XOR) | unresolved |

No save/config file of this game is TEA/XXTEA/XOR encrypted. The `.paz` archives are a different
container (they start with a 16/24-byte key string such as `ef_first_en_AA\0`); **save files do not
use that scheme at all** — they are plain zlib + text and can be edited with stock zlib tools.


---

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

## 1. Directory inventory (86 files)

```
save_cn/minori/ef - the first tale.(AA)/
├── ef0000.sav, ef0010.sav … ef0019.sav      11 save files  (ef0001…ef0009 are absent)
├── *.tbr                                    61 read-text bitmap files
├── ef0000.png, ef0010.png … ef0019.png      11 save thumbnails (1 per .sav)
├── cgv.dat                                   1 gallery/CG registry
├── sysparams.txt                             1 plain-text config
└── system.dat                                1 unknown 127-byte binary
```
11 + 61 + 11 + 3 = 86 files ✔ (the task brief said "*ef0000.sav .. ef0019.sav* (20 slots?)";
**only 11 exist**: the auto-save `ef0000` and save page-1 slots `ef0010..ef0019`).

Directory redirection (observation, from `ef_first_cn_AA.dll` ASCII strings):
`save_cn\`, `r18_cn`, `ef_first_en2cn.dll`.
The English exe wants `save\` and the game folder `ef - the first tale.(AA)` (exe string offsets
1538016 `save\`, 1538076 `ef - the first tale.(AA)`); the CN DLL re-points the
save root to `save_cn\` and the r18-patch asset root to `r18_cn\`. So this directory is the CN-patch
save root, not the original English one.

Save-file naming is confirmed by the exe: format strings **`ef%04d`** + **`.sav`** inside
`ef_first_AA.exe` at file offsets 1576976 and 1576984, next to the save-list date format
`%04d/%02d/%02d %02d:%02d` at offset 1576672 and the comment placeholder `sample comment.` at 1576960.

Slot naming (inference): `ef0000` = auto save; `ef0010..ef0019` = save page 1, slots 0..9
(i.e. index = page*10 + slot + 10). Quick-save slots (`ef0001..ef0009`) are not present in this
snapshot; `sysparams.txt` has `quickSaveFileNumber=1`.

---

## 2. `.sav` — format

> 移植实现见 §13.2（读档位置恢复）与 §13.8（最小读写伪代码）。

### 2.1 Container

Offsets are absolute from file start. All 11 files follow it exactly.

| Offset | Size | Type | Value / meaning |
|---|---|---|---|
| 0x00 | 4 | u32 LE | `0x00000000` — tag / format version (0 in all files) |
| 0x04 | 12 | char[12] | magic `"nnl_ft 1.00\0"` — engine `nnl`, game tag `ft`, format `1.00` |
| 0x10 | var | char[] | **save comment/title**, NUL-terminated, ASCII or CP936 (engine default strings `"Auto save"`, `"Quick save"`; `"sample comment."` is the input-field placeholder in the exe) |
| 0x10+len+1 | 4 | u32 LE | `0x00000000` (always 0 in all 11 files — reserved/flags, purpose unknown) |
| … | var | zlib stream | start bytes `78 9c`, standard RFC-1950 zlib, default compression; stream runs to EOF (`unused_data == 0` for every file) |

Examples (hexdump):

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

Compression observations: 11/11 files decompress cleanly with `zlib.decompress`, `unused_data == 0`;
payloads are 19,306 … 5,173,258 bytes (ratio ≈ 10:1). No CRC/encryption wrapper beyond zlib's own
Adler-32. **There is no TEA/XXTEA/XOR obfuscation** (tested; the payload is plain text after zlib).

### 2.2 Payload = text file

After decompression the payload is a **text document** (LF line endings, no CR, byte-transparent
values). Two sections, in this order, both present in all observed files:

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

* Section markers are literal strings: `<begin variables>`, `<end variables>`,
  `<<begin backlog>>`, `<<end backlog>>`. (Exe strings confirm all four.)
* Separator between key and value is a **single TAB (`0x09`)**; no spaces/quoting.
* Values are raw bytes, not escaped except for script-text inline commands (see 2.5).
* `ef0000.sav` payload = 38,913 bytes; the largest (`ef0015.sav`) = 5,173,258 bytes
  (full playthrough backlog; see §2.6).

### 2.3 Variables section

Line grammar: `key<TAB>value\n`. Two kinds of keys:

1. **Engine state variables** — start with `!`. 43 distinct keys seen (`!script_Filename`,
   `!script_ID`, `!stage_BGFilename`, `!bgm_Filename`, `!font_Color`, …). Names/columns are
   confirmed by the exe string table (e.g. `!script_Filename`, `!script_Pointer`, `!script_ID`,
   `!font_Color`, `!font_FontSize`, `!font_RubySize`, `!panel_Mode`, `!panel_Filename`,
   `!stage_FGFilename`, `!stage_BGFilename`, `!stage_BGX`, `!stage_BGY`, `!stage_FGX`, `!stage_FGY`,
   `!stage_StandFilename%d`, `!stage_StandPos%d`, `!stage_StandOffs%d`, `!frameFilename`,
   `!frameParam`, `!overlay_FGFilename/BGFilename/STFilename`, `!screen_*`, `!bgH/S/V`, `!stH/S/V`,
   `!bgm_Filename/Volume`, `!se1_Filename`, `!se2_Filename`, `!eff_Method/Filename/Count/Speed/Param`,
   `!tr_Method/RuleFilename/Param`, `!angBG`, `!angFG`, `!rotBG_*`, `!rotFG_*`, `!chL_%d`).
   Only non-default/active entries are written (e.g. `!stage_FGFilename` appears only in some saves),
   so the number of variables per save varies 37–43.
2. **Game/global flag variables** — no `!` prefix, values set by the scripts. Observed in this save set:
   `favMiyako` (Miyako affection counter, values 1/2), `favMiyako_a` (1), `finderCounter`
   (45…2814; "finder" game-progress counter). These are the closest thing to a "flag block" in the
   save: **game progression flags are script variables and are stored verbatim in this section**,
   not in a packed bitset.

Selected values (from `ef0019.sav`, a chapter-2 save):

| key | value | meaning (inference from exe names + behaviour) |
|---|---|---|
| `!script_Filename` | `216_04.sc` | current script file |
| `!script_ID` | `790` | current text line ID (same numbering as `.tbr` bits) |
| `!script_Pointer` | `111` | current script pointer (instruction index) |
| `!bgm_Filename` / `!bgm_Volume` | `efBGM054.ogg` / `100` | current BGM / its volume 0–100 |
| `!stage_BGFilename` | `ev02_163a05.png` | current background/CG image |
| `!panel_Mode` | `3` | message-panel mode |
| `!tr_Method`, `!tr_Param` | `0`, `40` | screen transition state |
| `!eff_Method` | `fadeout` | effect state |
| `!font_Color` / `FontSize` / `RubySize` | `16777215` (=0xFFFFFF) / `22` / `10` | text style |

**Not in the save**: play date/time, play time, read-text bitmaps, BGM/SE/voice volume settings,
CG gallery flags, save page number. (Date/time shown in the save-list UI is formatted with
`%04d/%02d/%02d %02d:%02d` — inference: taken from the `.sav` file timestamp; `.sav` and `.png` mtimes
are identical within ~20 ms for all 11 slots, e.g. `ef0011` 2025-11-04 16:36:54.)

### 2.4 Backlog section

The backlog is **plain text history of everything read in the current playthrough**.
72,273 entries total over the 11 saves. One entry = key/value lines terminated by `ZZ`.

Fixed base keys (47, always present, always in this order):

| key | meaning (evidence) | observed values |
|---|---|---|
| `LC` | **line count** of this message (0 = single line; N = multi-line) | 0 (73,150), 1 (63), 2 (17), 3 (9), 4 (25), 5 (9) |
| `L0` | **text line ID** inside the script; also the bit index used by `.tbr` | 100, 101, 102, … 6380; mostly `100+10k`, with irregular IDs for branch/special lines (698 distinct values observed) |
| `L1` | voice file name (empty = no voice) | `kei-216_04-0251`, `miy-112_01-0095`, … |
| `L2` | speaker display name, **CP936**, may carry a control prefix (`@`, `#`) | `景` (Kei), `宫子`, `京介`, `优子`, `纮`, `瑞希`, `泉`, `大村`, … |
| `L3` | message text, **CP936** (Chinese translation) with literal inline escapes (`\n`, `\a`, `\v`, `\w`) | Chinese dialogue |
| `CS1` | script file name | `216_05.sc` |
| `CS2` | script pointer (line index, not equal to L0/10) | 0…688 |
| `CS3` | **= `L0`** (verified `CS3 == L0` for all 73,273 entries) | |
| `CF1` | font color (`!font_Color`) | `16777215` only |
| `CF2` | font size (`!font_FontSize`) | `22` only |
| `CF3` | ruby size (`!font_RubySize`) | `10` only |
| `CP1` | panel mode (`!panel_Mode`) | 0, 1, 2, 3 |
| `CP2` | panel file name (`!panel_Filename`) | empty |
| `CT1` | stage FG image (`!stage_FGFilename`) | `vista_window.png`, `st_kei11b01-.png`, … |
| `CT2` | stage BG image (`!stage_BGFilename`) — backgrounds and event CG | 2003 distinct: `bgc_*`, `bg_up_*`, `ev01_*`, `ev02_*` … |
| `CT3`,`CT4`,`CT5` | stage/scroll numeric state (positions/offsets) | `0`, `400`, `600`, `781`, `1200`, `10`, … |
| `CT6`,`CT7` | generic stand/scene numeric state | `CT6=0` always; `CT7` 0…12 |
| `Frm` | frame image (`!frameFilename`) | empty, `finder.png` |
| `FrP` | frame param (`!frameParam`) | `-1`, `2570`, `2337`, … |
| `CO0`,`CO1`,`CO2` | overlay images (`!overlay_{BG,FG,ST}Filename`) | empty, `or_*` |
| `Cs0`,`Cs1`,`Cs2` | screen images (`!screen_{BG,FG,ST}Filename`) | empty, `sc_*` |
| `bgH`,`bgS`,`bgV` | background HSV adjust | `0` only |
| `stH`,`stS`,`stV` | stand/character HSV adjust | `0`, `5`, `-5`, `-25` |
| `CB1` | BGM file (`!bgm_Filename`) | `efBGM019.ogg`, … |
| `CB1v` | BGM volume | `100` only |
| `CE1`,`CE2` | SE1/SE2 files (`!se1_Filename`, `!se2_Filename`) | `AT_umi.ogg`, … |
| `Ce1` | effect method (`!eff_Method`) | `end`, `fadeout`, `WScroll2`, `Snow`, `SnowH`, `Cutin` |
| `Ce2` | effect image (`!eff_Filename`) | empty, `ev02_018.png` … |
| `Ce3`,`Ce4`,`Ce5` | effect count/speed/param (order inferred from exe var list) | `-1`, `60`, `1000`, `-2`, `500`, … |
| `Ct1` | transition method (`!tr_Method`) | 0…17 |
| `Ct2` | transition rule image (`!tr_RuleFilename`) | `fadeSide.png`, `fadeGray.png`, … |
| `Ct3` | transition param (`!tr_Param`) | 20, 30, 40, 80, 100, 400 |

Optional keys (only when a message spans several text lines, `LC > 0`):
`L1_<i>`, `L2_<i>`, `L3_<i>`, `L4_<i>` for i = 0 … LC−1 (one group per sub-line), and the stand
arrays `CT6_<i>`, `CT7_<i>`, `CT7Y_<i>` for i = 0,1 (character image / x position / y offset;
e.g. `CT6_0=st_kei09g03.png, CT7_0=550, CT7Y_0=0`). `L4_i` is numeric and not fully understood
(always `12` for i=0; 140, 172, 204, 236, 268, 300, 364 for later sub-lines — probably a text
layout coordinate). Multi-line entry keys replace the base `L1/L2/L3` triple.

Ordering: entries are in play order. Scripts appear in runs; the concatenation of first appearances
in `ef0015.sav` (57 scripts, 13,546 entries) gives the play order:

```
000_01mugai → 100_01 → 101_01 → 102_01 → 102_02 → 102_04 → 103_01 → 104_01 → 105_01 → 106_01
→ 107_01 → 107_02 → 107_04 → 107_05 → 108_01 → 108_02 → 109_01 → 110_01 → 111_01 → 111_02
→ 111_04 → 111_05 → 111_06 → 112_01 → 113_01 → 113_02 → 113_03 → 114_01 → 115_01 → 115_02
→ 116_01 → 117_01 → 700_01mugai → 200_01 → … → 212_01 → 212_02 → 212_04 → 213_01 → 214_01
→ 215_01 → 215_02 → 215_03 → 216_01 → 216_02 → 216_04 → 216_05
```

### 2.5 Encoding & escaping

* Script/dialogue text values (`L2`, `L3`, names, some `CT*` filenames) are **CP936 (GBK)** bytes,
  i.e. produced by the Chinese fan translation; ASCII parts remain 1 byte. The container does not
  declare an encoding — it is a raw byte stream, so decode values as GBK.
* `L3` keeps the script's inline control escapes as literal two-character sequences:
  observed `\n` (788), `\w` (216), `\a` (172), `\v` (18) after backslashes over the whole save set.
  `\n` is a line break; `\a`, `\v`, `\w` are script text/voice/wait markers (not decoded here).
* Other section content (keys, numbers, filenames) is ASCII.

### 2.6 Slot map (decoded)

`entries` = backlog entries in that file. `script/ID/ptr` = current position (`!script_*` vars).
`payload` = decompressed size.

| file | size (B) | comment | zlib@ | payload (B) | backlog entries | current script | line ID | ptr | scripts read | BGM | mtime |
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

Observations:
* `ef0000.sav` is the **auto save**: its comment is `"Auto save"` and its backlog is capped at exactly
  **100 entries** — matching `sysparams.txt :: autoSaveBacklogLimit=100`. It is also the only save
  whose current position is *ahead* of its last backlog entry (`ID=230` vs last `L0=220`), i.e. it was
  written mid-scene (auto save happens every `autoSaveCycle=200` text lines).
* Manual saves keep the **full backlog of the current playthrough** (up to 13,546 entries / 5.1 MB);
  the same `autoSaveBacklogLimit` does not apply to them.
* Comment strings are one/two characters (`t`, `2`, `4`, `5`, `11`, `3`, `13`, `12`, `13`, `14`) —
  player-entered save comments (`sample comment.` is the placeholder in the exe), not chapter numbers
  (they are non-monotonic with play progress). `"Auto save"`/`"Quick save"` are engine defaults.

---

## 3. `.tbr` — per-script read-text bitmap

> 移植实现见 §13.3。

### 3.1 Container

All 61 files: `u32 tag = 0` at 0x00, then **zlib stream at 0x04** (`78 9c`), decompressing to exactly
**8192 bytes** (65,536 bits) and ending at EOF. File size 45–98 bytes; no other fields.

```
100_01.tbr (61 bytes)
00000000: 0000 0000 789c edcf b111 0010 1444 c13f  ....x........D.?
00000010: 22a1 1268 4565 4a17 aa00 c3ec 062f bdb9  "..hEeJ....../..
00000020: 8825 a718 f587 f4cd 1be5 f641 6947 8602  .%.........AiG..
00000030: 0000 0000 0000 e019 13e3 6468 9c         ..........dh.
```

### 3.2 Bitmap semantics

* Bit order = **MSB-first** (Windows/MFC 1-bpp convention):
  `bit_index = byte_index*8 + k`, mask `0x80 >> k`.
* `bit_index` is the **text line ID** — the same `L0` value stored in the save backlog.
  Line IDs are usually `100, 110, 120, …` (the compiler reserves 10 IDs per line) but branch/special
  lines get irregular IDs (e.g. `101`, `231`, `455`, `1705`, `5575`), so most `.tbr` files contain
  a handful of bits at non-multiple-of-10 positions.
* bit set = that text line was **read** (persistent across playthroughs, used by the "skip read text"
  feature — `messageSpeedTBR` in `sysparams.txt` is the skip-read text speed).

### 3.3 Validation (observation)

For every one of the 57 scripts present in the fullest save's backlog, the `.tbr` bit set equals the
set of that script's `L0` values in `ef0015.sav` **exactly** (56 of 61 exact; details below).
Total: 61 `.tbr`, 14,668 set bits, 698 distinct line IDs; union of all save backlogs = 13,546 entries.

| result | count | explanation |
|---|---|---|
| `.tbr` bits == backlog `L0` set | 56 / 61 | independent cross-validation of the format |
| `216_05.sc` | 513 bits vs 106 | `ef0015.sav` was saved while reading 216_05 (only 106 lines read yet); `.tbr` continued to be updated afterwards |
| `112_02`, `216_06`, `217_01`, `218_01` | 117 / 76 / 274 / 248 bits vs 0 | those scenes were read in an earlier playthrough whose save was not kept; `.tbr` is cumulative, saves are per-playthrough |

This also explains "stray" bits such as `1705/2475/5575` in `100_01.tbr`: they are genuine `L0`
values of lines that exist only on a branch.

### 3.4 Script map / chapter structure

61 `.tbr` = 61 played script files (`.tbr` file name = script file name minus `.sc`).
Observed scripts (all with `.sc` suffix in game/backlog):

* `000_01mugai` — prologue (speakers 优子 / 夕)
* `100_01, 101_01, 102_01, 102_02, 102_04, 103_01, 104_01, 105_01, 106_01, 107_01, 107_02,
  107_04, 107_05, 108_01, 108_02, 109_01, 110_01, 111_01, 111_02, 111_04, 111_05, 111_06,
  112_01, 113_01, 113_02, 113_03, 114_01, 115_01, 115_02, 116_01, 117_01` — **chapter 1**
  (speakers: 纮 = Hiro, 宫子 = Miyako, 瑞希 = Mizuki, 景 = Kei …)
* `700_01mugai` — short interlude between the chapters (speakers 优子 / 夕)
* `200_01 … 211_01, 212_01, 212_02, 212_04, 213_01, 214_01, 215_01, 215_02, 215_03, 216_01,
  216_02, 216_04, 216_05` — **chapter 2** (speakers: 京介 = Kyousuke, 景 = Kei, 优子 = Yuuko …)
* Numbering gaps in the played set (`102_03`, `107_03`, `111_03`, `112_03?`, `212_03`, `216_03`, …)
  are **branch scenes not visited in the recorded playthroughs** (walkthrough file `中文攻略.txt`
  confirms multiple choices/bad ends). Chapter labels Prologue/Chap1/Chap2 also appear as exe strings
  for the "Memories" menu.

`_mugai` (`無害`, "harmless") suffix = all-ages variant of prologue/interlude; the r18 patch supplies
H-scene scripts (see §9) — hence the naming.

`README`-style per-file data: `samples/07_tbr_script_map.tsv`
(`tbr_file, script, set_bits, min_L0, max_L0, ef0015_backlog_L0, matches_ef0015`).

Rejected hypotheses: `.tbr` is **not** a thumbnail/DDS/PNG (the 8192-byte buffer rendered as a 1-bpp
256×256 image is just a sparse dot band, `samples/06_…REJECTED_view.png`) and **not** backlog text;
it is a pure 1-bit-per-line read registry (proven by the exact backlog match above).

---

## 4. `ef00XX.png` — save thumbnails

* Valid PNG: signature + `IHDR` + 1–3 `IDAT` + `IEND`, no palette, no text/time chunks.
* `IHDR`: width **100**, height **75**, bit depth **8**, colour type **6 (RGBA)**, no interlace.
* Raw decompressed image data = 75 × (1×100×4 + 1 filter byte) = 30,075 bytes; IDAT is split at
  8192-byte boundaries (typical libpng `png_set_write_fn`/buffer behaviour).
* Content = small screenshot of the scene at save time (not a UI overlay with text/date).
* 1:1 mapping by name: `ef0000.png ↔ ef0000.sav`, `ef0010.png ↔ ef0010.sav`, …; mtimes of the pair
  are identical within ~20 ms (written together).
* These are the thumbnails shown in the load/save screen (`saveloadBase*.png` etc. in the exe).

---

## 5. `cgv.dat` — CG-view / gallery registry

> 移植实现见 §13.4。

Container: `u32 tag = 0` + **zlib** at 0x04 → 39,550 bytes = **2236 lines**, each line an image file
name (`\n` separated, no trailing empty line counted), **sorted alphabetically and duplicate-free**.

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

* Format is exactly "sorted set of strings", zlib-compressed (same container family as `.sav`/`.tbr`,
  but this is a global file, not per-slot).
* Semantic inference: **"CG view" = the set of images that have been displayed to the player**
  (gallery registry). Supporting evidence:
  * exe strings contain `NOADDCGV` (a script command "do not add current image to CG view") and
    `cgv.dat`, so images are *added* when shown;
  * file is a set (no flag bytes), so presence/absence encodes unlock state;
  * exe also contains a hard-coded **catalog of 1885 event-CG file names** with 459 entries prefixed
    `*` (exe file offsets 1542136 … 1574729, right after the Memories menu labels
    Prologue/Chap1/Chap2/Movie/Music/Event);
    1682 of the 1885 are present in `cgv.dat`, 203 not yet registered — consistent with a
    near-complete playthrough (this save set reads 14,668 of the game's text lines).
  * 554 entries in `cgv.dat` are not in that event catalog: they are backgrounds (`bgw_`, `bgs_`,
    `bgc_`, `bg_`), credits/staff images and system images (`black.png`, `white.png` …), i.e. the
    engine also registers backgrounds shown on screen.
  * **Unknown**: whether lines are the *unlocked* set or a catalog merged with unlock state; the file
    is sorted, which fits a `std::set`-style registry serialised on exit. `*`-prefix meaning in the
    exe catalog is not proven (it is present for 426/459 entries that are also in `cgv.dat`).

Full decoded list: `samples/09_cgv.dat_entries_all.txt`.

---

## 6. `sysparams.txt` — engine configuration

> 移植字段对照表见 §14.2；移植实现见 §13.5。

* Plain text, **CRLF**, `key<TAB>value` lines, final CRLF; **51 keys**. Values are ASCII except the
  two font names, which are **CP936/GBK** (`fontname = 汉仪文黑-85W`, `symbolFontname = 黑体`).
* 44 keys are the engine's config table (the exe contains the same key names as an ordered string
  block, starting at `screenEffect` and ending at `modeNNL`); 7 additional 3-letter keys
  (`etc`, `hir`, `kei`, `kyo`, `mid`, `miy`, `yuk`) are not in that table but do exist as 3-byte
  strings in the exe (near the "Sound test"/`AppendPlaying`/`evh01_003.png` code and
  `configBase.png`/`BGMtest.wav`/`VOICEtest.wav` assets). Their exact meaning is **unknown**;
  they look like per-character / extra unlock flags (6 character tags: `kei`, `kyo`, `miy`, `mid`,
  `yuk`, `hir`). All observed values are 1 except `cls=0`.

| key | value | meaning |
|---|---|---|
| animation | 1 | animation effects |
| autoSaveBacklogLimit | 100 | backlog entries kept in the **auto-save** file (confirmed: `ef0000.sav` has exactly 100) |
| autoSaveCycle | 200 | auto-save every 200 read text lines |
| backlogVoicePlayback | 1 | play voice when backtracking |
| bgCacheCount | 3 | background cache size |
| bltOnce | 1 | blit optimisation |
| cls | 0 | engine config key (purpose unknown) |
| cursorControl | 0 | mouse cursor handling |
| effectDrawInterval | 10 | effect update interval |
| etc / hir / kei / kyo / mid / miy / yuk | 1 | extra 3-letter flags (see above) |
| fontname | 汉仪文黑-85W | message font (GBK) |
| fullScreenMode | -1 | auto |
| joystickUsage | 1 | joystick enabled |
| lastSavePageNumber | 0 | last page shown in save/load screen (0-based) |
| messageSpeedAutoPlay | 12 | auto-play speed |
| messageSpeedRead | 16 | already-read text speed |
| messageSpeedTBR | 7 | skip/jump speed used with the `.tbr` read flags |
| modeNNL | 1 | nnl-engine mode flag |
| muteBGM / muteSE / muteVoice | 0 / 0 / 0 | mute switches |
| playMode | 1 | play mode |
| progressInBackground | 1 | keep advancing when window inactive |
| quickSaveFileNumber | 1 | quick-save slot number |
| screenEffect | 1 | screen effects |
| screenMode | 2 | screen mode |
| scrollDrawInterval | 2 | scroll draw interval |
| setWindowPriority | 0 | window priority |
| shadow | 1 | text shadow |
| shakeDrawInterval | 3 | screen-shake interval |
| skipShowing | 1 | show skip indicator |
| stCacheCount | 10 | standing-character image cache |
| stopVoiceAtNext | 0 | stop voice on next line |
| stretchAA / stretchAcc | 1 / 1 | anti-aliased / high-precision screen stretching |
| symbolFontname | 黑体 | symbol/ruby font (GBK) |
| transitionDrawInterval | 5 | transition draw interval |
| volumeBGM / volumeSE / volumeVoice | 17 / 50 / 50 | volumes 0–100 |
| windowHeight / windowWidth | 634 / 808 | window size (px) |
| windowMode | 0 | window mode |
| windowXpos / windowYpos | 944 / 285 | window position |

Full decoded dump: `samples/11_sysparams.decoded.txt` (raw byte copy: `10_sysparams.txt.raw_copy`).

---

## 7. `system.dat` — unresolved

> 移植时原样复制、不要删除（§13.1）。

* 127 bytes: `u32 tag = 0` + 123 bytes of high-entropy data; not text.
* Tested and failed: zlib (offsets 0–5), gzip, bz2, lzma-alone/auto, raw deflate, single-byte XOR
  (including search for zlib magic), RC4/XOR with candidates `ef_first_en_AA`, the exe's 8-char
  strings (`102ycz1s`, `52AUwF8B`, `a7b77qgx`, `qmzj4Y9C`, `mvnsskip`), `system.dat`, the game title.
* Not block-cipher aligned (127 not divisible by 8 or 4-byte-word count), so plain TEA/XXTEA in the
  obvious layout is unlikely.
* Hypotheses: per-user "system save" (window/system state or gallery config) written by the exe
  (`system.dat` is referenced in the exe next to `test.tst` and the 8-char key pool); or a small
  encrypted/compressed blob. **Unknown** — needs code-level analysis of the exe/DLL to settle.
* Hexdump: `samples/12_system.dat_hexdump.txt`.

---

## 8. Encoding summary

| data | encoding |
|---|---|
| `.sav` container magic/keys/numbers | ASCII |
| `.sav` comment, `.sav` `L2`/`L3` and some script text | **CP936 / GBK** (Chinese fan translation); ASCII in the original English text |
| inline line breaks / control markers inside `L3` | literal backslash escapes `\n`, `\w`, `\a`, `\v` |
| `.tbr` | binary bitset only |
| `cgv.dat` | ASCII file names |
| `sysparams.txt` | ASCII keys/numbers + **CP936/GBK** for `fontname`, `symbolFontname` |
| `.png` | binary RGBA image |

---

## 9. Patch-related findings (CN translation + r18 patch)

* **CN translation**: `ef_first_cn_AA.dll` (root) contains `save_cn\` and `r18_cn` and
  `ef_first_en2cn.dll`; it redirects the save root to `save_cn\` and r18 assets to `r18_cn\`.
  Save data format itself is unchanged by the translation; only the text bytes inside `.sav`
  (backlog) and the font names in `sysparams.txt` are Chinese.
* **r18 patch**: the exe itself contains the registry key `SOFTWARE\minori\ef_first_en_r18patch`
  and the r18 asset directory name `r18_cn` (in `r18_cn/`: `bg.paz`, `scr.paz`, `st.paz`, `sys.paz`,
  `voice.paz`). The exe string pool also embeds a table of H-scene material:
  `H_kei01/H_kei02/H_miya01/H_miya02` plus the script names **`111_05`, `113_02`, `215_02`, `216_05`**
  (right before the H-CG names in the exe catalog) — inference: these 4 scripts are the r18 patch's
  H scenes (Kei / Miyako), and the `_mugai` scripts (`000_01mugai`, `700_01mugai`) are the all-ages
  variants used when the patch content is absent.
* **Save-data implications of the patch**: no patch/enable flag exists in `.sav`, `.tbr`, `cgv.dat`
  or `sysparams.txt` — patch state lives in the Windows registry. Therefore patched and unpatched
  runs share the same save directory. **Caveat**: `.tbr` bits and backlog `L0` IDs are per-script text
  line IDs; if the r18 patch replaces/augments those scripts with different line layouts, read flags
  and backlog entries from one version may not line up with the other (not verified — the patched
  `scr.paz` is encrypted, and running the game would modify the game folder, which was forbidden).

---

## 10. Unknowns / open questions

1. `system.dat` format and purpose (see §7).
2. Purpose of the always-zero `u32` after the `.sav` comment field.
3. Exact meaning of the 7 extra `sysparams.txt` keys (`etc`, `hir`, `kei`, `kyo`, `mid`, `miy`, `yuk`).
4. Meaning of `L4_<i>` inside multi-line backlog entries.
5. Exact `cgv.dat` semantics (unlocked-view set vs. catalog), and the `*` prefix in the exe's hard-coded
   CG catalog.
6. Exact numbering scheme of save slot files beyond "`ef%04d.sav`": which indices map to quick saves
   (`ef0001..ef0009` absent) and multi-page slots (page 2+ untested).
7. Whether `.tbr` line IDs for the r18-patched scripts match the all-ages scripts (patch incompatibility
   risk for read flags/backlog scrollback).
8. Full meaning of remaining backlog state fields (`CT3/CT4/CT5`, `CT6/CT7`, `Ce3/Ce4/Ce5`) —
   names are known from the exe variable list, but the abbreviation mapping is partly inferred.

---

## 11. Reproduce / tools

```bash
# dump any file (auto-detects .sav / .tbr / cgv.dat / sysparams.txt / system.dat)
python3 analysis/save/save_dump.py "save_cn/minori/ef - the first tale.(AA)/ef0000.sav" --tail 2
python3 analysis/save/save_dump.py "save_cn/minori/ef - the first tale.(AA)/100_01.tbr" --bits
python3 analysis/save/save_dump.py "save_cn/minori/ef - the first tale.(AA)/cgv.dat"
python3 analysis/save/save_dump.py "save_cn/minori/ef - the first tale.(AA)/sysparams.txt"
python3 analysis/save/save_dump.py --dir "save_cn/minori/ef - the first tale.(AA)"      # all 86 files
```

Raw one-liners used during the analysis:

```python
import zlib
b=open(path,'rb').read()
off=b.find(b'\x78\x9c',16)              # .sav: zlib offset (16+len(comment)+1+4)
payload=zlib.decompress(b[off:])        # .sav / .tbr / cgv.dat all use plain zlib
bitmap=zlib.decompress(b[4:])           # .tbr: fixed 8192-byte bitmap
bits=[i*8+k for i,v in enumerate(bitmap) for k in range(8) if v&(0x80>>k)]  # read line IDs
```

## 12. Sample files (`analysis/save/samples/`)

| file | content |
|---|---|
| 01_ef0000.sav_header_hexdump.txt | `.sav` header + payload head hexdump with annotation |
| 02_ef0000.variables.txt | decoded variables section |
| 03_ef0000.backlog_first_entries.txt | first backlog entries decoded GBK→UTF-8 |
| 04_100_01.tbr_hexdump.txt / 05_…_bitmap_bits.txt | `.tbr` hexdump + every set line ID |
| 06_…REJECTED_view.png | the 8192-byte `.tbr` buffer rendered as 1-bpp image (rejected hypothesis) |
| 07_tbr_script_map.tsv | 61 `.tbr` files ↔ scripts, bit counts, L0 ranges, backlog match |
| 08_cgv.dat_head_hexdump.txt / 09_cgv.dat_entries_all.txt | `cgv.dat` header + all 2236 entries |
| 10_sysparams.txt.raw_copy / 11_sysparams.decoded.txt | raw and decoded config |
| 12_system.dat_hexdump.txt | unknown binary hexdump |
| 13_slot_table.tsv | all 11 `.sav` files decoded summary |
| 14_readtext_validation.tsv | `.tbr` bits vs backlog L0 sets per script (the key proof) |
| 15_backlog_key_dictionary.tsv | 全部 backlog 键的 含义/类型/观测值（移植字段字典） |
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
