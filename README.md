# ef - the first tale · Linux 原生移植

基于 C++20、SDL3、OpenGL 3.3 和 FreeType 的原生框架。
运行时读取离线准备的 `content/`，不依赖 Wine、原版 exe 或汉化 DLL。

目前支持高清图像预览、矢量中文、剧本基础控制流和文字校阅。
原版演出、完整 UI、音视频播放、存档及 KTX2 缓存仍在开发中。

## 构建

需要 CMake ≥3.24、C++20 编译器和 Ninja。Debian/Ubuntu 可安装：

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
    libpng-dev libfreetype-dev libglew-dev libgl1-mesa-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
    libwayland-dev libxkbcommon-dev libdecor-0-dev \
    python3-pil python3-numpy python3-pycryptodome fontconfig fonts-noto-cjk
cmake --preset dev
cmake --build --preset dev
```

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
python3 tools/prepare_content.py
python3 tools/import_scripts.py --game-root "$GAME"
```

已有的高清 WebP 可以放在 `assets/hd/<tag>_4x/` 和 `<tag>_8x/`，
保留归档内相对路径并替换扩展名，然后重新运行 `prepare_content.py`。
该工具只导入少量预览素材，可用重复的 `--image tag/name.png` 指定图片。
默认带原始图像回退；`--hd-only` 要求选中图片已具备 4× 版本。
中文字体默认从本机 Noto Sans CJK SC 复制，也可通过 `--font` 和 `--font-face` 指定。

## 运行

```bash
./build/dev/ef_native --content content
./build/dev/ef_native --content content --validate-content
./build/dev/ef_native --content content --audit-scripts
./build/dev/ef_native --content content --script scr/test.sc --text-preview
```

F11 切换无边框全屏，F10 最大化/还原，Esc 返回/退出。
普通窗口请求 4× 素材，最大化/全屏请求 8×；缺少高档资源会记录回退。
R 可重载图像预览；剧本会话需重启以重新读取内容。

剧本模式支持点击、空格或回车推进，以及数字键/点击选项。
`--text-preview` 明确报告并跳过尚未实现的演出/音频命令，只用于文字校阅。
省略它则采用严格模式，遇到未支持命令会停止。`--variant e|j` 显式选择变体，
默认 `e` 是开发选择，尚未确认原汉化版的选择规则。

## 测试

```bash
ctest --preset dev

# 可选：软件 OpenGL 和真实键盘交互验证，全部使用合成素材
sudo apt-get install xvfb xauth openbox x11-utils libxtst6
xvfb-run -a -s '-screen 0 1600x1200x24' python3 tests/render_smoke.py
xvfb-run -a -s '-screen 0 1600x1200x24' python3 tests/script_window_smoke.py
```

`src/` 是运行时源码，`tools/` 是离线工具，`examples/` 是开发场景，`tests/` 是自动验证。
公开仓库只包含源码、构建配置、测试及必要使用说明。
本地设计文档、逆向记录、资源清单、验证报告、素材和构建产物由 `.gitignore` 排除。
