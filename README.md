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
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
    libwayland-dev libxkbcommon-dev libdecor-0-dev \
    python3-pil python3-numpy python3-pycryptodome fontconfig fonts-noto-cjk ffmpeg
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
目前仍需校准原版字体基线/描边和面板切换动画；全屏文本的跨消息连续累积也尚未复刻。
特殊转场、立绘位置标志、其他特效和跨曲交叠淡化仍会停止。
雪花运动与淡化已按原程序重建，随机种子采用固定值，尚未完成逐像素对照。
`--silent` 不打开音频设备，继续推进音频时钟；无头 `--trace-messages N` 自动采用此行为。
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
SDL_VIDEO_FORCE_EGL=1 xvfb-run -a -s '-screen 0 1600x1200x24' \
    python3 tests/render_smoke.py --out build/render-smoke-egl
```

`src/` 是运行时源码，`tools/` 是离线工具，`examples/` 是开发场景，`tests/` 是自动验证。
公开仓库只包含源码、构建配置、测试及必要使用说明。
本地设计文档、逆向记录、资源清单、验证报告、素材和构建产物由 `.gitignore` 排除。
