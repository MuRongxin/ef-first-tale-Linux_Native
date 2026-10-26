# ef - the first tale · Linux 原生移植

基于 C++20、SDL3、OpenGL 3.3 和 FreeType 的原生框架。
运行时读取离线准备的 `content/`，不依赖 Wine、原版 exe 或汉化 DLL。

目前支持全量资源导入与引用检查、高清图像预览、FreeType 矢量中文、剧本基础控制流和文字校阅。
严格模式已接入基础场景、转场、滚动与语音/BGM/音效；完整演出、原版 UI、视频播放、存档及 KTX2 缓存仍在开发中。

## 构建

需要 CMake ≥3.24、C++20 编译器、Ninja 和 Python ≥3.11。Debian/Ubuntu 可安装：

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
    libpng-dev libfreetype-dev libglew-dev libgl1-mesa-dev libegl-dev \
    libasound2-dev libpipewire-0.3-dev libspa-0.2-dev libpulse-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
    libwayland-dev libxkbcommon-dev libdecor-0-dev \
    python3-pil python3-numpy python3-pycryptodome fontconfig fonts-noto-cjk fonts-dejavu-core ffmpeg
cmake --preset dev
cmake --build --preset dev
```

音频使用 stb_vorbis 解码 OGG，支持原版 WAV；缺少系统 stb 时下载固定提交并校验哈希。
优先使用系统 SDL3 ≥3.4.18、libwebp ≥1.3、nlohmann_json ≥3.11；
缺少时下载固定版本并校验 SHA-256。安装了符合版本要求的依赖后，可以使用
`-DEF_FETCH_DEPENDENCIES=OFF` 禁用下载。`release` 预设用于发布构建。

## 准备自己的数据

仓库不包含游戏素材、字体副本或生成清单。以下命令从用户自己的游戏目录生成本地数据，
不会修改源目录；请在本仓库根目录执行，并将路径改成实际位置。

```bash
GAME='/path/to/ef - the first tale'
python3 tools/toc_scan.py --game-root "$GAME" \
    --out work/toc/entries.jsonl --stats work/toc/stats.json
python3 tools/extract_assets.py --game-root "$GAME" \
    --out-dir assets/raw --manifest-dir assets/manifest --jobs 4 --verify-reference 3
python3 tools/build_image_inventory.py --project-root . --jobs 4
```

已有的高清 WebP 可以放在 `assets/hd/<tag>_4x/` 和 `<tag>_8x/`，
保留归档内相对路径并替换扩展名；超宽图可使用 PNG。
H.264 视频放在 `assets/hd/video/`，保留原视频文件名主干，容器使用 MKV/MP4。

```bash
python3 tools/prepare_content.py --all --game-root "$GAME" --jobs 4
```

全量模式导入图片、UTF-8 剧本、原版 OGG/WAV 与已有的高清视频。
默认校验文件哈希、图像格式和尺寸；加 `--decode-images` 可完整解码全部图像。
包内使用独立副本和内容哈希去重，失败时保留原入口清单；初次打包需要额外磁盘空间。
导入报告由 `content/manifest.json` 的 `import_report` 字段指向，记录原图回退、缺失及同名歧义。
补丁仅覆盖同一归档类别；不同类别的同名异图保留歧义，资源解析会拒绝猜测。

只准备小型预览时，可省略 `--all`，用重复的 `--image tag/name.png` 指定图片，
然后运行 `tools/import_scripts.py --game-root "$GAME"` 导入剧本。
已有全量包须继续使用 `--all` 更新，避免预览命令覆盖资源索引。
默认带原始图像回退；`--hd-only` 严格要求每张选中图片具备 4× 版本。
中文字体默认从本机 Noto Sans CJK SC 复制，也可通过 `--font` 和 `--font-face` 指定。

## 运行

```bash
./build/dev/ef_native --content content
./build/dev/ef_native --content content --validate-content
./build/dev/ef_native --content content --audit-scripts
./build/dev/ef_native --content content --audit-resources
./build/dev/ef_native --content content --script scr/test.sc --text-preview
# 严格执行已导入的开场
./build/dev/ef_native --content content --script scr/000_01mugai.sc
```

F11 切换无边框全屏，F10 最大化/还原，Esc 返回/退出。
普通窗口请求 4× 素材，最大化/全屏请求 8×；缺少高档资源会记录回退。
R 可重载图像预览；剧本会话需重启以重新读取内容。

剧本模式支持点击、空格或回车推进，以及数字键/点击选项。
`--text-preview` 明确报告并跳过尚未实现的演出/音频命令，只用于文字校阅。
省略它则采用严格模式，遇到未支持命令会停止。基础场景支持背景裁切、默认居中落底的立绘、
无掩码 type-0 转场、背景滚动与等待；音频支持独立 BGM/语音/SE/SE2 通道和整曲循环。
文字支持逐字显示、点击快显、`\n` 换行、`\w` 暂停100ms、`\v` 等待语音结束、`\a` 在正文完成后自动推进。
`--text-speed 40` 是开发速度（字符/秒），`0` 为瞬显；尚未校准原版速度配置。
Snow/SnowH 使用内容包中的三种原版雪花精灵，支持效果淡出、清除和跨场景持续。
`.pragma disable_control` 段落禁用手动推进，正文和语音结束后继续；enable_control 恢复操作。
已显式兼容归档中 `[j]:.wait` / `[j]:.stage` 写法，执行时报告兼容诊断并保留源行号。
全量包已接入取证得到的普通消息窗与电影字幕布局，使用原版面板透明度和推进动画；
中文按完整正文预排版后逐字揭示，支持标点禁则与长消息上移。字体按实际显示像素重新光栅化，缩放不改变逻辑换行。
布局来自 `examples/message.ui.json`，导入后运行时只读 `content/`。已有包可仅更新布局：

```bash
python3 tools/import_scripts.py --refresh-ui --out content
```

更新后重启剧本预览。小型包缺少布局或资源目录时仍使用开发叠层。
全屏文本按 `script + L0` 分组续写，保留段落、空行和姓名列；新 L0 或离开模式会清页。
面板切换按原程序的 10 毫秒时钟交叠淡入淡出，默认约 160 毫秒：旧面板及文字逐渐退出，
新面板先显示空底图，切换完成后再开始下一条消息、语音、场景指令或脚本等待。
重复相同模式不增加等待，也不清空全屏续写；切换期间点击不会跳过后续对白。
已覆盖原数据全部8段、35条全屏对白，两种变体均无排版溢出。显式分页控制与自动分页器尚未接入，过长页面会明确报错。
目前仍需通过原版截图校准字体基线、描边及整体视觉对齐。

运行时支持自定义矢量字体（TTF、OTF、TTC），无需重新导入资源：

```bash
build/dev/ef_native --content content --script scr/100_01.sc --silent \
    --font "/绝对路径/自定义字体.ttc" --font-face 0 --font-size 110
```

也可直接把字体文件拖入游戏窗口即时应用。`F5` 循环切换无效果、阴影、描边加阴影；`F6` 切换集合中的字面，`F7` / `F8` 按 5% 调整字号
（75%–150%），`F9` 恢复随包字体、默认字号和文字外观；`--font-default` 提供相同的启动选项。
更换字体不会推进当前对白。缺字逐字回退到随包中文字体，消息过长时会自动缩小以适配区域，
因此实际字号可能低于设定值。字体仍按窗口物理像素光栅化，4×/8× 素材切换不影响字体选择。
字体文件由用户自行提供，仅保存绝对路径和字面索引，不会复制进资源包。

配置自动保存到 `$XDG_CONFIG_HOME/ef-native/font.json`（默认 `~/.config/ef-native/font.json`），
可用 `--font-config FILE` 指定独立配置。已保存字体不可用或配置损坏时会提示并使用随包字体；
运行中选择无效字体会保留当前设置。无头校验不写配置。

`--text-outline 1 --text-shadow 1 --text-baseline -1` 可分别调整描边宽度、右下阴影距离和基线偏移
（虚拟像素；宽度 0–4、阴影 0–8、基线 -8–8）。描边由 FreeType 轮廓扩张生成，按物理显示倍率光栅化，
各效果保持原有换行、逐字时序和推进标记位置。阴影和描边分别在全部前景文字之前绘制，避免覆盖邻字。
这些选项也随字体配置保存，支持旧版配置；基线校准数值不代表已完成原版截图对齐。

外置场景的文本层、消息窗布局根对象也可配置 `appearance`，用户设置优先：

```json
"appearance": {
  "outline_width": 1,
  "outline_color": [0, 0, 0, 1],
  "shadow_x": 1,
  "shadow_y": 1,
  "shadow_color": [0, 0, 0, 0.65],
  "baseline_offset": 0
}
```

省略 `appearance` 时沿用现有无效果外观，`--font-default` / `F9` 会恢复布局定义的外观。


BGM、SE、SE2 支持跨文件交叠淡化：旧声部按自己的包络继续退场，新声部同时淡入，剧本无需等待。
同一首 BGM 调音量时保留播放位置；同文件循环音效不重播，非循环音效再次触发时从头播放。
SE 循环参数遵循原版首字符规则，`t` / `true` 开启循环，其余值为不循环。
脚本停止仅作用于当前声部，之前已退场的尾音保留原包络；停止后同文件重播会启动新的声部。
混音器最多保留 10 个活动声部，超过容量时明确报错并保留已有播放状态。
旧声部数据由控制线程回收，实时回调不进行解码、内存分配或 PCM 释放。
音量按采样连续插值，仍待与原版离散音量步长进行波形对照。

`.screen` 与 `.overlay` 支持双参数（背景、前景）和三参数（边框、背景、前景），`*` 清除对应滤色，
双参数也会清除省略的边框滤色。指令只更新下一次 `.stage` 的材质，当前画面与旧场景快照保持各自的状态。
色带按源图底边对齐，顶部不足时延展首行；先处理源图，再裁切和合成，支持 601/611/621 行色带及 1200 高背景。
GPU 按原生定点公式依次计算 HSV、overlay、screen 并保留 alpha，独立色带缓存不会占用普通图像缓存预算；文字和 UI 不受影响。
`.shakeScreen` 已支持随机平移（含数字默认模式）、水平/垂直交替、裁切放大交替与旋转叠影，
包含消息窗的画布会被冻结，按 10ms 时长单位逐次完成；未确认的 T 模式和负时长继续报错。
震屏使用固定随机种子；高清栅格与原版 800×600 整数栅格尚未做逐像素对照。
`.HSVfilter` 的六参数分别作用于下一场景的背景、前景，边框与文字保持各自样式；全零清除。
已支持 type1 掩码转场（800×600 遮罩），包含原版反色和整数混合，时长约为 `51.2 / speed` 秒。
`Cutin` 已支持滑入、停留和滑出；负停留时间等待 `fadeout`，可选参数控制方向与加速。
`WScroll2 * 60 -2` 已支持两层循环背景、远近速度差和边框起伏，`effect end` 停止并保留最后画面。
步行效果采用普通 StageInfo 路径及固定采样，其他配置分支与原版逐帧对照仍待完成。
已兼容原版遗留的全角分号制作备注；其他未知文本和指令仍严格报错。
两种变体现在均从开场严格通过 **1694 条消息**，到达第一处分支选项；各分支及剩余特殊转场仍待验证。
普通图像缓存上限为 512 MiB，可容纳两张高清步行背景与 8× 角色边框。

特殊转场、立绘位置标志及其余未实现特效仍会停止。
雪花运动与淡化已按原程序重建，随机种子采用固定值，尚未完成逐像素对照。
正常游玩直接使用默认音频输出，启动日志会显示实际 SDL 后端和设备名称。
编译 SDL 前须安装上面的音频开发依赖；只有运行时音频服务而缺少开发头文件，可能构建出仅含 `dummy` / `disk` 的 SDL。
补齐依赖后执行 `cmake --preset dev`、`cmake --build --preset dev` 重新检测并编译；音频错误会列出已编入的后端。
`--silent` 用于主动静音或无头检查：不打开音频设备，继续推进音频时钟；无头 `--trace-messages N` 自动采用此行为。
`--start-line N` 可从源文件行号开始验证片段，不恢复此前的变量、场景或特效。
完整数据包的开场可用 `--script scr/000_01mugai.sc --trace-messages 428` 作无头检查；
覆盖开场427条对白和下一章首条对白，两种变体均已通过。无头检查会快进时间，不能代替画面和音频保真对照。
`--variant e|j` 显式选择变体，
默认 `e` 是开发选择，尚未确认原汉化版的选择规则。

`--audit-resources` 检查两种变体及补丁剧本的图片、语音、BGM、音效、视频、脚本引用和包内文件。
存在缺失、歧义或丢失文件时退出码为 2；导入成功不代表场景演出已经实现。

## 测试

```bash
ctest --preset dev
# 安装 xvfb/xauth 后，CTest 也会执行整场景合成像素检查。

# 可选：软件 OpenGL 和真实键盘交互验证，全部使用合成素材
sudo apt-get install xvfb xauth openbox x11-utils libxtst6
xvfb-run -a -s '-screen 0 1600x1200x24' python3 tests/render_smoke.py
xvfb-run -a -s '-screen 0 1600x1200x24' python3 tests/script_window_smoke.py
xvfb-run -a -s '-screen 0 1600x1200x24' python3 tests/message_ui_smoke.py
xvfb-run -a -s '-screen 0 1600x1200x24' python3 tests/nvl_window_smoke.py
SDL_VIDEO_FORCE_EGL=1 xvfb-run -a -s '-screen 0 1600x1200x24' \
    python3 tests/render_smoke.py --out build/render-smoke-egl
```

`src/` 是运行时源码，`tools/` 是离线工具，`examples/` 是开发场景，`tests/` 是自动验证。
公开仓库只包含源码、构建配置、测试及必要使用说明。
本地设计文档、逆向记录、资源清单、验证报告、素材和构建产物由 `.gitignore` 排除。
