# 02 — 脚本格式与命令参考（`.sc`，musica 引擎）

## 当前记录入口（2026-10-26）

**已确认语义、已实现范围、验证通过、原版画面对照是不同状态。** 下表指向最新专题记录；后面的历史命令表中，尚未同步的 I/U 简述不能覆盖这些专题结论。

| 已记录项目 | 最新记录 | 范围与尚未确认部分 |
|---|---|---|
| stage、基础转场、滚动、音频参数 | [场景与音频](RUNTIME-STAGE-AUDIO-20261025.md)、[音频交叠](AUDIO-CROSSFADE-20261026.md) | 基础图层/坐标、type0、滚动时钟和音频行为；特殊位置标志仍待确认 |
| 消息转义、雪花、NVL、消息面板 | [文字与雪花](OPENING-TEXT-SNOW-20261025.md)、[NVL](NVL-FLOW-20261025.md)、[面板转场](PANEL-TRANSITION-20261025.md)、[消息UI](MESSAGE-UI-20261025.md) | 已支持范围、原版常量和移植近似分别记载；不代表全部UI已复刻 |
| screen / overlay | [screen](SCREEN-20261026.md)、[overlay](SHAKE-OVERLAY-20261026.md) | 参数顺序、待用材质、整数公式、底边对齐、alpha及测试；高清插值顺序仍有差异 |
| shakeScreen | [震屏](SHAKE-OVERLAY-20261026.md) | R/数字默认/H/V/Z/O；T、负时长、全局随机种子仍未完成 |
| HSVfilter / type1掩码转场 | [定点调色与掩码](HSV-MASK-20261026.md) | 图层范围、钳制/回绕、反色、混合及512完成阈值；其他限制见转场专题 |
| Cutin / WScroll2 / effect end | [切入与步行](CUTIN-WALKING-20261026.md) | 运动阶段、时钟、循环、远近差、遮挡顺序；步行其他配置路径和原版逐帧对照未完成 |
| 特殊转场 type2/14/16/17 | [特殊转场](SPECIAL-TRANSITION-20261026.md) | 用户已提交并通过复核；speed>255即时完成，type2采样与采样精度限制见专题 |
| frame / finderCounter | [取景器](FRAME-AUDIT-20261026.md) | 下一stage生效，943000层序，H/MM/SS、电量阈值与变量时钟回写已实现；数字外形和原版逐帧对照待校准 |
| 全角分号制作备注 | [解析器补证](CUTIN-WALKING-20261026.md) | 原版无指令路径；移植仅兼容该类备注，其他未知输入保持严格报错 |

专题记录保留观察、推断、移植选择和未知项，并链接原版函数/REA Evidence ID及验证结果。原始证据在本地 `work/re/evidence/` 与导出证据包中。`AGENTS.md` 记录交接进度；[剩余问题](06-RE-ROADMAP.md)集中跟踪未完成范围。

当前两变体开场严格执行均到1694条消息及首个选项；后续分支仍须验证。**不能将已导入94个剧本视为全部指令已确认。**

以上 docs/ 记录按用户最新要求随源码提交；`AGENTS.md`、原始取证包、截图及游戏资源保留在本地，不入库。

## 1. 容器/编码
- PAZ 中文件名 `*.sc`；解包后为 **纯文本**：GBK(CP936) 编码（汉化版）或 CP932（日文原版）、CRLF。
- 94 个脚本（根 75 + r18 19）；当前全量导入为 31,182 物理行、17,168 条消息（详见 `SCRIPT-RUNTIME.md`；旧 25,912 行口径不再引用）；68/75 个在 PAZ 内被 zlib 压缩（读取层由 PAZ 解压处理）。
- 文本内转义（观测）：`\n` 换行(121)、`\a`(24)、`\w`(24)、`\v`(2)。`\a` 常出现在**无文本消息**（`.message <id> <voice>   \a`）→ 推断为"空文本/自动推进标记"；`\w` 在长音/喊叫行重复出现 → 推断为**逐字停顿/节奏控制**；`\v` 出现在语音台词末尾 → 推断与**语音同步/等待语音结束**有关。`#` 出现在个别名字字段（`#宫子`）→ 推断为名字显示样式标记。以上 4 点需读文字渲染/执行代码或运行观测确认。

## 2. 行语法
```
[前缀].命令 参数...
前缀:  无  |  [e].  |  [j].
特殊:  #include <file>      （预处理，10 次）
```
`[e]`/`[j]` 是同一行的两种 timing 变体：语料中成对出现（如 `[e].transition 0 * 40` 紧邻 `[j].transition 0 * 20`），数值不同。推断为**英/日两种文本模式**（英/日文本时序不同；注意转场数值是速度，不能推导为帧数或持续时长）。移植建议：把 `[e]`/`[j]` 解析为 variant 字段，设置项选择其一（汉化版对应哪一套需运行验证）。

## 3. 命令表（46 个 RTTI 类 + 观测语法 + 出现次数）
RTTI 枚举（exe 字符串 `. ?AVCommand*`）：Base, Comment, Stage, Message, Transition, Overlay, Panel, Screen, Frame, Char, Animation, Cutin, Effect, Font, Movie, Movie2, Scroll, VScroll, HScroll, WScroll, ScrollFG, ScrollAcc, EndScroll, RotateBG, RotateFG, ShakeScreen, HSV, Sqz, SetForeground, Set, SetGlobal, DeleteVar, Label, Goto, If, Select, Chain, Exec, End, Debug, Include, Pragma, PlayVoice, PlayBGM, PlaySE, PlaySE2, Wait, WaitAnimation, WaitBGM, SetTimer, WaitTimer.
（表中 `*` 表示"保持/清除/无"的占位符；参数名称为推断。）

| 命令 | 观测语法（频次） | 语义（推断，标注为 O/I/U） |
|---|---|---|
| `.message` | `<id:int> <voiceID> <name> <text>` (17,168) | 显示台词。id=文本行号(L0，存档/已读位图按此)，voiceID=语音资源键（如 `kei-218_01-1003`，空=无语音），name=角色名（空=旁白），text=GBK 文本。O |
| `.transition` | `<type:int> <mask|*> <speed:int>` (6,380) | 画面过渡。type 见下；mask `*`=无掩码；speed 为速度而非帧数。type0 时长约25.6/speed秒；type1读反色mask，时长约51.2/speed秒；其余type待实现。O（2026-10-26 REA；见HSV-MASK-20261026.md） |
| `.stage` | 4 args `/` 6 args `/` 8 args (4,918) | 设置舞台图层（背景+立绘+滚动偏移）。观测例：`* BLACK.png 0 0`；`* bgw_normal011a01_03.png 0 600 st_miyako03d04.png 400`；`st_kei10a02-.png 0 0 bg_scl002a01_00_f.png 0 0 bg_scl002a01_00_n.png 0`。推断为 1~3 组 `<file> <x> <y>`（`*`=不变），需 Execute 确认。I/U |
| `.overlay` | `[frame_file] <bg_file|*> <fg_file|*>` (163) | 下一stage的边框/背景/前景叠加滤色；省略frame即清除它。色带底对齐，在HSV之后、screen之前按原整数公式计算。O（2026-10-26 REA） |
| `.panel` | `<int>` (566) | 文本窗口面板状态（0/3 等值）。U |
| `.screen` | `[frame_file] <bg_file|*> <fg_file|*>` (191) | 下一stage的边框/背景/前景RGB滤色；双参数清空边框，`*`清除；源图底对齐色带并保留alpha。O（2026-10-26 REA；见SCREEN-20261026.md） |
| `.frame` | `<file|*>` (28) | 独立叠层，下一stage生效；finderCounter驱动取景器秒数与四档电量，*清除。O（2026-10-26 REA；实现/限制见顶部专题） |
| `.char` | —（脚本中未见，类存在） | 角色表情/状态管理（类 CommandChar；`@load/.char loada/.char keep` 为内部/调试形式）。U |
| `.animation` | —（脚本中未见） | 立绘动画（`st_*.ani` 序列）。U |
| `.cutin` | —（脚本中未见，作为 `.effect Cutin <file> <n>` 出现） | 切入动画。I |
| `.effect` | `<name> [file] [a] [b]` (100) | 已支持Snow/SnowH、Cutin（图、hold ticks、可选加速）、WScroll2（* 60 -2）、fadeout、end；特效层在边框之下。其余名称/参数仍严格拒绝。O（2026-10-26 REA，见CUTIN-WALKING-20261026.md） |
| `.scroll` | `<x> <y> <speed>` (32) | 背景滚动到坐标（帧数）。I |
| `.vscroll` | `<y> <speed>` (39) | 垂直滚动；100Hz 时钟下每 tick 前进 speed/10 像素（10×speed 像素/秒），正负指定方向。O（2026-10-25 REA） |
| `.hscroll` | `<x> <frames>` (20) | 水平滚动。I |
| `.wscroll` | —（未见；`.effect WScroll2` 相关） | 甩动/惯性滚动。U |
| `.scrollFG` / `.scrollAcc` / `.rotateBG` / `.rotateFG` | —（未见） | 前景滚动/加速滚动/旋转。U |
| `.endScroll` | `<f|t>` (48) | 结束滚动状态。I |
| `.shakeScreen` | `<mode> <amp:int> <ticks:int>` (17) | 阻塞震屏，ticks为10ms单位；R/数字默认为四向平移，H/V交替，Z裁切放大交替，O旋转叠影。T/负时长未实现。O（2026-10-26 REA） |
| `.HSVfilter` | `<bg_h> <bg_s> <bg_v> <fg_h> <fg_s> <fg_v>` (19) | 下一stage背景/前景定点调色，边框不受影响；h存储但原像素函数不使用，s/v钳制±100；全零清除。O（2026-10-26 REA） |
| `.movie` | `<id> <file> <w> <h> <t|f>` (8) | 全屏播放视频（AVI/MPEG：`ed_moji_test.avi`、`ef_first_sound.avi`、`ef_ED_sound.avi`），w/h=800/600。I |
| `.movie2` | —（未见） | 视频变体。U |
| `.playBGM` | `<file|*> [fade_in_speed] [fade_out_speed] [volume_percent]` (427) | 播放/停止 BGM；`*`=停止，`0 *` 是立即淡入和淡出速度占位，不能解释为静音。O（2026-10-25 REA） |
| `.playVoice` | `<voiceID|*>` (12) | 播放语音（`*`=停止）。I |
| `.playSE` | `<file|*> [loop:t|f] [fade_in_speed] [fade_out_speed]` (259) | 音效；例 `kyoukai_door.ogg f 0 *` 为不循环、立即淡入，第四参数不是声像。O（2026-10-25 REA） |
| `.playSE2` | 同上 (13) | 第二音效通道。I |
| `.wait` | `<ticks:int>` (370) | 等待（10ms ticks，200=2秒、350=3.5秒）。O（2026-10-25 REA） |
| `.waitBGM` / `.waitAnimation` / `.waitTimer` | —（未见） | 等待结束。I |
| `.setTimer` | —（未见） | 定时器。U |
| `.label` | `<name>` (45) | 标签。O |
| `.goto` | `<label>` (10) | 跳转。O |
| `.if` | `<var> <op> <int> <label>` (26) | 条件跳转；op ∈ `==`, `<=`, ...；例 `.if ef_000 == 1 ef_000_b1`。O |
| `.select` | `<text>:<label> <text>:<label> ...` (8) | 选项菜单；例 `.select 继续聊天:label1 去追宫村:label2`。O |
| `.set` | `<var> = <expr>` / `<var> = <var> + <int>` (49) | 局部变量赋值/自增。O |
| `.setGlobal` | `<var> = <int>` (21) | 全局变量（跨脚本/周目；r18 门控 `censored`、`H_*` 即此类）。O |
| `.deleteVar` | —（未见） | 删除变量。U |
| `.chain` | `<script.sc>` (94) | 场景衔接：把控制权交给下一脚本（`100_01.sc`→`101_01.sc`…；首脚本 `test.sc` chain 到 `000_01mugai.sc`）。O |
| `.exec` | `<script?>` (未见) | 子脚本调用/返回。U |
| `.end` | (38) | 脚本结束。O |
| `.pragma` | `enable_control` / `disable_control` (29) | 控制输入/自动推进开关。I |
| `.debug` | — | 调试。U |
| `#include` | `<file>` (10) | 文本包含（预处理）。O |
| 内部命令 | `@load`, `@stage`, `@update`, `@finish`, `@finish_all`, `.char loada`, `.char keep` | 引擎内部/调试指令（exe 字符串可见，普通脚本未见）。U |

## 4. 运行时/状态模型（结合存档证据）
- 存档含 `!script_Filename`、`!script_ID`（=当前文本行 id/L0）、`!script_Pointer`（编译脚本指令下标）；还有 `!font_Color/FontSize/RubySize`。移植版建议用 **script+行 id** 作为主定位锚点，`Pointer` 仅校验（详见 `analysis/save/SAVE_FORMAT.md` §13）。
- 已读文本位图 `.tbr`：每脚本 65536 bit，bit=行 id（MSB-first）→ 移植版"跳过已读"所需数据格式已明确。
- 全局变量决定路线/r18；`censored` 控制 H 内容（`000_01mugai.sc` → chain；r18 脚本用 `.if censored == 1` 跳过）。
- 资源命名约定：背景 `bg*/ago*`，立绘 `st_*`，overlay `or_*`，系统 UI 在 sys.paz，BGM `efBGM*.ogg`，SE 简短名，语音 `voice`（无扩展名，OggS 嗅探）。

## 5. 强制校验
`port/tools/sc_asset_audit.py`：94 个脚本引用的 **3,150 个不同资源全部存在于 13 个 PAZ 归档中（0 缺失）** → 脚本解析/资源映射正确。
