# 03 — 文字渲染与画面图层模型

## 1. 分辨率与时序
- 逻辑分辨率 **800×600**；`.movie` 以 800/600 参数全屏播放；UI 资源名含 `800x75` 等。
- 时间单位：`.wait` 为毫秒；`.transition`/`.scroll` 等以**帧**计（推断 60fps：`.transition 0 * 10`≈166ms）。`[e]`/`[j]` 两套数值并存（英文慢/日文快），移植需选定一套（汉化版选哪套待验证）。

## 2. 文字渲染管线（exe 证据）
1. 字体：内嵌 TTF 注册（`AddFontMemResourceEx`）→ `CreateFontA`（字号/字符集）。
2. 度量/轮廓：`GetTextMetricsA`、`GetCharWidth32A`、`GetGlyphOutlineA`/`W`（GDI 解析 TTF 取轮廓）。
3. 光栅化/合成：引擎自绘（支持抗锯齿，菜单项「高质量屏幕拉伸/反锯齿屏幕拉伸」→ 渲染到 800×600 表面后拉伸到窗口）。
4. 文本状态：脚本变量 `!font_Color`、`!font_FontSize`、`!font_RubySize`；UI 资源 `fontSelectDialog.png` 表明有字体选择对话框（`EnumFontFamiliesExA` 枚举系统字体）。
5. 汉化：GBK 双字节解码 + 系统 CJK 字体；名字/对话/选项均走同一文本层。

移植建议：FreeType 打开导入到 `content/` 的 CJK 字体；按虚拟 `FontSize × 物理显示缩放` 光栅化；实现字距=字体 advance、行距/边距常量（未知，见 06）；`\n` 换行、`\a` 空文本/推进、`\w` 逐字停顿、`\v` 语音同步；ruby 用 `RubySize` 在基线上方绘制（汉化版基本不用）。

## 3. 图层/画面状态模型（由脚本命令与资源推断）
引擎维护一组"舞台层"，`.stage/.overlay/.screen/.frame` 设置，`.transition` 负责过渡，`.effect/.HSVfilter/.shakeScreen/.scroll*/.rotate*/.cutin` 产生动态效果。观测到的图层：
- **Background**：`bg*.png` / `ago*.png`（可能大于 800×600，支持 `scroll/vscroll/hscroll` 平移）。
- **Tachie（立绘）**：`st_*.png`（+`.ani` 动画），`.stage` 的 4/6/8 参数变体表明可同时挂 1~3 组 `(file, x, y)`。
- **Overlay/Screen 色调层**：`or_*.png` / `sc_*.png` 是 1×601 竖向渐变色带，横向拉伸使用，不是前景角色/场景图片；HD 应重采样或程序化重建，不做 AI 超分。`.screen`精确语义已在SCREEN-20261026.md取证：下一stage材质，双参数背景/前景、三参数另含边框，底对齐逐RGB滤色并保留alpha；`.overlay`仍待确认。
- **Frame**：边框 `finder.png`（`finderNumber.png` 用于章节数字显示）。
- **Text**：消息窗口（`msgPanel.psd` 派生 UI）+ 名字框 + 选项（`SelectActive/Focus/Blur.png`）。
- **Movie**：全屏视频层（800×600）。
- **Menu/系统 UI**：sys.paz（configBase, album*, Memories*, MusicMode/MusicParts, backlogGauge, fade*/TRANS* 过渡遮罩, checkmark, bat* 音量条, thumb/ 存档缩略图框…）。

推断的 `.transition` 实现：用 `fade*.png`（Cross fade/FadeSide/FadeCircle/FadeSwirl…）作遮罩做淡入淡出；type 数字对应不同遮罩/方向（**type 表未知**，需 Execute 或运行观测）。

## 4. 移植实现建议
- 渲染：SDL3/OpenGL，800×600 **逻辑坐标**直接映射到物理像素的 4:3 视口，避免先降到 800×600 位图损失清晰度；保持 60fps 逻辑帧。普通窗口选 4×，最大化/Borderless 选 8×，与屏幕分辨率无关。已实现框架见 `NATIVE-RUNTIME.md`。
- 分层：`std::vector<Layer>`，每层 `texture, x, y, alpha, blend, z`；`.stage` 变体按"1~3 组 file+pos"解析即可覆盖语料。
- 过渡：先实现**交叉淡入淡出 + 黑场**覆盖多数 `.transition`；其它遮罩类型用 sys.paz 的 fade*.png 补齐。
- 文字：FreeType 字形缓存（按 codepoint+px 缓存位图）；GBK→UTF-8 在离线导入时完成，运行时严格读取 UTF-8（不要在转码前解释反斜杠，GBK 尾字节可能为 0x5C）。
- 音频：SDL_AudioStream + stb_vorbis；BGM 循环、SE 多通道、voice 单通道（`.playVoice` 停止语义）。
- 视频：离线重制为时长/帧率一致的 H.264（建议 4K），运行时用 FFmpeg 解码 `content/` 内的视频。

## 5. 未知（影响保真但不阻塞 M1-M3）
UI 布局坐标、文字排版常量、`.transition` type 表、`.effect` 名称→实现映射、`.frame/.HSVfilter` 精确语义（`.panel/.screen`已见后续专项取证）、`.ani` 帧结构、`[e]`/`[j]` 选用规则。

## 6. 汉化补丁对文字引擎的改造（移植复刻清单，证据见 analysis/dll/DLL_ANALYSIS.md）
汉化不是"换文本"这么简单：`ef_first_cn_AA.dll` 在 `DllMain` 里对 exe 内存打了 **67 处补丁**，但**移植版不需要复刻这些补丁**——按下列语义在引擎层原生实现即可：

1. **编码从 Shift-JIS 改为 GBK/CP936**：15 处前导字节上界 `0x9F→0xFE`；字节→字形码映射重写（`0x4CA6D0`，跳过 SJIS 特例 0x8140/0x8165、改为双字节合并）；`MultiByteToWideChar` 代码页 `1252→936`、输入长度 `1→2`（`0x525030`）；新增 GBK 双字节字符读取 hook。→ 移植：**GBK→UTF-32，再交给 FreeType**；文本仍按"字符（码点）"排版。
2. **字体**：唯一字体数据补丁是把默认字体名 `"Merriweather MG"` 改为 GBK `"黑体"`（SimHei，5 字节补丁）；内置 Latin-only TTF 仍被 `AddFontMemResourceEx` 加载，但默认 face 变为系统黑体 → 汉化版全部文字（含拉丁）都用黑体渲染。→ 移植：用**可配置 CJK 字体**（建议 Noto Sans CJK SC；黑体/汉仪文黑是商业字体）。
3. **宽度/断行**：CJK 双字节宽度与禁则处理由字体度量 + 引擎原有行宽决定；移植需实现 CJK 字宽（advance）+ 行首/行尾禁则。
4. **UI 串/对话框本地化**：原版从 exe 资源取英文字符串；汉化版把 `LoadStringA`/`DialogBoxParamA` 的 hInstance 换到 DLL，读取 DLL 内中文 `RT_STRING`/`DIALOG`。→ 移植：把 UI 串/对话框**外置为本地化资源**（JSON/文本），不要学 IAT hook。
5. **目录重定向**：存档目录写 `save_cn\`（`0xC3735` 钩子），r18 补丁检测 `.\r18_cn\` 存在（`0xBDC8B` 钩子，替代注册表 `HKLM\SOFTWARE\minori\ef_first_en_r18patch\InstallDir`）。→ 移植：用配置项 `save_dir` / `r18_patch_enabled`。
6. **字体的枚举过滤**：汉化版修改 `EnumFontFamiliesExA` 回调（`0x13F150/0x13F16A`），影响字体选择对话框列表。→ 移植：字体选择只列可用 CJK 字体。

结论：**移植版不需要 `ef_first_cn_AA.dll`**，也不需要复刻 67 处内存补丁；只需要一个 GBK+CJK 文本栈。


## 2026-10-25 消息窗取证补充

默认字号22、ruby10、行高32；panel1为底部消息窗，panel3为75px上下黑边与下方字幕区。
最新坐标、证据和未定项见 MESSAGE-UI-20261025.md；不要沿用开发叠层坐标。
文字转义已确认：a正文完成后自动推进，w固定100ms暂停，v等待语音；旧“空文本/逐字停顿”仅为早期猜测。
