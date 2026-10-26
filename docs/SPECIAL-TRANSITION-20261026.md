# 特殊转场 type 2 / 14 / 16 / 17 取证与实现

> 日期：2026-10-26
> 目标：原版 `ef_first_en_AA.exe`（musica，PE32/i386，SHA-256
> `9824732313d13136cb2300f33038a655ba480ae01cefc2749028d1b1748b4f9e`），REA + Ghidra 12.1.4 只读分析。
> 用户已核对坐标语义：type14 从左上角按比例展开、type16 纵向展开、type17 横向展开（完整新图缩放到逐渐扩大的区域）；type2 为中心缩放 + 逐帧叠影，需要保留历史帧。本文件把核对结论与证据、实现、测试边界固定下来。

## 1. 结论摘要

| type | 原生语义 | 完成条件（counter） | 实现 |
|---|---|---|---|
| 14 | 左上角锚定、4:3 同步放大矩形的区域展开；矩形外显示旧图 | `counter > 800` → 801 | `transition_region(14,s)={0,0,s,s*600/800}` + 旧图垫底 GPU 分支 |
| 16 | 纵向展开：整宽、高度增长 | `counter > 600` → 601 | `{0,0,800,s}` |
| 17 | 横向展开：整高、宽度增长 | `counter > 800` → 801 | `{0,0,s,600}` |
| 2 | 中心缩放 + 逐帧 1/2 叠影（保留历史帧）；前半旧图、后半新图 | `counter >= 400` | 历史 FBO 乒乓 + `zoom_transition_region` |

统一时钟（证据 402/410 的调度器 0x53adc0）：

```
counter = (int)((elapsed_10ms_ticks * speed) / 10)        // 原生为整数、截断
每次更新间隔 = gfx.transitionDrawInterval 默认 5 → 5 个 10ms tick = 50ms
```

因此 `presentation.progress()` 采用同样的 `ticks*speed/10` 整数 step（所有类型一致；type0/type1 的 update 函数同样消费整数 `DAT_005df330`）。
更新节拍证据链（本轮独立复核补强）：
- `0x53adc0` 门控：`elapsed - last < (**(code**)(DAT_005df338+0xe0))()` 时不更新 counter、不执行 update；
- 运行期 vptr=`0x5792e4`（构造 `0x4bd8df`；`0x436e80` 的 `0x57940c` 是初始/退化表）；`+0xe0 = 0x436db0`（`mov 0xd0(%ecx),%eax; ret`），`+0xe4=0x436dc0`（`0xd4`）、`+0xe8`（`0xd8`）；
- 设置序列化 `0x4bca7a`：`0xd0(%esi)` ↔ key `0x579214`="transitionDrawInterval"，`0xd4` ↔ "scrollDrawInterval"，`0xd8` ↔ "shakeDrawInterval"；
- 官方手册 `analysis/dll/chm_extract/300_techinfo.htm`：transitionDrawInterval = "Minimum Interval between screen transition. (in units of 10ms)"，默认 5。
故原生最多每 5 tick（50ms）更新一次；speed=20 时 counter 每次 +10，与 renderer 的 `sample=step/10`（phase 10,20,…,390，共 39 次）一致。
另：原生主循环对 `speed>0xff` 会向 dispatcher 传“立即完成”参数（0x4e11a0 case 0xe → 0x53adc0），
**所有类型**都直接 `copy(新图→画布)+done`（0x53ae6f..0x53ae8b）；语料 `scr/102_04.sc:52/88` 的
type17/16 `*400` 因此是即时切镜。移植版 `fading_ = active_speed_ <= 255` 已按此实现（含新增边界测试）。

## 2. 证据索引（work/re/evidence/）

| 地址 | 内容 | Evidence ID |
|---|---|---|
| 0x4df4e0 | 转场命令执行：`type=+0x24c, speed=+0x26c`，重置 `DAT_005ac8d4` 时钟 | `ev_634ee1d8b82e79004d41627ae7506d89e478998f471bd729e23bfbabe6204618` |
| 0x53ade4 | 更新调度器：counter 公式、5tick 门控、type→update 函数分派 switch | `ev_a1352de7259f2dbb762fb48f7fe6b65636f29edb02d1e9f759cdd83e10752b4e` |
| 0x53ac80 | type2 更新：<200 用旧图、否则 `399-counter` 查表用新图；50% 叠影 | `ev_e1107b27b425812741e18c0aaa7dafe0fbbd909faab888d4d200574196ce2471` / 汇编 `ev_0105ab3d475f6a4d4efd3d93fc7c510429ec021de9cc0a361b91f41ac1022b40` |
| 0x539ac0 | type14 更新：矩形 `{p, p*[0x5dcf34]/[0x5dcf30]}`，`p>[0x5dcf30]` 完成 | `ev_e6187c5835c44167963cd4d26d2cf42d9e0a6f2adda4776f3bdbb34d7af2d0fc` / 汇编 `ev_9dbef580e3b5949e2072ff5cb30617e0e6568ef35d18d79b5d4cdef0f0648aa5` |
| 0x539920 | type16 更新：矩形 `{800,p}`，`p>[0x5dcf34]` 完成 | `ev_a05f547db7c173fd32d27c0f3800450814a5ea8a83647f97251a9b8f02cb41c4` / 汇编 `ev_8f361faa5eb6fc382128f8b8a8f8d70841bd5da8abd8ca0667a3234b05c078bd` |
| 0x539890 | type17 更新：矩形 `{p,600}`，`p>[0x5dcf30]` 完成 | `ev_152783f51306fe44b10abbdb563f80ae98f8b76eaab1de02306052a9fa461f84` / 汇编 `ev_38b12f920042e00cbc6efb29eb3189f9e3d51ff3037ae63630b61e4f3c3434cd` |
| 0x51ecf0 | 把新图按矩形缩放盖到显示缓冲（旧图不动的来源） | `ev_5e13ec8b667d691956142830e9acff11730649c601ec4b67af5e2f9e42b34698` |
| 0x51d4c0 | 最近邻增量表：dest i → `trunc(i*src/dst)` | `ev_4bc87df53d0be8098f3850737e2ac7256adb75f5730b2edf5e2ec809a1a72188` / 汇编 `ev_fbce5adc8900c1068b3f3ed1adc63364011afdfa5f3782b5aa930be9cb6ce985` |
| 0x51fe80 | type2 单步：`W=trunc(minW/scale), H=trunc(minH/scale)`，居中裁剪并铺满临时图 | `ev_8918f4a9b41556981cb4c05798818f9f6d28acc426eda5abba727ed998836b1f` / 汇编 `ev_26edfbc18070acd3bb725a431cc9f57d04df54ec6312342000512423e5da90c7` |
| 0x51f290 | 最近邻缩放 blit（Bresenham 行列增量，dest→source 覆盖） | `ev_930bf898772fc7ce465e93d0ed6a7e439c759983f371dbee362d3fa547191f31` |
| 0x51f030 | 历史叠影：逐字节 `floor((old+new)/2)`（`((a^b)&0xfefefefe)>>1 + (a&b)`） | `ev_9032679fd8f25eade997e3e11b2f14c9d8843b2e1c455f694d77e57e134619ef` |
| 0x5737e0 | type2 缩放查表 200 个 double | `ev_4ad9a55fdf493cc86297ecd46287cd26601828056eec890a99286e7d84f98daa` |
| 0x436db0（vtable 0x5792e4+0xe0） | getter：返回设置对象 `+0xd0` | 直接 objdump（`work/re/verify/direct-objdump-notes.txt`） |
| 0x4bca40 | 设置序列化：`[obj+0xd0]` ↔ "transitionDrawInterval"(0x579214)；`+0xd4`↔"scrollDrawInterval"；`+0xd8`↔"shakeDrawInterval" | 直接 objdump |
| chm `300_techinfo.htm` | 手册：transitionDrawInterval = 最小转场间隔（10ms 单位），默认 5 | 原版英文手册（本地） |
| 0x436799 / 0x4367a3 | 初始化写 `[0x5dcf30]=0x320`(800)、`[0x5dcf34]=0x258`(600) | `ev_153f353420b1609c0aeba03e277e2eb4ba6100e507b3c6863d3303e569edb95e` |
| 0x5dcf30 静态 read_bytes | 该地址为 BSS，静态读值全 0，**不能**当常量证据 | `ev_256e1760cea3f35449b652d1a729061408cc98952cf591c6c23d5db0ad8a6128` |

补充：
- 独立复核报告：`work/re/transition-semantics-verify-20261026.md`（含复算脚本 `work/re/verify/*.py`、
  直接反汇编摘录、2 份新 REA 证据）。
- `0x4bec73` 初始化三个 800×600 缓冲：`0x5df30c`(旧)/`0x5df310`(新)/`0x5df318`(显示历史)，
  因此 type2 的“历史帧”是显示缓冲本身；展开类在旧显示缓冲上盖新图。证据 `ev_d5bb7f5bf957c91c64f9f5e5311287f86f029f93e59cb23963e86a1a89ca8ab5`。
- `0x539720` 转场开始/重置时把 counter、计时、done 标志清零。证据 `ev_53a162be8be6977ce83ca6985dff543aec2bc0882fd2020024f988f1264004c6`。
- 调度器 switch 同时确认了仍未实现的类型：`3..13 → 0x539b50(type-3)`、`15 → 0x5399b0`、`18 → 0x53a400`。
  这些不在本轮范围（用户只核对了 2/14/16/17 的坐标），移植版继续严格拒绝。

## 3. 数值重建

### 3.1 type14/16/17
- `[0x5dcf30]=800`、`[0x5dcf34]=600`（画布尺寸；由初始化函数 `0x436799/0x4367a3` 写入坐实，见 §2；
  直接 read_bytes 该地址为 BSS 0，不可用）。
- type14 矩形 = `{0,0,p, p*600/800}`（整数除法，p=0..800）；完成当 `p>800` → 第 801 个 counter 才走“完整新图”。
- type16 矩形 = `{0,0,800,p}`，完成 `p>600` → 601。
- type17 矩形 = `{0,0,p,600}`，完成 `p>800` → 801。
- 区域外像素来自上一帧显示缓冲（旧图），新图仅在矩形内按 `dest i → trunc(i*src/dst)` 最近邻缩放。

### 3.2 type2
```
counter < 400：
  idx = counter<200 ? counter : 399-counter
  scale = DOUBLE[200] @0x5737e0[idx]
  W = trunc(minW/scale), H = trunc(minH/scale)      // minW=800, minH=600
  temp = 最近邻把 source 的居中 W×H 窗口铺满 800×600
  history = floor((history+temp)/2)  // 逐通道逐字节，含 alpha 字节
  source = counter<200 ? 旧图 : 新图
counter >= 400：显示 = 新图
```
表值精确等于 `float("%.10g" % (1+99*(i/199)^2))`（200/200 个值均一致）；
`transition.hpp` 用同式 10 位有效数字舍入后取整宽高，单元测试固定检查 0/10/100/199/250 等采样点。

## 4. 实现映射（公开源码，已提交）

- `src/scene/transition.hpp`（新）：`supported_transition` / `transition_completion` / `transition_region` /
  `zoom_transition_region` 纯函数，严格抛错。
- `src/scene/presentation.cpp`：`.transition` 校验（type 集合、type1 mask 只能配 `*`、type2 speed 必须 20）；
  `progress()` 统一整数 `step=floor(ticks*speed/10)`（所有类型；原生 counter 为整数），active type/speed/mask
  在 `.stage` 冻结，pending 修改不影响进行中的时钟（边界测试覆盖）。
- `src/render/renderer.cpp`：fragment shader 增加
  - type2：中心窗口采样 + `old` 历史图 1/2 平均；
  - type14/16/17：矩形外取旧图、矩形内对新图做最近邻映射；
  - CPU 侧 type2 用两块 800×600 逻辑坐标的 ping-pong FBO，按 10 counter 单位（=5 tick）推进叠影，
    并处理 rebuild / cache reset / history rewind / progress=1 冻结快照。
- `src/app/application.cpp`：把 `presentation.transition_type()` 传给 renderer。

## 5. 测试与验收

- `tests/transition_tests.cpp`（ctest `transition_clock`）：
  - 纯函数：方向/宽高比/整数舍入、完成值 256/512/400/801/601、type 集合、非法 type；
  - 时钟边界：speed200 在 counter=800（type14/17）/600（type16）仍 busy，+1 后完成；
    speed>255 对**所有类型**立即完成（原生 0x4e11a0 case 0xe `setg` 把 `speed>0xff` 作
    dispatcher 参数 → 0x53adc0 直接 copy(new→canvas)+done；语料 2 处 type16/17 `*400` 即即时切镜）；
    speed1 精确在 80.1s/60.1s 完成（严格大于画布边界）；
    speed12 验证 `floor(ticks*speed/10)` 的整数截断；type2 398→busy、400→完成；
    pending type 隔离；脚本消息严格等转场完成后才开始；
  - 严格拒绝：type3/非20的type2/type16带目标/mask 尺寸等。
- `tests/transition_render_tests.cpp`（ctest `transition_render`，Xvfb + 软件 GL）：
  - type14/16/17 全屏渐变色非整除最近邻逐像素对照（含 0/1/2/17/24/79/299/edge-1/edge、完成帧）；
  - type2 逐采样历史叠影、同采样保持、冻结新场景、cache reset 重放、视口偏移+8× 档位、历史回退、最终帧。
- 结果：dev ctest **25/25 PASS**（含上述两项）；core ASan ctest **18/18 PASS**（commit `b3a0b2e`，
  整数 counter 统一后的 `aba8c78` 复跑仍为 25/25、18/18）。
- 真实内容严格 trace（`work/runtime/transition-trace-20261026/`，e/j 两变体，commit `b3a0b2e` 二进制）：
  - 基线无回归：从开场两变体各 **1694 消息 PASS**，与旧日志逐字节/逐消息一致，停在 `scr/102_01.sc:217` 首个选项。
  - 特殊转场 **24/30 个点位真实执行**并通过（type2/14/16/17 全覆盖、e/j 两套速度）：如
    `scr/216_04.sc:1` type14、`scr/214_01.sc:171` type2 与 `:470` type14、`scr/102_04.sc:52/59/88/94`
    type17/16、`scr/104_01.sc` type16/17、`scr/212_01/211_01/208_01` type14；每点均验证“前/后消息继续、
    紧随 `.stage` 激活、无控制流分流”。未到达的 6 点均属 `scr/202_01.sc`，被已知 bg/st 同名立绘歧义挡在 `:106`。
  - `scr/214_01.sc` 整本 422 条消息跑完（含 type2），chain 后停在 `215_01:137 .frame`（未实现）。
  - speed>255 修复（3004b44）后复跑 `scr/102_04.sc` **1000 条消息 PASS**（越过 :52/:88 的 type17/16 `*400` 即时切镜，继续到 `scr/105_01.sc`）。
  - 因此下一批阻塞命令（按到达顺序）：`.frame`（28 行/7 脚本）→ `.stage` 逗号位置注解 token（如
    `600,75`，34 行/4 脚本）→ bg/st 同名立绘歧义（19 行/7 脚本，已知未知项）→ `.movie`（8 行）。
- 真实内容窗口/截图抽查（Xvfb + 真实 4× 素材，commit `b3a0b2e`）：
  type14 `scr/216_04.sc` 中途按 4:3 从左上角展开（测得区域 560×420 逻辑、宽高严格 4:3）；
  type17/type16 以 speed60 的 `[e]` 行起跑，中途测得新图分别压缩在 318×600（横展）与 800×318（纵展）区域内、
  区域外为旧图；type2 在旧场景为空时前半段显示黑（旧图）符合“前半旧图/后半新图”模型，无 GL 错误。

## 6. 独立复核结论与已知限制

2026-10-26 独立复核（`work/re/transition-semantics-verify-20261026.md`，复核对象 `b3a0b2e`/
`aba8c78`）：分派映射、completion 400/801/601 与严格 `>`、rect 公式、zoom 表 200/200 逐 bit 一致、
type2 裁剪窗/前后半来源/逐字节 floor 平均/400 完成/新图整帧覆盖 / 画布跨帧累积均已坐实；
`[0x5dcf30]/[0x5dcf34]=800/600` 由初始化函数 `0x436799/0x4367a3` 的 `MOV ...,0x320/0x258`
坐实（静态 read_bytes 该地址为 BSS 全 0，不能作为证据）。复核提出的 R1（type2 采样节奏）经 Lead
追溯 0x53adc0 第一道 5 tick 门控 + `transitionDrawInterval` 设置字段/官方手册后判定为**误判**，
当前 `sample=step/10` 与原生一致；R8（speed>255 应即时完成，含 type16/17）已按复核建议修复并加测试
（`fading_ = active_speed_ <= 255`，对应 0x4e11a0 case 0xe 的 setg 参数路径）。剩余限制：

1. **展开类的更新节拍**：原生 counter 只在 `transitionDrawInterval=5` tick 边界更新；移植版 type2 与之一致，
   但 type14/16/17 的渲染用连续 `progress()` 逐帧推进区域（原生 50ms 步进），完成时刻与原生最多差一个
   更新间隔（<50ms）。属既存时钟近似（type0/1 同样连续），未在本轮改动。
2. **type2 最近邻取样分布**：原生 `0x51f290` DDA 的闭式经逐点模拟确认为
   `src = left + round(i*(cropW-1)/(800-1))`（y 用 599；`round` 半进位；复核者的公式正确，
   Lead 曾另推 `floor((i+0.5)*cropW/800)`，已用 1..800 全 W 模拟证伪），而 shader 用
   `left + floor(i*cropW/800)`；在 speed20 实际用到的 39 个 phase 上约 **22.7% 的取样点差 ±1 源像素**
   （1× 下亦存在）。快速缩放叠影中视觉可忽略，但**不宣称 1× 像素级等价**；展开类使用 `0x51d4c0`
   的 `trunc(i*src/dst)`，1× 下与 shader 等价（已复核 PASS）。
3. **HD/任意窗口**：转场在物理分辨率 FBO 上执行（4×/8×/非整数窗口），native 是 800×600 软件缓冲再拉伸；
   移植版更细，不是“原生结果的简单放大”。这是运行时按物理分辨率渲染的自然结果，尚未做原版逐帧截图对拍。
4. **type2 speed 只允许 20**：原生对 speed 无校验；但 GPU 叠影采样步长按“每 10 counter（=5 tick × speed20/2）”
   实现，只有 speed=20 与 5 tick 门控严格对齐，故保留语料驱动限制（其他 speed 需先推广采样公式）。
5. **显存预算**：type2 需要 4 张全屏 target（16B/px），视口 >约 16.7M 像素（8K 级）会触发 256MiB composite
   预算异常；本机与 4K 及以下不受影响（普通窗口约 1–2M 像素）。
6. **未实现类型**：3..13（`0x539b50(type-3)`）、15（`0x5399b0`）、18（`0x53a400`）；继续严格拒绝。

## 7. 下一步

1. type2 DDA 闭式已定论（`round(i*(cropW-1)/799)`）；若要 1× 逐像素等价，需要把 type2 计算放回
   800×600 虚拟网格再放大（与 HD 重采样策略一起决策），当前保持物理分辨率近似并记录在案。
2. 按 trace 结果补齐下一批指令：`.frame`（28 行/7 脚本）→ `.stage` 逗号位置注解 token（34 行/4 脚本）
   → bg/st 同名立绘优先级（19 行/7 脚本）→ `.movie`。
3. 逆向 `0x539b50/0x5399b0/0x53a400` 的参数与坐标表，按同一模式扩 type 3..13/15/18。
4. 各剧情分支（首个选项之后）继续严格 trace，定位新的未实现命令。
