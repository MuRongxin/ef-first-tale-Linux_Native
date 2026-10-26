# 06 — 剩余逆向任务路线（带具体地址/线索）

> 当前状态（2026-10-26）：已确认项目的统一入口见 [02-SCRIPT-REFERENCE.md](02-SCRIPT-REFERENCE.md) 顶部。历史章节中的环境、阻塞点和未确认标注以最新专题记录为准。
>
> 当前重点：验证首个选项之后的各剧情分支；转场type2/14/16/17已由用户完成，`.frame`语料范围现已实现；接下来处理`.stage`逗号位置参数、视频及其他未完成路径；补齐特殊震屏、步行配置分支和原版逐帧对照。开场1694条消息通过不等于全剧情演出完成。


> 已完成项（PAZ/存档/脚本文本/字体/资源）见 00-OVERVIEW。以下为影响"保真度"的剩余项，按优先级排列。

## P1 脚本命令执行语义（最重要）
**已解决：原版 bg/st 同名立绘优先级（2026-10-26）**：
- 观察：`st_kei10b05-.png` 在 bg/st 两仓内容不同（bg 810×620 / st 800×620，alpha 裁剪后的画稿逐像素相同），被引用 15 次；
  `st_kei11b03_close-.png` 同样不同，被引用 4 次。两张都是 tachie 类。
- 结论：原版 stage 立绘加载链 `0x4df99c → 0x5081a0 → 0x5068e0` **先查 st 归档读取器**
  （slot3/`0x5e0898`，检查点 `0x506cb0`），仅当 st.paz 缺失时才回退 bg 读取器（slot2/`0x5e0628`，`0x506d2e`），
  并把 image type 置为 3（st）/2（bg）。两处同名图都存在于 st.paz，因此 canonical = `st/<name>`（800×620）。
- 归档/读取器顺序的运行时证据：Wine + strace 观测原版打开 `scr, r18_cn/scr, bg, r18_cn/bg, bg, r18_cn/bg, st,
  r18_cn/st, sys, r18_cn/sys, bgm, voice, r18_cn/voice, se, se, mov`，与读取器对象数组 `0x5e0148`（10 个 0x270 槽）
  及名称表 `0x5a9f60` 的 `scr,bg,bg,st,sys,bgm,voice,se,se,mov` 顺序一致；r18 patch 归档在各自 base 之后打开。
- 移植实现：`Content::lookup_resource` 对仅含 bg/st 两候选且内容不同的图片按 st 优先解析（其余歧义仍失败关闭）；
  Python `content_resources.ResourceIndex.resolve` 同策略。`--audit-resources`：14,942 次 / 9,157 种引用，
  缺失 0、**歧义 0**（原 19）、包内丢失 0，退出码 0。
- r18 仅覆盖对应基础归档类别属于移植版显式策略（与归档打开顺序一致），仍不能表述为本轮新取得的"运行期覆盖"证据。

**已取证：`.movie`（2026-10-26，实现待 FFmpeg 开发包）**：
- `CommandMovie`：命令名 `0x578588`、注册 `0x4cb3c8`、对象 0x78、构造 `0x4ec7b0`、Parse `0x4ec3a0`、Execute `0x4ec850`。
  参数至少 4 个：`<id> <file> [w] [h] [flag] [x] [y]`；默认 w=800/h=600、flag=true、x=y=0；
  flag 首字符 `0/f/F` 为 false，其余/缺省 true；`id` 解析保存但 Execute 不读取。
- 播放链：Execute → `0x4db670`（清场景、取 mov.paz 读取器 slot9=`0x5e1738`）→ `0x4fd820`（读取器检查文件）→
  `0x4fcda0` 启动全局播放器 `0x5aa320`；按扩展名 `.mpg/.mpeg/.avi` 建立 DirectShow 图
  （`CFileStream` vtable `0x575a10`，filter 名 `MPEG-I Stream Splitter/MPEG Video Decoder/MPEG Audio Decoder/
  Video Renderer/Sound Renderer/VMR7`，音频 48000）。
- 阻塞：movie active 时主循环（`0x4bf368` 起）调用 `0x4fc3b0` 绘制并暂停脚本；`0x4fc9e0` 为播放/消息循环，
  播完 `0x4fc750` 释放。flag≠0 时中断输入会写配置键 `movieInterrupted` 后停止（=可跳过），flag=0 跳过该分支（不可跳过）。
- 实现方案与依赖：content 已有 4 个视频（`mov/op_logo.mpg` 等，H.264 MKV 3840×2880）；
  移植需 FFmpeg 开发包（libavcodec/format/util/swscale/swresample-dev；本机仅运行库），
  解码→纹理按 `(x,y,w,h)` 绘制、音频送现有 mixer；先做命令/阻塞状态机，再接视频管线。详见 `MOVIE-20261026.md`。
- 残余：flag 动态按键验证、`.movie2`、`id` 其它用途、DirectShow 渲染细节、`movieInterrupted` 的存档作用。

**已知线索**（本目录 exe，PE32 i386）：
- 命令分派函数：`0x4CB1A8 – 0x4CCE8F`（按命令名串匹配，逐命令 `new` 命令对象后登记）。命令名表在 VA `0x578570–0x578740`（46 个）。
- 命令类 RTTI 名可枚举（`strings ef_first_AA.exe | grep '\.?AVCommand'`，52 个类，含 `CommandBase`）；对象大小示例：`.panel` 命令对象 0x78 字节、`.transition` 0xDC 字节（`push 0x78/0xDC; call operator new`）。
- 下一步：由 RTTI CompleteObjectLocator → vtable → 每个 `Command*` 的虚函数（dtor/Execute）；反汇编 Execute 即可得到**参数含义**与**状态写入**（`this+offset` → 引擎状态结构）。无 Ghidra 时可用 objdump/capstone 脚本辅助（本环境无原生 provider）。
- 汉化补丁逐条细节见 `port/docs/07-汉化补丁机制.md` 与 `patch_table_中文.tsv`（含 67 条补丁的 RVA/字节/作用）。
- 优先级：`.stage`(图层/坐标) → `.transition`(type 表) → `.panel/.screen/.frame` → `.effect` 名称表 → `.scroll*/.rotate*/.cutin` → `.HSVfilter` → `.char/.animation`。
- 交叉验证：可用 Wine + 运行时观测（截帧/内存）辅助，但非必需。

## P2 `.ani` 动画格式
- `.ani` 与同名 `.png` 配对（st.paz），未压缩者头 `00 01 02 00`（长度 0x??），压缩者 zlib 后 `00 01 04 00`；解压后比 original_size 多 8 个 0（引擎按 original_size 截断，paz.py 已截）。
- 下一步：解出多帧差分/序列长度，试按"帧数 + 每帧矩形 + RLE/delta"解析；对照同名 PNG 基底还原帧。
- 移植回退：静态显示 PNG 即可。

## P3 UI 布局
- 菜单界面坐标硬编码（exe）+ sys.paz 资源（`configBase.png`、`album*`、`Memories*`、`MusicMode/MusicParts`、`backlogGauge`、`bat*`、`Select*`、`checkmark*`、`thumb/`）。字符/按钮文本经 `DLL` 资源（汉化）或 `LoadStringA`。
- 产品要求：全部 UI 尽量复刻原版。用 REA 读取静态坐标表，以开发期 Wine 截图对齐，导出虚拟 800×600 坐标、状态、九宫格、文字样式 JSON；再重绘 4×/8× 素材。未知坐标保留待确认，不能把框架预览布局当作原版证据。

## P4 文字排版常量
- 关键字串：`fontname`/`symbolFontname`、`!font_FontSize`/`!font_RubySize`/`!font_Color`；字体枚举/选择对话框存在（`EnumFontFamiliesExA`、`fontSelectDialog.png`）。
- 下一步：在 GDI 调用点（IAT：`GetGlyphOutlineA` @VA 0x555FE4?、`GetTextMetricsA` 等；IAT 表起始 0x555FD8 附近，见 re_tools）附近找字号/行距/边距常量；语料统计文本长度可反推换行宽度。
- 回退：参数化（字号 24/26/28、行距 1.2~1.4、左右边距 40~80px）目测微调。

## P5 其他
- 当前剧本全量导入还发现 60 条 `[j]:.wait`、2 条 `[j]:.stage` 与 11 条其它非标准行。原引擎是否执行/忽略这些行尚未确认；框架保留 `<unparsed>` 并在严格模式拒绝执行，不将 Python 旧解析器输出的 `..wait` 当作真实命令。详见 `SCRIPT-RUNTIME.md`。
- `[e]`/`[j]` 变体选择规则（英/日文本模式；汉化版选哪套待验证——可对比两套 timing 与实际汉化文本长度）。
- `system.dat`（`u32 0`+123B 高熵）：非 zlib/加密可识别；移植可忽略/保留。
- `sysparams.txt` 7 个三字母键（etc/hir/kei/kyo/mid/miy/yuk，疑似角色音量/出场配置）。
- `.select` 选项的完整语法（`text:label` 多选项、跨行？）——语料仅 8 处，已够实现。
- r18 覆盖优先级（内容推断，未运行时观测）：移植默认"r18_cn 同名文件覆盖基础归档"。

## 取证环境
早期记录使用 objdump / capstone；当前已可通过 REA 使用 Ghidra 12.1.4，只读分析原版 exe。近期指令语义以专题记录中的静态反编译、汇编和 Evidence ID 为依据；证据导出到本地 `work/re/`，分析结束后 close_binary。静态分析不等于已经观察原版运行画面。

## 2026-10-26 未确认指令推进

`.shakeScreen` 的语料模式和 `.overlay` 已完成静态取证与实现，详见 `SHAKE-OVERLAY-20261026.md`。
待处理：e 变体 scr/100_01.sc:464 特殊转场；j 变体 :454 `.HSVfilter`。
仍未知：T 震屏、负时长震屏的移植行为、原版全局随机种子、高清旋转栅格与原版的运行画面对照。
全剧情严格验证仍未完成，不应把 94 个脚本导入成功解释为所有演出已实现。

### HSV / 掩码转场续进

2026-10-26 `.HSVfilter` 和 type1 mask 已按静态原版证据实现。详见 `HSV-MASK-20261026.md`。e/j 下一实际阻塞变为 WScroll2 / Cutin；type2/14/16/17 等剩余转场仍未实现。此前 :454 / :464 的阻塞记录仅为历史状态。

### Cutin / WScroll2 与制作备注续进

2026-10-26 已实现语料中的 Cutin、WScroll2、effect end，并按原版层序将特效置于边框下；全角分号制作备注经原版loader/parser确认是无指令路径。两变体开场到第一处分支1694消息通过。详见CUTIN-WALKING-20261026.md。下一步检查各分支和type2/14/16/17等转场；普通StageInfo以外的步行配置、原版逐帧像素对照仍未完成。

### type2/14/16/17 特殊转场续进

2026-10-26 按用户核对坐标实现四类转场：type14 左上角 4:3 比例展开、type16 纵向、type17 横向（新图缩放到扩大区域、区域外旧图，完成 801/601/801 严格大于画布边界）；type2 中心缩放+逐帧叠影（200 项缩放表 0x5737e0 逐 bit 重构、前 200 旧图后 200 新图、逐字节 floor 平均、400 完成）。调度 0x53adc0：整数 counter=floor(elapsed_10ms*speed/10)，按 transitionDrawInterval（+0xe0 getter→设置 +0xd0；手册写 10ms 单位、默认5；本机 sysparams=5）门控；`speed>0xff` 时主循环 0x4e11a0 case 0xe 传"立即完成"参数 → 所有类型直接 copy 新图（语料 type17/16 `*400` 为即时切镜，3004b44 修复）。type 分派 switch 同时确认 type3..13→0x539b50、15→0x5399b0、18→0x53a400 仍未实现。详见 SPECIAL-TRANSITION-20261026.md。dev 25/25、ASan 18/18；e/j 1694 基线无回归；8 个转场脚本 24/30 点位真实执行通过，scr/214_01 整本 422 条跑完（含 type2）。下一批阻塞：`.frame`（28行/7脚本）→ `.stage` 逗号位置注解 token（34行/4脚本）→ bg/st 同名立绘歧义（19行/7脚本）→ `.movie`。原版逐帧/逐像素对拍、展开类 5tick 步进、type2 DDA 取样分布（原生闭式 round(i*(src-1)/(dest-1))）与 HD 重采样差异仍未完成。
