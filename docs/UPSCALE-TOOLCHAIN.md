# ef - the first tale Linux 移植版：高清化（AI 超分）工具链方案

> 文档状态：**可行性已验证（本机实测）**，2026-10-24
> 适用范围：`assets/raw/` 下 3,758 张 PNG（800×600 为主）的批量高清化
> 相关脚本：`tools/upscale/`；原始探测证据：`work/upscaler/`

> ⚠ **倍率约束更新（2026-10-24，用户指定）**：本项目只使用 **4x / 8x** 两档，
> 由远端 `img2_upscale` PyTorch 流水线产出，见
> [`UPSCALE-REMOTE-PIPELINE.md`](UPSCALE-REMOTE-PIPELINE.md)。本文 §4.5 / §6 中
> 2x/3x 的推荐与命令**已作废**；本机 ncnn 4x 管线（`tools/upscale/run_realesrgan.sh`）
> 只作为**没有 GPU 服务端时**的临时备选。

## 0. 结论（TL;DR）

1. **本机可以直接开跑，无需安装任何东西、无需下载模型。** 机器上已有可用的
   `realesrgan-ncnn-vulkan`（ELF，2022-04 build）与完整模型目录，GTX 960M（4 GB）
   Vulkan 推理实测通过：800×600 全屏图 4× 放大约 **2 秒/张**（批量摊薄后）。
2. **推荐管线：`realesrgan-ncnn-vulkan` + `realesr-animevideov3` 模型，按类别分批目录处理。**
   CG/背景/立绘 → 4×（或 3×）；RGBA 立绘/UI 走 alpha-aware 拆分管线；纯色/渐变遮罩**程序化重绘**；
   缩略图**由高清 CG 降采样生成**，不单独超分。显卡 4 GB 显存够用（animevideov3 峰值 155 MiB）。
3. **规模与预算**：去重后 3,754 张、1,497.2 Mpx。全量输出：
   **2× = 5,956 Mpx ≈ 6.8 GB ≈ 1.5 h；3× ≈ 15.4 GB ≈ 1.6 h；4× ≈ 23,790 Mpx ≈ 27.3 GB ≈ 1.9 h**
   （animevideov3，GPU 时间；PNG 体积 ±30%）。磁盘剩余 63 GB，4× 能放下但偏紧。
4. **倍率建议**：默认发 **2×（1600×1200）**；可选 **3×（2400×1800）HD 包**；4× 留给 4K 但滚动背景
   （最宽 5054 px）4× 后 20,216 px **超过 Maxwell 单纹理上限 16384**，必须 2×/3× 或切块，故 4× 不建议作为默认包。
5. **主要风险**：① 半透明边缘（立绘）直接超分会有轻微描边，需 alpha 拆分；② 2,226 个 `.ani`
   动画数据里的像素坐标/尺寸需在移植引擎里按倍率放大（格式语义待确认）；③ UI 里烧死的文字应改用引擎
   FreeType 重绘，而不是超分；④ 发行需附第三方许可（Real-ESRGAN/ncnn 均 BSD-3-Clause）。

## 1. 环境实测（只探测，未安装）

原始输出见 `work/upscaler/gpu.txt`、`tools-env.txt`、`python-packages.txt`、`env2.txt`、`network.txt`、
`vulkaninfo-summary.txt`、`tool-versions-and-kinds.txt`。

### 1.1 GPU / 驱动 / Vulkan

| 项目 | 实测值 |
|---|---|
| 独显 | **NVIDIA GeForce GTX 960M**，4,096 MiB 显存，Maxwell（sm_50），N/A 功耗墙 |
| 驱动 / CUDA | 580.178.04 / **CUDA 13.0**（driver API）；`nvcc` 12.4.131（支持 compute_50~90） |
| 当前占用 | 4 MiB（仅 gnome-shell），空闲 |
| 集显 | Intel HD Graphics 530（SKL GT2），Vulkan 1.4.335，Mesa 26.0.8 |
| 软件渲染 | llvmpipe（LLVM 21.1.8）——**不可用**，比独显慢 1~2 个数量级 |
| Vulkan | Instance 1.4.341；独显 device apiVersion **1.4.312**，driverInfo 580.178.04 |
| 设备顺序 | **`-g 0` = Intel HD530，`-g 1` = GTX 960M，`-g 2` = llvmpipe**（实测 realesrgan `-v` 输出确认） |
| 会话 | Wayland（`XDG_SESSION_TYPE=wayland`，DISPLAY=:0），无头计算不受影响 |

> 实测：Vulkan 枚举顺序与 `vulkaninfo` 完全一致；**必须显式 `-g 1`**，否则默认可能选中集显。
> 本机是 Optimus 双显卡笔记本，独显 Vulkan 计算可用，无需 `__NV_PRIME_RENDER_OFFLOAD`。

### 1.2 Python 与图像栈

| 组件 | 状态 |
|---|---|
| Python | 3.12.12 |
| numpy / Pillow | **2.4.4 / 12.2.0（已装，够用）** |
| torch / torchvision / onnxruntime / cupy / cv2 | **全部未安装** |
| realesrgan / basicsr / spandrel / ncnn(py) | 未安装（**不需要**：用 ncnn-vulkan 原生二进制） |

### 1.3 PATH 工具

| 工具 | 状态 |
|---|---|
| `realesrgan-ncnn-vulkan` / `waifu2x-ncnn-vulkan` | PATH 里没有，但**本机已有解压好的副本（见 1.5）** |
| ffmpeg | **8.0.1**（需要处理 mov/视频时用；本次超分不需要） |
| ImageMagick | **7.1.2 Q16**（程序化重绘遮罩、缩略图降采样可用） |
| vulkaninfo / glxinfo / nvidia-settings | 已装 |
| nvcc 12.4 / cmake 4.2.3 / git 2.53 | 已装（若要走 PyTorch 路线才用得到） |
| chaiNNer / upscayl / waifu2x-converter-cpp | 未安装（不需要） |

### 1.4 磁盘 / 内存 / CPU

| 项目 | 实测值 |
|---|---|
| 根分区 | 915 G，已用 807 G，**可用 63 G（93%）** |
| 内存 | 30 G（可用约 19 G） |
| CPU | 8 核 |

> 4× 全量输出 27.3 GB：能放下，但剩余会降到 ~36 GB。若同时要 3×+4× 两套（42.7 GB），
> 建议先清理或放外置盘。2×（6.8 GB）毫无压力。

### 1.5 本机已有的超分工具（关键发现）

```
/home/lrxin/文档/realesrgan-ncnn-vulkan/
├── realesrgan-ncnn-vulkan        ELF x86-64, 11.4 MB, 2022-04-24 build
│                                 sha256 22c2296a39b40cc43ac92615c827ab6d4b60b7142e8cee59f718e3f8e2cdbda9
├── README_ubuntu.md
└── models/                       共 45 MB
    ├── realesr-animevideov3-x{2,3,4}.{param,bin}   1.25 MB/个（SRVGG compact，动画风格，速度最快）
    ├── realesrgan-x4plus-anime.{param,bin}         8.9 MB（RealESRGAN_x4plus_anime_6B，4×）
    └── realesrgan-x4plus.{param,bin}               33.4 MB（通用 4×，偏照片/写实）

/home/lrxin/文档/waifu2x-ncnn-vulkan/   waifu2x-ncnn-vulkan 8.3 MB（2025-09 build），
    models-cunet / models-upconv_7_anime_style_art_rgb / models-upconv_7_photo
/home/lrxin/.local/share/realesrgan/    同一份 realesrgan 的解压副本（含 r.zip 46 MB，可作备份）
```

许可：Real-ESRGAN、ncnn 均为 **BSD-3-Clause**（模型权重随 release 提供，可再分发）；
waifu2x-ncnn-vulkan 与 waifu2x 模型为 **MIT**。发行时附 `LICENSE` 即可；超分产物属于游戏美术的
衍生作品，法律风险与移植本身相同（不额外恶化）。

**不需要安装**。若换到别的机器，`realesrgan-ncnn-vulkan` 官方 Ubuntu 包约 **72 MB**
（`https://github.com/xinntao/Real-ESRGAN/releases` → `realesrgan-ncnn-vulkan-20220424-ubuntu.zip`），
解压 + `chmod +x` 即用，无 CUDA/Caffe 依赖。

### 1.6 网络可达性（只做 HEAD，5~10 s 超时，未下载）

| 目标 | 结果 |
|---|---|
| pypi.org / files.pythonhosted.org | 200 / 404（可达，~2.8 s） |
| github.com / api.github.com / raw.githubusercontent.com | 200 / 200 / 301（可达） |
| huggingface.co | 200（可达）；`cdn-lfs.huggingface.co` DNS 解析失败 |
| developer.nvidia.com | 200 |

> 结论：需要补工具/模型时网络可用，但本次**没有必要**下载任何东西。
> 若走 PyTorch 路线（不推荐）：`torch cu124` 轮子约 2.5 GB 且新版本可能移除 Maxwell（sm_50）支持，
> CUDA 12.4 的 `nvcc` 仍列出 compute_50，但需要自行实测。

## 2. 工具实测基准（GTX 960M，`-g 1`，Vulkan）

原始日志：`work/upscaler/smoke/*.log`、`gpu_samples_*.txt`、`batch_bench.log`。

### 2.1 速度

| 测试 | 输入 | 参数 | 墙钟 | 备注 |
|---|---|---|---|---|
| 单张 RGBA 背景 | 800×600（vista_window2） | animevideov3 **4×** | **2.76 s** | 含 ~1.5 s 模型加载 |
| 单张 RGB 事件 CG | 800×600 | animevideov3 **4×** | 4.01 s | 首次/冷启动偏高 |
| 单张立绘 RGBA | 999×600 | animevideov3 **4×** | 3.86 s | 输出 3996×2400 |
| 单张滚动背景 RGB | 3200×600 | animevideov3 **4×** | 10.04 s | 输出 **12800×2400**（30.7 Mpx） |
| 单张缩略图 | 107×80 | animevideov3 **4×** | 0.75 s | 固定开销为主 |
| **批量 5 张 CG** | 5×800×600 RGB | animevideov3 **4×** | **12.39 s** | 摊薄 ≈ **2.2 s/张 ≈ 4.6 s/Mpx** |
| 批量 5 张 CG | 5×800×600 RGB | animevideov3 **3×** | 10.73 s | ≈ 3.9 s/Mpx |
| 批量 5 张 CG | 5×800×600 RGB | animevideov3 **2×** | 10.15 s | ≈ 3.7 s/Mpx（2× 并不比 4× 快多少） |
| 批量 5 张 CG | 5×800×600 RGB | **x4plus-anime 4×** | **34.30 s** | ≈ 6.5 s/张 ≈ 13.5 s/Mpx（约慢 3 倍） |
| waifu2x cunet 2× | 999×600 RGBA | `-n 0 -s 2` | 2.29 s | 速度可以，但细节恢复弱于 ESRGAN |
| **waifu2x CPU 回退** | 107×80 | `-g -1` | **segfault (signal 11)** | **CPU 路线不可用**，只能用 Vulkan 独显 |

### 2.2 显存与内存

| 模型 | 峰值显存（nvidia-smi 1 Hz 采样） | 进程 RSS 峰值 |
|---|---|---|
| realesr-animevideov3 4× | **155 MiB** | ~270–400 MB |
| realesrgan-x4plus-anime 4× | **2,055 MiB** | ~540 MB |

> 4 GB 显存跑两种模型都安全。**不要并发多进程**，也不要在跑批时开 Blender/Unity/浏览器游戏；
> x4plus-anime 峰值 2 GB 时剩余空间已经不多。

### 2.3 输出体积（PNG 8-bit，`-f png`）

| 输入 | 输出 | 体积 | B/px |
|---|---|---|---|
| 800×600 RGB CG | 3200×2400 | 9.67–11.55 MB（5 张实测均值 **10.5 MB**） | ~1.37 |
| 999×600 RGBA 立绘 | 3996×2400 | 10.34 MB | ~1.08 |
| 810×620 RGBA 合成层 | 3240×2480 | 8.84 MB | ~1.10 |
| 3200×600 平滑背景 | 12800×2400 | 16.83 MB | ~0.55 |
| 800×600 RGB CG（2×） | 1600×1200 | 2.37 MB | ~1.23 |
| 107×80 缩略图 | 428×320 | 266 KB | ~1.94 |
| 800×600 RGB CG（x4plus-anime 4×） | 3200×2400 | 3.25–5.52 MB（均值 4.6 MB） | ~0.60 |

> 有意思的副作用：x4plus-anime 输出 PNG 明显更小（画面更"干净"、高频噪声少）；animevideov3
> 会补出更多细碎纹理，体积更大。体积估算因此以 1.3 B/px（CG）为上限锚点。

### 2.4 alpha 行为实测（重要）

- 输入 RGBA 时工具**不会丢 alpha**：输出仍是 RGBA PNG，尺寸正确。
- **alpha 通道由重采样器放大，质量接近 Lanczos**（对 4× 输出与 `PIL.resize(LANCZOS)` 比较：
  mean abs diff 0.05–0.08；vs BILINEAR 0.13–0.21；vs NEAREST 0.35–0.53），alpha 均值逐位保持不变。
- **但半透明边缘的 RGB 是模型"编造"的**：立绘 `st_yuko13g05` 源图 near-transparent 边缘 RGB≈8，
  输出边缘 RGB≈46–61（偏亮）。在深色背景上可能出现浅色/深色描边。透明区 RGB 垫底不同的图
  （如 vista_window2 用白色垫底）表现又不同。
- CG 的 alpha 大多是**淡入淡出用的整体 alpha**（抽样 40 张 `has_alpha` 的 ev CG，alpha 最小值 220–225，
  只有 0.58% 像素处于半透明），**不是抠像透明**，直接超分无描边风险；立绘/UI 的 alpha 是真抠像
  （593 张立绘 alpha min=0，1.7%~3.7% 半透明像素），**必须走 alpha-aware 管线**。
- 统计口径：3,754 张唯一 PNG 中 2,253 张带 alpha 通道（其中 CG 1,581 张属"淡入型 alpha"）。

## 3. 资源规模统计

由 `tools/upscale/make_manifest.py` 生成（数据源 `assets/manifest/entries_*.jsonl`，
结果 `work/upscaler/asset-stats.json`，清单 `work/upscaler/worklists/*.txt`）。
去重规则：按"归档内相对路径"去重，`r18_*` 覆盖集同相对路径 4 张替换基础版（MemoriesSelect1/2 + Over）；
其余同名多为 `sys/thumb` 缩略图与全尺寸 CG 同名复用（内容不同，必须分别处理）。

| 类别 | 文件数 | 原始 Mpx | 原始 MB | 带 alpha | 4× 输出 Mpx | 4× 估算体积 | 4× GPU 时间 | 策略 |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| cg（ev*/evh*/ed* 事件/结局 CG） | 2,327 | 1,158.30 | 1,304.9 | 1,581 | 18,532.9 | 22.44 GB | 1.48 h | 超分 |
| tachie（st_*/stm_* 立绘） | 593 | 165.99 | 106.7 | 593 | 2,655.8 | 2.67 GB | 0.21 h | **alpha-aware 超分** |
| bg（bg_/bgs_/bgw_/bgc_/kei_/vista_/ago_/prologue_/terop_） | 168 | 87.56 | 84.3 | 3 | 1,400.9 | 1.30 GB | 0.11 h | 超分 |
| scroll_bg（bg_scl*，3200×600 等） | 28 | 52.81 | 28.1 | 8 | 844.9 | 0.43 GB | 0.07 h | 超分（≤3×，见 4.5） |
| ui（sys 其余 UI 图） | 84 | 14.82 | 4.7 | 55 | 237.2 | 0.35 GB | 0.02 h | 装饰图超分 / 文字重绘 |
| **mask_procedural**（1×601 条、WHITE/BLACK、fade*、snow*、or_*） | 73 | 6.87 | 1.1 | 9 | 109.9 | ~0 | 0 | **程序化重绘，不超分** |
| other（兜底） | 14 | 6.72 | 0.2 | 3 | 107.5 | 0.11 GB | 0.01 h | 超分 |
| **thumb**（107×80 图鉴/400×80 事件缩略图） | 467 | 4.09 | 7.0 | 1 | 65.5 | 0.01 GB | 0 | **由高清 CG 降采样生成** |
| **合计（唯一 PNG）** | **3,754** | **1,497.2** | **1,537.0** | 2,253 | **23,790.1** | **≈27.3 GB** | **1.90 h** | |

倍率汇总（animevideov3，批量、扣除加载开销）：

| 倍率 | 输出像素 | 估算体积 | GPU 时间 | 适用窗口 |
|---|---:|---:|---:|---|
| 2×（1600×1200） | 5,956 Mpx | **6.8 GB** | **1.53 h** | 1080p 窗口（4:3 内容 1440×1080）绰绰有余 |
| 3×（2400×1800） | 13,387 Mpx | **15.4 GB** | **1.61 h** | 1440p 窗口原生；2400×1800 仍在纹理上限内 |
| 4×（3200×2400） | 23,790 Mpx | **27.3 GB** | **1.90 h** | 4K 目标；显存/填充率与滚动背景纹理上限受限 |
| 4× + x4plus-anime | 23,790 Mpx | ≈35 GB | **5.57 h** | 如果质量验收要求更高 |

特殊尺寸（必须特殊处理，否则会出 bug）：

| 情况 | 数量 | 处理 |
|---|---:|---|
| 宽/高 ≤ 4 px（1×601 / 1×621 渐变条） | 51 | **不超分**，按输出高度程序化生成渐变（这些是滚动/过场遮罩） |
| 107×80 / 400×80 / 115×88 缩略图 | 467 | 由对应高清 CG 降采样；或引擎运行时生成 |
| 宽 > 800（含 5054×600） | 144 | 滚动背景，2×/3×；4× 会超纹理上限 |
| 高 > 600（800×610/654/675/1200 等） | 280 | 正常超分；引擎按原尺寸/滚动参数绘制 |
| 像素 > 800×600 | 249 | 同上，注意输出体积与纹理尺寸 |
| 纯色/遮罩（WHITE/BLACK/TRANS800x75/fade*） | 73 | **程序化重绘**（用原图取色） |

最大尺寸 top5：`bg_scl008a01_03.png` 5054×600、`bg_scl001a01_03_n.png` 4627×600、
`bg_scl001a01_02_n.png` 4627×600、`bg_scl001a01_01_n.png` 4627×600、`ev00_004_mugai_h2.png` 1581×1637。

## 4. 选型结论

### 4.1 模型对比

| 方案 | 本机可用性 | 速度 | 显存 | 画质倾向 | 结论 |
|---|---|---:|---:|---|---|
| **realesr-animevideov3（4×/3×/2×）** | ✅ 已有 | **4.6 s/Mpx** | 155 MB | 动画线条干净、细节略"AI 感" | **全量默认模型**（唯一支持 2/3/4× 的现成模型） |
| realesrgan-x4plus-anime（6B） | ✅ 已有 | 13.5 s/Mpx（约 3×） | 2.0 GB | 更平滑、噪点少、线稿更稳；PNG 更小 | 重点 CG/立绘的质量备选，A/B 后按需替换 |
| realesrgan-x4plus（通用） | ✅ 已有 | 约 22 s/Mpx（估算，未实测） | 3+ GB | 写实/照片质感更好；对动画线条可能过锐 | 若背景是照片/3D 渲染再考虑，先 A/B |
| waifu2x cunet | ✅ 已有 | 2–4 s/Mpx 级 | 小 | 2× 降噪为主，细节恢复弱于 ESRGAN | 只作对比/应急，不推荐当主方案 |
| chaiNNer / upscayl | ❌ 未安装 | — | — | GUI 工作流，本质调用同类模型 | **不需要**（还要装 1–2 GB 依赖） |
| PyTorch Real-ESRGAN | ❌ 未安装 | 960M 上更慢 | 2–4 GB | 模型生态最全 | **不需要**（ncnn 路径已可用；装 torch 2.5 GB+，且新轮子可能不支持 sm_50） |
| 内置 alpha 直跑（passthrough） | ✅ | 同上 | 同上 | 立绘边缘有描边风险 | 可用于 CG（淡入型 alpha）；**立绘/UI 用拆分管线** |

实测画质对比见 `work/upscaler/smoke/montage_cg_quality2.png`（左 bicubic / 中 animevideov3 /
右 x4plus-anime）：两者都显著优于 bicubic；animevideov3 线条干净、x4plus-anime 细节更实、体积更小。
建议在正式全量前对 20~30 张代表性图做一次 A/B（脚本见 §6.4）。

### 4.2 按类别推荐

| 类别 | 模型/处理 | 倍率 | 说明 |
|---|---|---|---|
| 事件/结局 CG（2,327） | animevideov3 | 2×（v1）/ 3×（HD） | 大头；alpha 为淡入型，可直跑；重点 CG 可换 x4plus-anime |
| 立绘 `st_*`（593） | animevideov3 **+ alpha 拆分** | 2×/3×/4× | 透明边缘必须 alpha-aware（§4.3）；可用 `--alpha-mode split` |
| 背景（168） | animevideov3 | 2×/3× | 若是照片型背景，先 A/B x4plus |
| 滚动背景（28） | animevideov3 | **2× 或 3×** | 4× 最大 20,216 px > 16,384 纹理上限；且运行时纹理显存大 |
| UI 装饰图（84 中的无文字部分） | animevideov3 | 2×/3× | 图标/边框/面板 |
| UI 文字（菜单/存档/图鉴文字） | **引擎 FreeType 重绘** | — | 原图文字是 800×600 位图，超分会糊/出鬼影；移植版已有字体渲染方案（docs/03） |
| 纯色/遮罩/渐变条（73） | **程序化重绘** | 目标尺寸 | 用 ImageMagick/代码生成，零 GPU、零伪影 |
| 缩略图（467） | **由高清 CG 降采样** | 目标尺寸 | 107×80 图鉴缩略图/400×80 事件条；不要直接放大低清缩略图 |

### 4.3 alpha 处理策略（立绘/UI 必读）

方案：**拆分 → 填色 → 超 RGB → 重采样 alpha → 合成**（`tools/upscale/alpha_pipeline.py` 已实现）。

1. **拆分**：RGBA → RGB + 单通道 alpha。
2. **填色（matte/dilate）**：对 RGB 的透明区做 4~8 次邻域膨胀填色（用最近的不透明像素颜色），
   消除模型看到黑/灰垫底而"编"出描边的根源。**不要预乘**（预乘会把颜色压暗，模型会把暗色当真）。
3. **超 RGB**：3 通道交给 animevideov3（可整目录批处理）。
4. **alpha 放大**：Lanczos（Pillow RESAMPLE_LANCZOS）到同尺寸；与 ncnn 自带 alpha 重采样质量相当
   （实测 ncnn 的 alpha ≈ Lanczos），但这样可控。
5. **合成**：输出**直通 alpha（straight alpha）** 的 8-bit RGBA PNG，引擎按普通 alpha 混合即可。
6. 验收：抽查透明边缘在深色/浅色背景上的合成效果；`compare_montage.py` 默认深灰垫底便于发现描边。
   实测直跑 ncnn 时 alpha 形状正确、均值不变，只有边缘 RGB 会偏亮约 +40（0–255 刻度），
   因此"是否值得上拆分管线"可由这 20 张抽查决定；建议立绘/UI 一律用拆分。

### 4.4 瓦片 / 显存参数

- `-t 0`（自动）：800×600 / 3200×600 在本机 4 GB 显存下无需切块，实测正常。
- 若换到小显存机器或跑 x4plus-anime 大图：`-t 512`（或 256）；失败时降到 256。
- `-j 1:2:2` 默认合适；PNG 编码是 CPU 侧瓶颈，可试 `-j 2:2:2`（收益有限）。
- **串行**跑批，不要并行多进程；批处理一次喂一个类别目录，摊薄模型加载（省 1.5–2 s/张）。
- `-x`（TTA）会让耗时翻数倍且收益很小，**不要开**。

### 4.5 倍率与移植引擎窗口缩放的关系

移植版渲染方案（见 `docs/03-TEXT-AND-RENDERING.md`、`docs/05-PORT-PLAN.md`）是
**800×600 逻辑分辨率离屏合成 → 整数/平滑缩放到窗口**。高清化后的接法：

1. **坐标系不变**：逻辑坐标仍是 800×600；渲染到 `800·S × 600·S` 的 FBO/离屏目标（S=倍率），
   投影矩阵乘 S，所有 quad 的目标尺寸 = 源图尺寸 × S。UI 布局代码不用改数值。
2. **纹理按原尺寸绘制**：800×600 的图在 S=2 时画到 1600×1200 目标区域，1:1 采样，不糊。
3. **窗口缩放**：FBO（4:3）→ 窗口做整数倍 + 黑边（pillarbox/letterbox）；对应原版"高质量屏幕拉伸"。
4. **滚动背景**：`scroll/hscroll/vscroll` 用原图宽高（会大于 800×600）在 FBO 坐标系里平移；
   2× 时最宽 5054→10,108 px，3× 时 15,162 px（仍在 16,384 纹理上限内）；4× 时 20,216 px
   **超过 GTX 960M 的 Maxwell 单纹理上限 16384**，要么保持 2×/3×，要么切成 2 条纹理拼接。
5. **推荐档位**：
   - **默认 2×（1600×1200）**：1080p 窗口（4:3 内容 1440×1080）有 1.11× 余量；输出 6.8 GB；纹理 7.7 MB/张。
   - **HD 包 3×（2400×1800）**：原生覆盖 1440p，15.4 GB；GPU 时间只多 5%，性价比高。
   - **4× 仅作 4K 实验包**：27.3 GB、FBO 7.7 Mpx/帧，960M 上 60 fps 风险大；滚动背景需特殊处理。
   - 低配/兼容档：保留原始 800×600 资源，运行时按设置选择（移植版设置项"高清素材包"）。

### 4.6 输出格式与位深

- **PNG 8-bit**：源全是 8-bit（bit_depth=8，无 16-bit），16-bit 只会让文件/显存翻倍且引擎还要转换。
- 不透明图 → **RGB PNG**（丢掉无用 alpha，省 ~20–25% 体积与显存）；有真实 alpha → **RGBA PNG**。
- **不要 JPEG**：文字/线条振铃、alpha 不可用。可选无损 WebP 需引擎加解码依赖，v1 不做。
- 文件命名与目录结构保持与 `assets/raw/` 完全一致（端口按原 rel_path 查找）；`.ani` 不动。
- 放大后可用 `oxipng -o2 --strip safe` 无损再压缩（可省 10–20%），但这是可选步骤。

## 5. 推荐管线（分阶段执行计划）

**阶段 P0 — 抽样验收（今天就能做，≈10 分钟）**
1. `make_manifest.py` 生成清单/统计（已生成，见 `work/upscaler/`）。
2. 每个类别抽 20 张跑 4×；`compare_montage.py` 出对比图；确认无鬼影/色偏/文字糊。
3. 立绘抽 10 张跑 alpha 拆分管线，深色/浅色背景合成检查描边。

**阶段 P1 — 正式产出（GPU 1.5–1.9 h）**
4. 本机备选：`--scale 4 --execute` 全量；正式 4x/8x 产出走远端 PyTorch 流水线（`UPSCALE-REMOTE-PIPELINE.md`）。
5. `--class mask_procedural` 跳过（程序化重绘）；`thumb` 由 HD CG 降采样生成。
6. UI 文字部分列清单，交给引擎字体渲染，不超分。

**阶段 P2 — 增强包（可选）**
7. 重点 CG 可用 x4plus-anime 重跑（5.6 h）。
8. 8x 一律由远端 PyTorch 流水线产出；本机 ncnn 不支持 8x。

## 6. 逐条命令

> 以下命令都在项目根目录执行：`cd "/home/lrxin/Ef_Linux/ef - the first tale"`。
> `RR`/`M` 指向本机已有二进制与模型，不需要安装。

### 6.1 资源统计 / 清单

```bash
python3 tools/upscale/make_manifest.py \
    --stats-out work/upscaler/asset-stats.json \
    --emit-worklists work/upscaler/worklists --verify
```

### 6.2 单图试跑（原始命令，便于诊断）

```bash
RR="/home/lrxin/文档/realesrgan-ncnn-vulkan/realesrgan-ncnn-vulkan"
M="/home/lrxin/文档/realesrgan-ncnn-vulkan/models"

# 800x600 → 3200x2400，4x
"$RR" -i "assets/raw/bg/ev02_187.png" -o /tmp/ef_test_4x.png \
      -n realesr-animevideov3 -s 4 -m "$M" -g 1 -t 0 -f png -v

# 2x（显式 -s 2 会使用 realesr-animevideov3-x2 模型）
"$RR" -i "assets/raw/bg/ev02_187.png" -o /tmp/ef_test_2x.png \
      -n realesr-animevideov3 -s 2 -m "$M" -g 1 -t 0 -f png
```

### 6.3 批量（脚本，默认 dry-run，务必先看计划再 `--execute`）

```bash
# 看 4x 全量计划（不跑 GPU）
tools/upscale/run_realesrgan.sh --scale 4

# 每个类别抽 20 张验收（约 1 分钟）
tools/upscale/run_realesrgan.sh --scale 4 --class cg --limit 20 --execute

# 本机备选：4x 全量（输出 work/upscaler/out/4x/；正式 4x/8x 见远端流水线）
tools/upscale/run_realesrgan.sh --scale 4 --execute

# 立绘走 alpha-aware 管线
tools/upscale/run_realesrgan.sh --scale 4 --class tachie --alpha-mode split --execute
```

### 6.4 QA 对比图

```bash
python3 tools/upscale/compare_montage.py \
    --dst work/upscaler/out/4x --class cg --limit 20 \
    --out-dir work/upscaler/qa
# 输出 qa/*.compare.png：左=bicubic 放大，右=AI；qa-index.txt 记录尺寸与来源
```

### 6.5 程序化重绘：纯色/渐变遮罩（不经 GPU）

```bash
# 示例：纯色 800x600 白色遮罩 → 2x 输出
magick -size 1600x1200 xc:'#ffffff' work/upscaler/out/2x/mask/WHITE.png
# 示例：1x601 垂直渐变条 → 2x（高 1202）；颜色从原图首尾像素采样
magick -size 2x1202 gradient:'#000000-#ffffff' work/upscaler/out/2x/mask/sc_night01.png
# 批量脚本骨架：tools/upscale/redraw_masks.sh（TODO，见 §9 未知项）
```

### 6.6 缩略图（由 HD CG 降采样，不单独超分）

```bash
# 107x80 图鉴缩略图：从对应 2x CG 中心/合适区域裁剪并缩放到 214x160
magick "work/upscaler/out/2x/cg/ev02_187.png" \
       -resize 214x160^ -gravity center -extent 214x160 \
       work/upscaler/out/2x/thumb/ev02_187.png
# 注意：原缩略图的裁剪区域/构图需先从原图反推（见 §9 未知项）
```

### 6.7 环境降级 / 换机器时的安装命令（本机不需要，仅备忘）

```bash
# Real-ESRGAN ncnn-vulkan（Ubuntu，解压即用，约 72 MB）
curl -L -o /tmp/realesrgan.zip \
  https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-ubuntu.zip
unzip -q /tmp/realesrgan.zip -d ~/tools/realesrgan && chmod +x ~/tools/realesrgan/realesrgan-ncnn-vulkan
# 不要把模型/二进制提交进游戏资源目录，放 tools/ 之外或 .gitignore

# 备选：PyTorch 路线（不推荐；约 2.5 GB，Maxwell 支持随版本变化需实测）
# python3 -m pip install torch torchvision --index-url https://download.pytorch.org/whl/cu124
# python3 -m pip install spandrel    # 比 basicsr 更耐新 torchvision
```

## 7. 风险与未知

| # | 风险/未知 | 影响 | 缓解 |
|---|---|---|---|
| 1 | **GPU 老（Maxwell sm_50，4 GB）** | 4× 大图/高分辨率窗口 60 fps 有压力；CUDA 新轮子可能不支持 | 用 ncnn-Vulkan 而非 CUDA；默认 2×；4× 仅实验 |
| 2 | **滚动背景 4× 超纹理上限**（5054×4=20,216 > 16,384） | 4× 包在 960M 上无法上传单纹理 | 滚动背景固定 2×/3×，或切块纹理 |
| 3 | **`.ani` 动画坐标语义未确认**（2,226 个 ANI） | 高清素材后动画位移/尺寸可能错位 | 引擎里把 ANI 内坐标按 S 缩放；需拿 1–2 个 ANI 反解验证（`work/upscaler/` 未做，属端口任务） |
| 4 | **立绘透明边描边** | 深色背景上可见浅边 | alpha 拆分管线（已实现）；20 张抽查 |
| 5 | **UI 烧死文字** | 超分后文字糊/鬼影 | 文字交由引擎 FreeType 重绘；仅装饰图超分 |
| 6 | **缩略图构图未知** | 无法批量从 CG 反推裁剪区域 | 先用原缩略图（小尺寸可接受），或手工/运行时生成；列为端口后续任务 |
| 7 | 磁盘 63 GB | 4× 27.3 GB + 现有资源，余量 ~35 GB；同时要 3×+4× 会紧张 | 只产出所需倍率；先清临时文件/外置盘 |
| 8 | PNG 体积比预期大（CG ~1.3 B/px） | 4× 发行包 ~27 GB | 2×/3× 为主；可选 oxipng 无损压缩 |
| 9 | 模型输出"AI 感"（纹理过度锐化） | 与原画风格有差异 | A/B 选择；x4plus-anime 更平滑；重点图可人工挑模型 |
| 10 | 许可/分发 | 发行包需带第三方许可 | 打包 `LICENSES/Real-ESRGAN-BSD-3-Clause.txt` 等；超分图与游戏素材同权属风险 |
| 11 | CPU 回退不可用 | 换到无 Vulkan 机器会失败 | waifu2x `-g -1` 在本机 segfault；备用方案只能是 ncnn CPU 版或 PyTorch |
| 12 | 全量跑批无断点续跑验证 | 中断需重跑该类别 | `--skip-existing` 已支持；脚本按类别/分批，失败不影响已完成文件 |

## 8. 交付物与证据索引

| 路径 | 内容 |
|---|---|
| `docs/UPSCALE-TOOLCHAIN.md` | 本文档 |
| `tools/upscale/make_manifest.py` | 资源统计/分类清单/体积时间估算（只读） |
| `tools/upscale/run_realesrgan.sh` | 批量超分驱动（默认 dry-run；`--execute` 才跑） |
| `tools/upscale/alpha_pipeline.py` | RGBA alpha-aware 管线（默认 dry-run；已端到端验证 1 张立绘） |
| `tools/upscale/compare_montage.py` | QA 对比图生成 |
| `tools/upscale/README.md` | 脚本速查 |
| `work/upscaler/asset-stats.json` | 2×/3×/4×/x4plus-anime 四套统计（机器可读） |
| `work/upscaler/worklists/*.txt` | 8 个类别、3,754 条 rel_path 清单 |
| `work/upscaler/gpu.txt` | nvidia-smi 原始输出 |
| `work/upscaler/vulkaninfo-summary.txt` | Vulkan 设备摘要 |
| `work/upscaler/python-packages.txt`、`tools-env.txt`、`env2.txt` | Python/工具/磁盘/内存探测 |
| `work/upscaler/network.txt` | HEAD 网络探测（未下载） |
| `work/upscaler/existing-tools.txt`、`tool-versions-and-kinds.txt` | 本地已有工具与模型清单、二进制 sha256 |
| `work/upscaler/smoke/*.log`、`gpu_samples_*.txt` | 单图/批量基准原始日志与显存采样 |
| `work/upscaler/smoke/{a..i}_*.png` | 九组超分样例（含 12800×2400 滚动背景） |
| `work/upscaler/smoke/montage_*.png` | 画质/透明边对比图 |

## 9. 验收清单（给 Lead）

- [x] 本机可直接运行超分（GTX 960M + Vulkan，`-g 1` 实测通过，无需安装/下载）
- [x] 资源规模与分类统计完成（3,754 张唯一 PNG / 1,497.2 Mpx / 8 类清单）
- [x] 2×/3×/4× 输出体积与 GPU 时间估算（6.8 / 15.4 / 27.3 GB；1.5 / 1.6 / 1.9 h）
- [x] 模型选型结论（animevideov3 默认，x4plus-anime 备选，waifu2x 对比）
- [x] alpha 策略实测与 alpha-aware 脚本（已跑通 1 张立绘：3996×2400 RGBA，alpha 均值不变）
- [x] 倍率与引擎窗口缩放衔接方案（2× 默认、3× HD 包、4× 受限；滚动背景纹理上限）
- [x] 脚本骨架 dry-run 验证（`run_realesrgan.sh --scale 4` 打印计划不跑 GPU）
- [ ] **待 Lead 决策**：默认倍率（建议 2×）、是否要 3× HD 包、UI 文字清单谁负责、ANI 坐标放大由谁实现
- [ ] 待做（端口任务，非本文档范围）：ANI 坐标语义反解、缩略图构图反推、`redraw_masks.sh` 落地
