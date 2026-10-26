# 原生框架与 content v1（2026-10-25）

## 全量资源导入（2026-10-25）

`python3 tools/prepare_content.py --all --game-root "$GAME" --jobs 4` 已实际生成完整本地资源包。
包含 3,758 图像 / 10,008 档位、94 UTF-8 剧本、29 BGM + 89 SE + 6,078 voice、4 个 H.264 视频。
所有 6,250 个已完成高清档均已接入，包括超宽 PNG 与 8× 树中的 4× 内容。
633 张原图回退（mask 26 / UI 93 / bg 1 / thumb 466 / tint 47）明确记录在导入报告，`.ani` 2,226 项未伪装成已支持。

包以内容哈希去重，使用独立副本，不链接外部素材目录；全量导入有写锁、失败保留旧 manifest。
`script_index/resources/import_report` 指向不可变批次索引，最后原子发布 manifest。
默认哈希/格式/尺寸检查，`--decode-images` 可启用全部像素解码；本轮未声称已深度解码全部 HD。
全部图像路径的 PNG/WebP 头部另行核对通过，无包外软链接；M0 原始 PNG 完整解码仍由独立验证器覆盖。

C++ `Content::resource(kind,name,r18)` 优先精确归档名（不区分 ASCII 大小写），没有匹配时才尝试伪扩展名兼容别名；
例如 `iconson.png` 与 `iconson.png-`、`pauseAnime.png` 与 `pauseAnime.png-` 内容不同，不能合并为一个资源。
支持同源别名及显式补丁覆盖门控；
原版 bg/st 同名立绘按 **st 优先、bg 回退** 解析（stage 立绘加载链 `0x4df99c→0x5081a0→0x5068e0`，先查 st 读取器
`0x5e0898`、再回退 `0x5e0628`），移植版 `Content::lookup_resource` 已按此实现。
`--audit-resources` 从包内重新解析剧本，Python/C++ 对 14,942 次 / 9,157 种引用的报告完全一致：
缺失 0、**歧义 0**（原 19，bg/st 优先级取证后清零）、包内丢失文件 0。

全部 6,196 音频已用 `tools/paz.py` 基准重新解出并与包内逐字节比较 PASS，音频没有重编码。
4 个视频用 ffprobe 实测均为 H.264 / 3840 宽，原 AVI/MPEG 引用正确映射到包内 MKV。
CTest 5/5、合成渲染/键盘交互、真实资源 4×→8×→4×、94 剧本审计与 100 条消息追踪均 PASS。
检查从 `/tmp` 指定绝对 content 路径运行，运行时无需 assets 或原游戏目录。
Wayland/EGL 曾触发现有 GLEW 的 GLX 后置检查错误；仅在 SDL 当前 GL 上下文存在且 GLEW 3.3 已加载时接受该特定错误，
不放过其它 GL 初始化错误。X11/GLX、X11/EGL 像素检查和真实 Wayland 启动均 PASS。
报告、日志、预览图全部只在忽略的 `work/runtime/` 与 `build/`。

## 当前交付

`ef_native` 是可编译、可启动的 Linux 原生场景预览程序。它不运行 exe/DLL，
不读 PAZ，不依赖 Wine；只读取指定 `content/` 内的 JSON、PNG/WebP 和字体。
这次完成 M1 的框架和图像路径，尚不是可通关的游戏。

| 模块 | 已实现 |
|---|---|
| `app/` | SDL3 窗口、事件循环、60 Hz 固定步长计时、异常退出与资源释放 |
| `core/` | 800×600 逻辑画布、物理像素视口/留黑、坐标转换、窗口档位规则、严格 UTF-8 解码 |
| `data/` | content v1 清单、路径约束、PNG/WebP 文件头识别及 RGBA 解码、尺寸检查、缺失资源候选回退 |
| `render/` | OpenGL 3.3 Core shader、按场景顺序分层、straight-alpha 合成、256 MiB LRU 图像纹理缓存 |
| `text/` | FreeType Unicode/TTC face、按物理像素光栅化、按字号/码点缓存、简单换行与 kerning、缺字检查 |
| `scene/` | 外置工程预览 JSON：图像/矩形/中文文本；图像默认按 `design_size` 绘制 |
| `tools/prepare_content.py` | 从现有 manifest/raw/hd 准备少量独立预览素材；校验 raw SHA-256、尺寸、损坏文件；原子切换清单 |

普通窗口一律请求 4×；最大化与 Borderless 请求 8×，不根据屏幕像素或 DPI 选择档位。
窗口实际状态通过 SDL flags 读取，不把异步请求成功当成已经完成。F11 全屏始终使用桌面模式。
渲染直接映射到物理分辨率的 4:3 视口，**不先降到 800×600 位图再放大**。

图像预览保留独立入口；新增 `src/script/` 基础控制流与文字校阅，详见 `SCRIPT-RUNTIME.md`。
未知的原版参数和坐标没有被编造成已确认行为。

## 构建

系统依赖（Debian/Ubuntu；只需安装缺少的包）：

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
    libpng-dev libfreetype-dev libglew-dev libgl1-mesa-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
    libwayland-dev libxkbcommon-dev libdecor-0-dev \
    python3-pil fontconfig fonts-noto-cjk
cmake --preset dev
cmake --build --preset dev
```

要求 CMake ≥3.24、C++20。优先使用系统 SDL3 ≥3.4.18、libwebp ≥1.3、nlohmann_json ≥3.11；
缺少时 CMake 下载固定版本 **SDL 3.4.18 / libwebp 1.6.0 / JSON 3.12.0**，使用 SHA-256 校验。
下载/编译均在 `build/`，不安装或修改系统依赖。可额外安装 `libsdl3-dev libwebp-dev nlohmann-json3-dev`
并使用 `-DEF_FETCH_DEPENDENCIES=OFF` 禁用下载；旧发行版没有 libsdl3-dev 时可保留自动下载。
离线也可通过 CMake 标准 `FETCHCONTENT_SOURCE_DIR_SDL3` / `_WEBP` / `_NLOHMANN_JSON` 指向已解压源码。

`dev` 为 Debug，`release` 为 Release，`asan` 为无窗口的核心 ASan/UBSan 构建。
构建默认并发 4，避免占满正在进行资源处理的机器。
Linux 为已验证平台；没有宣称 Windows/macOS 已通过构建。

SDL 版本下限来自实际回归：3.2.28 在 X11/Openbox 全屏窗口退出时曾出现间歇性
`BadWindow / X_TranslateCoords`。3.4.18 的 X11 事件实现包含对此 reparent 竞态的
非致命错误处理，因此固定到这一稳定版本（[上游实现](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/video/x11/SDL_x11events.c)）。

## 启动

```bash
python3 tools/prepare_content.py
./build/dev/ef_native --content content
```

默认导入 `bg/prologue_mugai_e.png` 的原始与已完成 HD 版本，使用 `images.csv` 的尺寸声明；
这是不加 `--all` 的小型预览模式，仅复制选中素材。已有全量包时请用 `--all` 更新，或另选 `--out`。
字体通过 fontconfig 选择 Noto Sans CJK SC，
复制到包内并保留 face index。运行时不依赖系统字体。

```bash
# 另一张背景，加一层立绘；明确使用 tag，避免猜测 bg/st 同名覆盖顺序
python3 tools/prepare_content.py --out work/runtime/another \
    --image bg/bg_up002a01_02.png --image bg/st_yuko11f10-.png
./build/dev/ef_native --content work/runtime/another

# 只带 HD（每张图必须已有 4×），或手动指定中文字体
python3 tools/prepare_content.py --hd-only
python3 tools/prepare_content.py --font /path/to/chinese-font.otf --font-face 0
```

F11：全屏切换；F10：最大化/还原；R：重载包；Esc：退出全屏，再按退出程序。
新增 HD 不会自动进入已导入包：重跑准备工具，再按 R。
`examples/preview.scene.json` 仅为验证渲染的工程布局，**不是原版 UI 复刻结果**。

## content v1 契约

包根的 `manifest.json`：

```json
{
  "version": 1,
  "design_size": [800, 600],
  "development": true,
  "entry_scene": "scenes/preview.json",
  "font": {"path": "fonts/NotoSansCJK-Regular.ttc", "face": 2},
  "images": {
    "bg/example.png": {
      "design_size": [800, 600],
      "variants": {
        "1": {"path": "images/example.png", "pixels": [800, 600]},
        "4": {"path": "images/example-4.webp", "pixels": [3200, 2400]},
        "8": {"path": "images/example-8.webp", "pixels": [6400, 4800]}
      }
    }
  }
}
```

路径必须在包内，禁止绝对路径、`..`、软链接逃逸。导入工具使用 `blobs/<sha256>.<扩展名>`
减少重复并支持原子更新；上例的可读路径同样有效。每次读文件前再次解析路径约束。
`--validate-content` 检查全部声明图像能解码、像素尺寸与清单相符、场景资源引用有效、字体能覆盖文本。

- `design_size` 来源于 `assets/manifest/images.csv`，是虚拟坐标尺寸；不按 `_big` 名字再猜倍率。
- 档位顺序：8→4→1 或 4→1。1× 仅在 `development=true` 时允许；最终发布包应仅含高清档。
- `pixels` 表示真实解码尺寸。8× 超出 WebP 16383 边长上限时，允许 `_8x` 里装 4× 内容；
  其它尺寸不一致直接报错。不对这类内容误作 8× 坐标缩放。
- 缺少、损坏、超 GPU 最大纹理尺寸或单图超过 256 MiB 时会记录并尝试低档；没有可用档则错误退出。
- PNG/WebP 解码内存上限 512 MiB，单次读取文件同样受限；动画 WebP 暂不支持。
- 包准备工具遇到损坏 HD 会失败，并保留上次有效 manifest；等待流水线文件完成后重试。
- `--hd-only` 要求 4× 已存在，8× 可后补。原始图像通过 SHA-256 与 manifest 对拍。
- 缩略图（`sys/thumb/`、`r18_sys/thumb/` 等 465 项）**不预烘文件**（清单 method =
  `runtime_derive_from_hd`）：运行时从对应 4× 高清图降采样并缓存，原 1× 缩略图作为
  缺图回退；图鉴/存档打开时按需生成，缓存需计入显存/内存预算。

场景字段见 `examples/preview.scene.json`：所有坐标/字号使用 800×600 逻辑单位。
图像使用完整画布、保留透明边，层按数组顺序绘制。矩形可指定 RGBA；文本支持 UTF-8 和换行。
该 JSON 是新框架的预览格式，不能冒充 `.sc` 脚本语义或原版 UI 坐标证据。

## 验证

```bash
ctest --preset dev
./build/dev/ef_native --content content --validate-content

# 窗口/像素测试：需要 Xvfb、Openbox、xprop；使用合成素材，不读游戏数据
sudo apt-get install xvfb xauth openbox x11-utils
xvfb-run -a -s '-screen 0 1600x1200x24' python3 tests/render_smoke.py

# 真实素材预览，在正常桌面会话运行
mkdir -p work/runtime
./build/dev/ef_native --content content --smoke --screenshot work/runtime/preview.ppm

# 数据层或工具修改后仍须执行（需本机原游戏数据）
python3 work/verify/verify.py
```

渲染测试实际比较 4×/8× 的不同颜色、半透明像素、黑边和文字区域，并观察
4× 窗口→8× Borderless→4× 还原的原生状态。Xvfb 单独只是显示服务器，
必须有窗口管理器响应全屏请求；测试脚本负责启动独立 Openbox。
CI 执行核心/导入和合成渲染测试；不运行依赖私有游戏数据的 M0 验证。

本机验收记录（2026-10-24，Ubuntu 26.04 / GCC 15.2 / SDL 3.4.18 / Mesa 软件 OpenGL）：
CTest 3/3 通过（核心、剧本 + 5 项导入测试）；合成图像的渲染/最大化/全屏/回退检查 PASS；
真实素材 1×/4×/8× 解码与全屏往返 PASS，另连续 20 次全屏启动/退出均为退出码 0。
预览图与日志保存在被忽略的 `work/runtime/`，自动像素检查产物在 `build/render-smoke/`。
M0 独立验证重新读取 254 个归档样本、核对全部 5,989 个文件哈希、检查 3,758 张 PNG，
最终 PASS、0 失败，报告见 `work/verify/VERIFY-REPORT.md`。

## 下一阶段与限制

1. 完整离线导入、音视频映射和类型化引用审计已实现；bg/st 同名立绘优先级已取证（st 优先、bg 回退，见 `06-RE-ROADMAP.md`），审计歧义归零。
2. M2：基础解析/消息/选项/变量/跳转已完成；后续接实际场景演出与音频。严格模式拒绝未知命令，文字校阅需显式启用。
3. UI 坐标逆向与截图对齐后实现全部原版 UI；当前预览布局不进入正式 UI。
4. WebP→KTX2 BC7/ASTC 安装缓存、后台资源加载与预取。当前同步解码可能在首次加载/切档时短暂停顿；
   多张大图超过缓存时可能反复换入，后续需场景预算/预取管理。
6. 缩略图运行时派生与缓存（`runtime_derive_from_hd` 465 项）：从 4× 图按显示尺寸降采样，
   缓存复用，1× 原图仅作缺图回退；不要为缩略图单独走 AI 超分。
5. CJK 禁则、ruby、逐字/语音时序、原版字距/行距、`.ani`、转场、音视频、新存档及迁移。

代码没有为这些未实现模块放置返回成功的空实现。C++ 运行时不链接 Blowfish/RC4 或调用原始游戏目录。
系统渲染驱动与标准库是原生依赖，游戏内容来源仅为包目录。
