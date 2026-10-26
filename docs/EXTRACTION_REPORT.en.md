> **英文原稿（备份，供对照）** — 正式中文译本见同目录下同名文件（去掉 `.en`）。

# PAZ extraction report — `ef - the first tale` (English Steam build + Chinese/r18 patch data)

Tool: `paz.py` (this directory). Format spec: `PAZ_FORMAT.md`. Machine-readable
stats: `re_tools/archive_stats.json`, `re_tools/extract_stats.json`,
`re_tools/toc_check.json`. Reproduction: `python3 re_tools/verify_extracted.py`.

All extraction results below were produced with:

```
python3 paz.py list-all
python3 paz.py extract <archive>.paz extracted/<name> [--limit N] [--filter REGEX]
```

No failures: **all 13 archives decrypt and list** (12,289 TOC entries total), and
**every extracted file passed its type check** (0 magic/decode failures).

---

## 1. Verified archive inventory (TOC level, 13/13 archives)

| archive | game id | entries | zlib-packed | file bytes | sum(unpacked bytes) | data start | first offset | last end |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| bg.paz | ef_first_en_AA | 3457 | 1333 | 1,272,342,060 | 1,379,693,457 | 143164 | 143164 | = file size |
| bgm.paz | ef_first_en_AA | 29 | 0 | 51,733,692 | 51,732,464 | 1132 | 1132 | = file size |
| mov.paz | ef_first_en_AA | 4 | 0 | 51,003,452 | 51,002,980 | 460 | 460 | = file size |
| scr.paz | ef_first_en_AA | 75 | 68 | 405,268 | 1,115,536 | 2612 | 2612 | = file size |
| se.paz | ef_first_en_AA | 89 | 0 | 6,494,884 | 6,490,889 | 3676 | 3676 | = file size |
| st.paz | ef_first_en_AA | 1033 | 506 | 121,894,852 | 144,464,192 | 43708 | 43708 | = file size |
| sys.paz | ef_first_en_AA | 572 | 0 | 20,932,372 | 20,905,364 | 24972 | 24972 | = file size |
| voice.paz | ef_first_en_AA | 5830 | 0 | 217,589,044 | 217,335,168 | 233252 | 233252 | = file size |
| r18_cn/bg.paz | ef_first_en_AA_R18Patch | 868 | 388 | 288,256,132 | 331,650,781 | 35884 | 35884 | = file size |
| r18_cn/scr.paz | ef_first_en_AA_R18Patch | 19 | 12 | 79,868 | 230,158 | 716 | 716 | = file size |
| r18_cn/st.paz | ef_first_en_AA_R18Patch | 2 | 1 | 125,180 | 151,835 | 124 | 124 | = file size |
| r18_cn/sys.paz | ef_first_en_AA_R18Patch | 63 | 0 | 1,540,276 | 1,537,107 | 2932 | 2932 | = file size |
| r18_cn/voice.paz | ef_first_en_AA_R18Patch | 248 | 0 | 13,178,484 | 13,167,589 | 9972 | 9972 | = file size |

Structural check (all 13, `re_tools/toc_check.json`):
`toc_len == align8(4 + [256 for mov] + Σ(name_len+1+24))`, `first entry offset ==
0x24 + toc_len`, `last entry end == file size`. This is the primary correctness
proof of the TOC layout and of every entry placement.

Content-type census by file extension (from the TOCs):

| archive | png | ani | ogg | wav | sc | avi/mpg | psd/txt/tmp/other | no ext |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| bg | 2124 | 1333 | | | | | | |
| bgm | | | 29 | | | | | |
| mov | | | | | | 1 mpg + 3 avi | | |
| scr | | | | | 75 | | | |
| se | | | | | | | | 89 ogg¹ |
| st | 527 | 506 | | | | | | |
| sys | 560 + 2 `.png-` + 1 `.png.tmp` | | | 6 | | | 3 psd | |
| voice | | | | | | | | 5830 ogg¹ |
| r18_cn/bg | 480 | 386 | | | | | 2 txt | |
| r18_cn/scr | | | | | 19 | | | |
| r18_cn/st | 1 | 1 | | | | | | |
| r18_cn/sys | 63 | | | | | | | |
| r18_cn/voice | | | | | | | | 248 ogg¹ |

¹ entries with no extension; all sniff to `OggS`/Vorbis. The engine treats every
entry of `bgm/se/voice` archives as audio.

---

## 2. What was fully extracted (in `extracted/`)

| path | contents | files | bytes |
|---|---|---:|---:|
| `extracted/scr/` | **all** scripts from scr.paz (`.sc`, raw GBK text) | 75 | 1,115,536 |
| `extracted/sys/` | **all** UI/config/font/table images, sounds, PSDs | 572 | 20,905,364 |
| `extracted/r18_scr/` | **all** r18-patch scripts | 19 | 230,158 |
| `extracted/r18_sys/` | **all** r18-patch UI images | 63 | 1,537,107 |
| `extracted/bg/` | first 20 entries of bg.paz (mixture of `.png` + `.ani` sample pairs) | 20 | 1,715,938 |
| `extracted/st/` | first 20 entries of st.paz (10 PNG + 10 ANI, in TOC order) | 20 | 4,385,910 |
| `extracted/se/` | 5 OGG sfx samples | 5 | 269,870 |
| `extracted/bgm/` | 5 OGG music samples | 5 | 7,875,051 |
| `extracted/voice/` | 5 OGG voice samples | 5 | 167,073 |
| `extracted/mov/` | 2 small video samples: `op_logo.mpg` (MPEG-PS) + `ed_moji_test.avi` (AVI) | 2 | 675,908 |
| `extracted/r18_bg_notes/` | `names.txt` (CP932 name table) + `log.txt` (packer leftover dir listing) from r18_cn/bg.paz | 2 | 30,146 |
| `extracted/scr_examples/` | readable examples: raw ASCII scripts + GBK→UTF-8 conversions | 7 | ~ |
| **total** | | **788** | **~39 MB** |

The huge archives (bg/st/voice/bgm/mov) were deliberately **not** fully extracted
(per task instruction); only the entries above plus all scr/sys.

---

## 3. Decoded resource formats and standard-library decodability

Every extracted file was opened with a standard decoder (Pillow / Python `wave` /
`ffprobe` / text decode). Results (`re_tools/extract_stats.json`), 0 failures:

| class | ext(s) | internal format | extracted instances | sizes (unpacked) | standard decoder? | verified |
|---|---|---|---|---:|---|---|
| images | `.png`, `.png-`, `.png.tmp` | PNG (RGBA/RGB, mostly 800×600; sys also has 999×600 CGs, tiny 107×80 thumbs, 1×601 strips) | 20 (bg) + 20 (st) + 572 (sys) + 63 (r18 sys) visual files total incl. PNGs | 1.5 KB … 3 MB (max 2,999,897 B) | **yes** (Pillow/ImageMagick/libpng) | all PNGs `Image.open()/load()` OK |
| animations | `.ani` | minori custom "ANI" (paired with a `.png` of the same stem; `00 01 02 00 00 00 00 00` or `00 01 04 00 00 00 00 00` header; 10 KB … 283 KB) | 6 (bg) + 9 (st) | 10,290 … 283,636 B | **no standard format** (custom; needs RE if animations are ported; eyes/mouth/blink data for the standing sprite) | header census in `extract_stats.json` |
| audio | `.ogg` | Ogg Vorbis | 89 (se), 29 (bgm), 5830 (voice), 248 (r18 voice) | se 5.7 K…220 K; bgm 0.9–2.3 MB; voice ~13–47 K (typical) | **yes** (libvorbis/ffmpeg) | samples: `vorbis`, `se` 22050 Hz stereo, `bgm` 44100 Hz stereo, `voice` 44100 Hz **mono** — all probe OK |
| audio | `.wav` | RIFF/WAVE PCM | 6 (sys) | 4.6 K … 7,356,460 B | **yes** (Python `wave`) | all 6 parse: mono/stereo 22050/44100 Hz PCM16 |
| video | `.mpg` | MPEG-1 program stream (MPEG-1 video + MP2 audio), 800×600, 4 s | 1 (mov) | 446,468 B | **yes** (ffmpeg) | ffprobe: mpeg1video+mp2, 800×600, 3.997 s |
| video | `.avi` | RIFF AVI, MPEG-4 video, 800×600 | 3 (mov) | 229,440 B … 39,668,126 B | **yes** (ffmpeg) | `ed_moji_test.avi`: mpeg4 800×600, 6.83 s; other two same container |
| images/sources | `.psd` | Adobe Photoshop 8BPS (stored raw, no RC4 — name does not match png/ogg/sc/avi rule) | 3 (sys: `icons.psd`, `msgPanel.psd`, `gaiji.psd`) | 20,711 / 26,156 / 37,088 B | **yes** (Pillow PSD plugin) | all 3 `Image.open()`/`load()` OK |
| scripts | `.sc` | plain text, **GBK/CP936**, CRLF; engine command language (see §5) | 75 + 19 | 77 B … 180 KB | **no** (custom text format; needs a script interpreter, but is human-readable) | 94/94 strict-GBK decodes OK |
| name table | `.txt` (r18) | CP932/Shift-JIS text, `st_miyako<TAB>みやこ` mapping | 1 | 495 B | yes (plain text) | decodes CP932 OK |
| log | `.txt` (r18) | ASCII `dir` listing from pack machine (2011) | 1 | 29,651 B | yes | text OK |
| voice no-ext | *(none)* | Ogg Vorbis | several thousand | ~5 K…150 K | yes (sniff `OggS`) | ffprobe OK on samples |

Useful numbers for a port: the majority of image assets are PNG (libpng-decodable
as-is), all audio is Ogg Vorbis / PCM WAV, videos are MPEG-1/MPEG-4 in standard
containers. The **only** custom binary asset formats are `.ani` (animation deltas)
and `.sc` (script text); `.psd` is genuine PSD.

---

## 4. r18 patch (`r18_cn/`) analysis

Same container/keys, `game_id = ef_first_en_AA_R18Patch`, separate data directory
(the exe has the registry flag `SOFTWARE\minori\ef_first_en_r18patch`). Name-level
relation to the base data:

| r18 archive | entries | names also in the *matching* base archive | names also in **any** base archive | completely new names | notes |
|---|---:|---:|---:|---:|---|
| bg | 868 | 0 (base bg is backgrounds only) | 1 (`st_yuko09f01_bokashi.png` overrides st.paz) | 867 | 480 PNG + 386 ANI R18 CG/standing images (`evh01–evh05`, `ev01–ev02`), 2 txt |
| scr | 19 | 13 | 13 | 6 | 13 **overridden** scripts (sizes differ; r18 variants) + 6 new R18 scripts (`113_02_ero.sc`, `216_05*.sc`, …) |
| st | 2 | 0 | 0 | 2 | `st_kei06g05.png`+`.ani` |
| sys | 63 | 4 | 4 | 59 | 4 overridden `MemoriesSelect*` images + 59 new CG thumbnails (`thumb\...`) |
| voice | 248 | 0 | 0 | 248 | extra R18 voice files |

Interpretation (inference from content + exe flag): the patch is an additive
override layer merged by file name when the R18 flag is on; it is *not* a full copy
of the game data. The base `scr.paz` in this installation already contains the CN
translation (GBK) and some `_ero` scripts; `r18_cn/scr.paz` supplies the remaining
overridden/new r18 scripts (`test.sc` sets global `censored = 0`; r18 scripts use
`.setGlobal H_kei01 = 1` etc. and gate explicit scenes with
`[e].if censored == 1 skipit`).

`r18_cn/bg.paz` also carries two plain files (ordinary PAZ entries): `names.txt`
(CP932 sprite-id → Japanese-name table used by the engine UI) and `log.txt`
(a leftover 2011 `dir` listing of the original authoring machine, listing `.psd`
sources — proof that `.psd` entries in `sys.paz`/patch are authoring artifacts).

---

## 5. Script format (`scr.paz`, the most porting-relevant asset)

* **Files**: 75 in `scr.paz`, 19 in `r18_cn/scr.paz`; extension `.sc`.
* **Size**: 77 B … 180 KB (root total 1,115,536 B; 25,912 lines).
* **Encoding**: text, **CRLF**, **GBK/CP936** (Chinese fan translation already
  applied to the root archive: 68/75 files decode strictly as GBK with 0 errors and
  fail Shift-JIS; 7/75 are pure ASCII). The original Japanese release and the
  English release store these as Shift-JIS; the CN patch re-encoded text to GBK.
* **Compression**: 68/75 are zlib-compressed in the archive (handled transparently
  by `paz.py`); 7 stored raw.
* **Line language** (all commands/operators are ASCII; only message/choice text is
  localized):

```
[e].transition 0 * 40                  ; command with optional layer prefix [e] / [j]
[j].transition 0 * 20
.stage * ed02_001a01.png 0 0           ; background / foreground layers
.panel 3
.message 100 kei-218_01-1003  在进入暑假后，又过了几天——
.message 110 hir-218_01-0003 纮 「——这我也明白的，可是……」
.playBGM efBGM022.ogg
.playSE hiro_bag_naguru.ogg f 0 *
.wait 150
.shakeScreen R 10 10
.setGlobal censored = 0
.select 继续聊天:label1 去追宫村:label2 ; choice text : jump label
.if favMiyako_a > 0 miyako_a            ; conditional jump
.goto miyako_c
.label label1                           ; label definition
.chain 100_01.sc                        ; continue with another script file
#pragma enable_control                  ; engine pragma
#include 111_05_ero.sc                  ; script include (preprocessor-like)
.end
```

* **Command census** (root scr, 29 distinct commands, 25,838 non-empty lines):

  | command | uses | command | uses |
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

* **Layer prefixes**: many commands carry a `[e]` or `[j]` prefix (`[e].transition
  0 * 40` / `[j].transition 0 * 20` appear as pairs with different timings; `[j]`
  also appears as `[j]:.wait 300` and as `[j]；（…）` Japanese authoring comments).
  Hypothesis (to be verified by VM analysis): `[e]`/`[j]` select the command stream
  for English vs Japanese text mode — the English build carries two timing variants.
  Semantics **unknown**, reported as observation.
* **Message format**: `.message <number> <voice-id>  <text>` — `<voice-id>` selects
  a file in `voice.paz` (e.g. `kei-218_01-1003`), text may be empty or contain
  backslash escapes (e.g. `\a`); some messages have no voice id
  (`.message 101   \a`).
* **Command name table found in the exe** (`.rdata` file offset 0x176c00–0x176f40,
  dump in `re_tools/exe_script_command_tokens.txt`) additionally contains commands
  not used by ef but supported by this engine: `.movie2`, `.setFG`, `.char`,
  `.animation`, `.scrollFG`, `.scrollAcc`, `.rotateBG`, `.rotateFG`, `.cutin`,
  `.wscroll`, `.deleteVar`, `.setTimer`, `.waitTimer`, `.waitAnimation`,
  `.waitBGM`, `.exec`, `.debug`, and character-layer tokens
  (`bgS/bgV/stH/stS/stV/aBG/rBGdx/aFG/…`, `@define/@pos/@move/@load/@stage/@update/
  @finish`). This is the natural starting point for the script VM.
* **Examples saved**: `extracted/scr_examples/` (`test.sc`, `opening.sc`,
  `movie.sc` raw; `218_01.sc.utf8.txt`, `216_02.sc.utf8.txt`,
  `r18_216_05_ero.sc.utf8.txt` transcoded to UTF-8 for reading).

---

## 6. Quirks, edge cases, failed cases

* **Failed entries: none.** All 12,289 TOC entries decrypt; all 788 extracted files
  pass checks.
* **`st.paz` `.ani` +8 quirk**: compressed `.ani` inflates to `original_size + 8`
  with 8 trailing NUL bytes (506/506 in st.paz, 1/1 in r18_cn/st.paz; bg.paz `.ani`
  are normal). The engine and packer use only `original_size` bytes; `paz.py`
  truncates and reports the adjustment. All other compressed entries match exactly.
* **Non-standard extensions** inside `sys.paz`: `pauseAnime.png-`,
  `iconson.png-` (PNG content, trailing `-` kept by packer), `MemoriesBaseOver.png.tmp`
  (PNG content) and `voice.paz` entries without any extension (Ogg Vorbis). A port
  must sniff contents, not trust extensions.
* **`.psd` entries** are stored *raw* (not compressed, and no RC4 because `.psd`
  doesn't match any type password) — they come from authoring sources; Pillow can
  read them, but the engine may not actually load them.
* **Mixed namespaces**: r18 patch stores standing images in `bg.paz` and CG
  thumbnails in `sys.paz`; entry names may contain backslashes (`thumb\th_*.png`).
  Use the archive's file namespace, not the archive name, to classify assets.
* **`mov.paz` has exactly 4 entries** (whole archive = 51 MB): `op_logo.mpg`,
  `ef_first_sound.avi` (39.7 MB), `ef_ED_sound.avi` (10.7 MB), `ed_moji_test.avi`.
  Sample extracted: first two (small), remaining two listed only.
* **Root archives already contain CN-translated data** (GBK scripts, some r18-ish
  `_ero.sc` files) — the installation is the Chinese fan-translation state, not a
  pristine English release. `r18_cn/` is the additional R18 patch data set.

---

## 7. Known unknowns (for the port)

1. `.ani` format internals (custom animation deltas; not decoded — only paired-file
   relationship and header bytes observed).
2. Script semantics for `[e]`/`[j]` prefixes, `.stage` arguments, message numbering,
   `@…` character-layer commands, and the VM itself (next phase, Lead's area).
3. Whether `.psd`/`names.txt`/`log.txt` are used by the runtime at all.
4. r18 merge order/precedence at runtime (inferred by name, not observed executing).
5. `simple_key != 0` and multi-part `.pazA` archives (not present here).
6. Exact reason for the `.ani` +8 quirk (packer padding artifact).
