# 脚本命令参数语法（语料统计，自动生成）

> 数据来源：94 个 `.sc` 脚本（25,912 行、17,168 条 `.message`）。每种命令下列出观测到的「参数类型序列」及出现次数，并给出真实例句。
>
> 类型词表：`int`=整数、`float`=小数、`hex`=十六进制、`file`=文件名（含扩展名）、`ident`=标识符/标签名、`assign`=含 `=` 的赋值表达式、`*`=占位符（保持/清除）、`other`=其它（如 `:label`、`+` 等）。
>
> 这是原始统计附录；命令语义解读见 `02-SCRIPT-REFERENCE.md`。

## 原始统计数据（自动生成）


## .HSVfilter
- `int int int int int int` x19
  e.g. `[j].HSVfilter 0 0 0 0 0 0`
  e.g. `[j].HSVfilter 0 0 0 0 0 0`
  e.g. `.HSVfilter 0 0 0 5 -5 0`
  e.g. `.HSVfilter 0 0 0 0 0 0`

## .chain
- `other` x94
  e.g. `.chain 100_01.sc`
  e.g. `.chain 101_01.sc`
  e.g. `.chain 102_01.sc`
  e.g. `.chain 102_02.sc`

## .effect
- `ident` x51
- `ident * int int` x38
- `ident file int int` x6
- `*` x3
- `ident file int` x2
  e.g. `.effect Snow`
  e.g. `[j].effect fadeout`
  e.g. `[e].effect fadeout`
  e.g. `.effect *`

## .end
- `` x38
  e.g. `[j].end`
  e.g. `[j].end`
  e.g. `[j].end`
  e.g. `.end`

## .endScroll
- `ident` x48
  e.g. `.endScroll f`
  e.g. `.endScroll f`
  e.g. `.endScroll f`
  e.g. `.endScroll f`

## .frame
- `file` x14
- `*` x14
  e.g. `.frame finder.png`
  e.g. `.frame *`
  e.g. `.frame finder.png`
  e.g. `.frame *`

## .goto
- `ident` x10
  e.g. `[j].goto ef_000_b1`
  e.g. `[j].goto ef_000_b2`
  e.g. `[j].goto ef_000_b3`
  e.g. `.goto miyako_c`

## .hscroll
- `int int` x20
  e.g. `[e].hscroll 0 -25`
  e.g. `[j].hscroll 0 -10`
  e.g. `[e].hscroll 660 40`
  e.g. `[j].hscroll 660 10`

## .if
- `ident assign int ident` x20
- `ident other int ident` x5
- `ident assign int other` x1
  e.g. `[j].if ef_000 == 1 ef_000_b1`
  e.g. `[j].if ef_000 <= 0 ef_000_a1`
  e.g. `[j].if ef_000 == 1 ef_000_b2`
  e.g. `[j].if ef_000 <= 0 ef_000_a2`

## .label
- `ident` x44
- `other` x1
  e.g. `.label ef_000_a1`
  e.g. `.label ef_000_b1`
  e.g. `.label ef_000_a2`
  e.g. `.label ef_000_b2`

## .message:head
- `int other other` x17168

## .movie
- `int file int int ident` x8
  e.g. `.movie 10000 ed_moji_test.avi 800 600 f`
  e.g. `.movie 10000 ed_moji_test.avi 800 600 t`
  e.g. `.movie 10001 ef_first_sound.avi 800 600 f`
  e.g. `.movie 10002 ef_ED_sound.avi 800 600 f`

## .overlay
- `* file` x64
- `* *` x60
- `file * *` x20
- `* * *` x19
  e.g. `[e].overlay * or_miyako03d.png`
  e.g. `[e].overlay * or_miyako03d.png`
  e.g. `[j].overlay * or_miyako03a02.png`
  e.g. `.overlay * *`

## .panel
- `int` x566
  e.g. `.panel 3`
  e.g. `.panel 0`
  e.g. `.panel 3`
  e.g. `.panel 0`

## .playBGM
- `*` x201
- `file` x198
- `* * int` x10
- `file int *` x9
- `* int int` x8
- `* int` x1
  e.g. `.playBGM efBGM016.ogg`
  e.g. `.playBGM * 10`
  e.g. `.playBGM efBGM025mix.ogg`
  e.g. `.playBGM efBGM026mix.ogg`

## .playSE
- `file ident int *` x160
- `*` x58
- `file ident` x19
- `* ident * int` x17
- `file ident * int` x2
- `* int` x1
- `file ident other` x1
- `other ident int *` x1
  e.g. `.playSE kyoukai_door.ogg f 0 *`
  e.g. `.playSE kyoukai_door.ogg f 0 *`
  e.g. `.playSE akikan.ogg f 0 *`
  e.g. `.playSE bike_a2.ogg f 0 *`

## .playSE2
- `*` x7
- `file ident int *` x3
- `file ident` x3
  e.g. `.playSE2 yuka.ogg f 0 *`
  e.g. `.playSE2 *`
  e.g. `.playSE2 hiro_keitai_male.ogg f 0 *`
  e.g. `.playSE2 hiro_keitai_male.ogg f 0 *`

## .playVoice
- `*` x11
- `* * int` x1
  e.g. `[j].playVoice *`
  e.g. `.playVoice *`
  e.g. `.playVoice *`
  e.g. `.playVoice *`

## .pragma
- `ident` x29
  e.g. `[j].pragma disable_control`
  e.g. `.pragma enable_control`
  e.g. `[j].pragma disable_control`
  e.g. `.pragma enable_control`

## .screen
- `file file` x78
- `* *` x74
- `file file file` x20
- `* * *` x19
  e.g. `.screen sc_night01.png sc_night01.png`
  e.g. `.screen * *`
  e.g. `.screen sc_night01.png sc_night01.png`
  e.g. `.screen * *`

## .scroll
- `int int int` x32
  e.g. `[e].scroll 581 837 30`
  e.g. `[j].scroll 581 837 6`
  e.g. `[e].scroll 400 1046 15`
  e.g. `[j].scroll 400 1046 6`

## .select
- `other other` x8
  e.g. `.select 继续聊天:label1 去追宫村:label2`
  e.g. `.select 去看看景:label1 直接回家:label2`
  e.g. `.select 去接宫子:label1 优先工作:label2`
  e.g. `.select 去看看情况:label1 应该没事吧:label2`

## .set
- `ident assign int` x44
- `ident assign ident other int` x5
  e.g. `.set favMiyako_a = favMiyako_a + 1`
  e.g. `.set favMiyako_d = favMiyako_d + 1`
  e.g. `.set favMiyako = favMiyako + 1`
  e.g. `.set favMiyako = favMiyako + 1`

## .setGlobal
- `ident assign int` x21
  e.g. `[j].setGlobal EYE_SEQ_DEFAULT = 2`
  e.g. `.setGlobal ef_000 = 1`
  e.g. `[j].setGlobal EYE_SEQ_DEFAULT = 2`
  e.g. `[e].setGlobal BADEND1 = 1`

## .shakeScreen
- `ident int int` x16
- `int int int` x1
  e.g. `.shakeScreen R 100 100`
  e.g. `.shakeScreen R 100 100`
  e.g. `.shakeScreen R 100 100`
  e.g. `.shakeScreen R 100 100`

## .stage
- `* file int int` x3303
- `* file int int file int` x1050
- `other int int file int int file int` x256
- `file file int int` x219
- `* file int int file int file int` x34
- `* file int int file other` x32
- `file file int int file int` x18
- `* file int int other int` x3
  e.g. `.stage * BLACK.png 0 0`
  e.g. `[j].stage * BLACK.png 0 0`
  e.g. `[j].stage * BLACK.png 0 0`
  e.g. `[j].stage * BLACK.png 0 0`

## .transition
- `int * int` x5901
- `int file int` x479
  e.g. `.transition 0 * 10`
  e.g. `[j].transition 0 * 15`
  e.g. `[j].transition 0 * 20`
  e.g. `[j].transition 0 * 15`

## .vscroll
- `int int` x39
  e.g. `[e].vscroll 1200 25`
  e.g. `[j].vscroll 1200 10`
  e.g. `[e].vscroll 0 -30`
  e.g. `[j].vscroll 0 -10`

## .wait
- `int` x370
  e.g. `[j].wait 200`
  e.g. `[e].wait 350`
  e.g. `[j].wait 100`
  e.g. `[j].wait 80`
