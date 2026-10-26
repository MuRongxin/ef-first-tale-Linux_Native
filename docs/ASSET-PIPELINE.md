# 09 — 图像资源解包与 HD 高清化清单（移植第一步交付）

> 本文记录 **ef - the first tale Linux 原生移植** 的第一步成果：把原始 `.paz`
> 归档里所有**需要放大/高清化处理的图像资源**完整解包、校验、分类，并生成可直接
> 驱动超分流程的清单。
>
> - 工程根目录：`/home/lrxin/Ef_Linux/ef - the first tale`
> - 原始游戏数据（只读）：`/home/lrxin/Downloads/ef - the first tale`
> - 生成脚本：`tools/toc_scan.py`、`tools/extract_assets.py`、
>   `tools/build_image_inventory.py`、`tools/paz_fast.py`（基准实现 `tools/paz.py`）
> - 生成时间：2026-10-24

---

## 1. 一句话结论

**6 个图像归档全部解开：5,989 个文件 / 1.87 GB / 0 失败；3,758 张 PNG 全部
Pillow 解码通过；独立验证（未改动基准实现重读原归档对拍）PASS。**
去重后 **3,754 张 PNG / 1,497 Mpx** 是实际待放大规模（其中 3,112 条唯一美术内容
走 AI 超分），另有 2,226 个 `.ani` 动画随配对 PNG 缩放、465 张缩略图运行时从高清 CG
降采样（不预烘）、74 份纯色/渐变遮罩程序化重建、82 个 UI 元素超分或重绘。

**超分管线（用户指定倍率 4x / 8x，不变）**：使用远端 `img2_upscale` PyTorch 流水线
（Real-ESRGAN x4 [+ waifu2x x2] → lossless WebP），输入/输出已适配到本项目：
读 `assets/raw/`、写 `assets/hd/<tag>_4x|_8x/`，客户端为
`tools/upscale/remote/ef_client.py`，执行说明见
[`docs/UPSCALE-REMOTE-PIPELINE.md`](UPSCALE-REMOTE-PIPELINE.md)。
规模：4x 输出 ≈23.3 Gpx、8x ≈93.1 Gpx；4 个超宽滚动背景 4x 即超 WebP 上限会被跳过，
15 个源 8x 超限按流水线语义以 4x 内容写入 `_8x` 树（另有那 4 个已跳过）。
（本机 ncnn 4x 工具链仅作无 GPU 服务端时的临时备选。）

---

## 2. 本次解包范围与结果

只解包**图像管线**相关条目（PNG 及其伪扩展名、ANI 动画、PSD 源文件、r18 元数据 txt），
音频/视频/脚本不在本报告范围（已有既有成果）。

> 视频（`mov.paz`）已单独解包：4 个 AVI/MPEG-PS，共 51.0 MB，见
> `assets/raw/mov/` 与 `assets/manifest/videos.json`（`tools/extract_videos.py`，
> 快速实现与基准实现逐字节对拍通过）。规格：800×600 / 24 fps；
> `ef_first_sound.avi` 145.3s（mpeg4+mp3）、`ef_ED_sound.avi` 65.1s、
> `op_logo.mpg` 4.0s（mpeg1+mp2）、`ed_moji_test.avi` 6.8s（无音轨）。
> 按产品决策，重制为 H.264（建议 4K）属于 M4。

| tag | 归档 | TOC 条目 | 抽取文件 | 解包字节 | 内容 |
|---|---|---:|---:|---:|---|
| bg | `bg.paz` | 3,457 | 3,457 | 1,379.7 MB | 事件/结局 CG、立绘、背景、标题美术、AND 动画 |
| st | `st.paz` | 1,033 | 1,033 | 144.5 MB | 立绘 + 滚动背景 + ANI |
| sys | `sys.paz` | 572 | 566 | 12.3 MB | 系统 UI 560 PNG(+2 伪扩展名) + 3 PSD（排除 6 个 wav） |
| r18_bg | `r18_cn/bg.paz` | 868 | 868 | 331.7 MB | R18 CG/立绘 + ANI + 2 个元数据 txt |
| r18_st | `r18_cn/st.paz` | 2 | 2 | 0.1 MB | R18 立绘 + ANI |
| r18_sys | `r18_cn/sys.paz` | 63 | 63 | 1.5 MB | R18 CG 缩略图 + 覆盖 UI |
| **合计** | 6 个归档 | **5,995** | **5,989** | **1,869.8 MB** | |

- 13 个归档的完整 TOC（12,289 条）已导出到 `work/toc/entries.jsonl`（0.5 s）。
- 解包耗时 34 s（6 进程；加速实现 `paz_fast.py`：numpy 4 字节反转 + C 版 RC4 +
  每归档复用 Blowfish 上下文，与 `paz.py` 语义逐字节一致）。
- 源归档 SHA-256 记录在 `assets/manifest/archive-digests.json`，保证可复现。

### 2.1 抽取校验（我方自检）

`assets/manifest/` 中每文件记录 sha256、尺寸、格式、PNG IHDR 元数据。
自检结果：缺失 0 / 大小不符 0 / 哈希不符 0 / PNG 解码失败 0 / IHDR 尺寸不符 0 /
未分类 0。

### 2.2 独立验证（另起验证员、基准实现对拍）

验证员**没有使用加速实现**，而是用未修改的 `tools/paz.py` 重读原始归档：

| 检查 | 结果 |
|---|---|
| 集合完整性（TOC 图像条目 vs 磁盘） | **PASS** 5,989 == 5,989，缺失/多余/零字节均 0 |
| 抽样重读对拍（254 条，含各归档最大 3 条） | **PASS** sha256 100% 一致 |
| 全量 sha256（5,989 文件 / 1.87 GB）vs manifest | **PASS** 0 不一致 |
| 3,758 个 PNG open/verify/load + IHDR 宽高 | **PASS** 0 失败 |
| ANI 配对 / 头部统计 | 2,226 个全部有配对 PNG；8 种头部变体 |
| 跨归档同名内容比较 | 15 组：内容相同 7 / 不同 8 |

报告：`work/verify/VERIFY-REPORT.md`（机器可读 `work/verify/verify.json`，
复现：`python3 work/verify/verify.py`，退出码 0）。

> 验证过程顺便确认了原 `EXTRACTION_REPORT` 的"st.paz 压缩 .ani 多 8 字节 NUL"
> 怪癖：st 22 条、r18_bg 30 条、r18_st 1 条被正确截断到 `raw_size`。

---

## 3. 资源分类结果

分类规则 = 归档命名空间 + 文件名族 + 实测尺寸（脚本见
`tools/build_image_inventory.py`；全量清单 `assets/manifest/images.json|csv`）。

| 类别 | 文件数 | 字节 | 源像素 | 说明 |
|---|---:|---:|---:|---|
| `cg` | 2,327 | 1,368.3 MB | 1.158 Gpx | 事件 CG / 结局 CG / R18 CG（`ev*`/`evh*`/`ed*`） |
| `ani` | 2,226 | 257.7 MB | — | 自定义动画数据，**全部**与同名 PNG 配对 |
| `tachie` | 593 | 111.9 MB | 0.166 Gpx | 立绘（`st_*`/`stm_*`） |
| `thumb` | 466 | 7.3 MB | 0.004 Gpx | 存档/图鉴缩略图（107×80 ×462、400×80 ×4） |
| `bg` | 194 | 117.9 MB | 0.139 Gpx | 背景（含滚动/宽幅变体） |
| `ui` | 93 | 5.5 MB | 0.018 Gpx | 系统 UI 元素（菜单/存档/图鉴/滑块…） |
| `tint` | 47 | 0.04 MB | — | 1px 宽竖向色带（`or_*`/`sc_*`） |
| `mask` | 26 | 1.1 MB | 0.007 Gpx | 过渡遮罩/雪花粒子/纯色层（`fade*`/`snow*`/`WHITE`/`BLACK`/`TRANS`） |
| `title` | 12 | 0.07 MB | 0.006 Gpx | 标题/制作名单美术（`terop_*`、`sound`、`music`…） |
| `source` | 3 | 0.08 MB | 0.0001 Gpx | PSD 制作源文件（`icons`/`msgPanel`/`gaiji`） |
| `meta` | 2 | 0.03 MB | — | `names.txt`（CP932 立绘名表）、`log.txt`（2011 年打包机目录列表） |

### 3.1 各归档实际装了什么（修正此前的直觉）

| 归档 | 实际内容（按数量） |
|---|---|
| `bg.paz` | cg 1,850 + **ani 1,333** + bg 178 + tachie 78 + title 12 + mask 3 + ui 3 |
| `st.paz` | tachie 513 + ani 506 + bg 14（`bg_scl*` 滚动背景） |
| `sys.paz` | **thumb 407** + ui 86 + tint 47 + mask 23 + source 3 |
| `r18_bg.paz` | cg 477 + ani 386 + bg 2 + tachie 1 + meta 2 |
| `r18_st.paz` | tachie 1 + ani 1 |
| `r18_sys.paz` | thumb 59 + ui 4（`MemoriesSelect*.png` 覆盖基础 UI） |

关键点：**`bg.paz` 不是"背景归档"，而是美术总仓库**——事件 CG、立绘、标题美术
都在里面；`st.paz` 主要是立绘；`sys.paz` 一半以上是图鉴缩略图。

### 3.2 关键发现（对后续移植/高清化有直接影响）

1. **纠正旧文档推断**：`or_*` / `sc_*`（`overlay`/`screen`）**不是前景角色层/场景层**，
   而是 **1×601（部分 1×611/1×621）的竖向渐变色带**（47 个，合计仅 40 KB，每张
   91–191 种颜色）。引擎把它们横向拉伸成全屏着色层（时间/天气色调）。
   → HD 时**按目标高度重采样即可**，不能走 AI 超分（1px 宽没有可恢复的纹理）。
2. **缩略图不预烘**：466 张 `thumb\*` 是 107×80（CG 图鉴）与 400×80（宽横幅）；
   运行时从对应 4× 高清图降采样并缓存（1× 原图作缺图回退），直接超分只会放大压缩噪声。
3. **存在超屏画布**：背景有 3,200×600 / 4,627×600 / 5,054×600 的横向滚动图，
   800×1,200 / 800×1,500 的纵向图，以及 1,200×600 / 1,600×600 的宽幅图；
   超分必须按"内容实际尺寸"处理，不能统一按 800×600。
4. **已有 2× 母版**：8 张 `*_big.png` 是 1,600×1,200（或 1,200×1,600），
   相对 800×600 已经是 2×；本地规划清单会标注为 2× 源（远端流水线仍按源像素固定 4×/8×），
   避免做成 8× 浪费算力。
5. **PNG 规格统一**：全部 8-bit；RGB 1,493 / RGBA 2,253 / 灰度 12；
   无调色板、无 tRNS、无交错；2,253 张带 alpha（立绘/CG 镂空）。
6. **`.ani` 有 8 种头部变体**（不止旧文档的两种）：
   `00 01 04 00`=1,222、`00 01 02 00`=432、`00 01 06 00`=324、`00 01 08 00`=157、
   `00 01 0a 00`=50、`00 01 0c 00`=32、`00 01 03 00`=6、`00 01 0e 00`=3。
   全部 2,226 个都有同名 PNG；内部帧结构仍未逆向（见 `06-RE-ROADMAP`）。
7. **跨归档同名 15 组（内容相同 7 / 不同 8）**：
   - `sys ↔ r18_sys` 4 组 `MemoriesSelect*.png` **内容不同** → r18 覆盖 UI（符合补丁语义）；
   - `bg ↔ st` 10 组：6 组相同；`st_kei10b05-` 与 `st_kei11b03_close-` 的 png+ani
     内容不同（同名不同版本，需引擎加载顺序才能确定谁生效）；
   - `r18_bg ↔ st` 1 组 `st_yuko09f01_bokashi.png` 相同。
8. **内容完全相同的重复组 193 组**（`duplicates.json`）——超分只需处理每组一份，
   其余用硬链接即可，实测可显著减少重复计算。
9. **脚本引用覆盖**（`docs/asset-audit.json` 的 3,150 个资源名）：
   CG 2,128/2,327、立绘 593/593、背景 192/194、标题 12/12、tint 26/47、
   mask 9/26、UI 仅 3/93（UI 由 exe 硬编码，脚本不引用）→ **HD 优先级按"是否被脚本
   引用"排序**，2,913 个优先 1 类（被引用）、2,507 个优先 2 类。

---

## 4. HD 高清化计划（`assets/manifest/upscale-plan.json`）

按**唯一内容（sha256）去重**后共 **5,959 条任务**（含 ANI 跟随项）：

| 方法 | 数量 | 源像素 | 4× 目标像素 | 含义 |
|---|---:|---:|---:|---|
| `upscale_art` | 3,112 | 1.455 Gpx | 23.09 Gpx | AI 超分（动画风格模型）4× 母版 |
| `scale_with_pair` | 2,226 | — | — | `.ani` 坐标随配对 PNG 同比缩放（格式逆向后再处理） |
| `runtime_derive_from_hd` | 465 | 0.004 Gpx | 0.065 Gpx | 缩略图运行时从 4× HD 降采样（不预烘文件，1× 回退） |
| `upscale_ui_or_redraw` | 82 | 0.016 Gpx | 0.258 Gpx | UI 元素：超分或矢量重绘二选一 |
| `procedural_regen` | 74 | 0.008 Gpx | 0.135 Gpx | 纯色/渐变遮罩按目标分辨率程序化重建 |

`upscale_art` 按类别：cg 2,321 + tachie 592 + bg 187 + title 12
（其中 8 张 `_big` 已按 2× 源处理）。

### 4.1 目标倍率（用户指定：4× / 8×，不改）

本项目超分**只产出 4× 与 8× 两档**，由远端 `img2_upscale` PyTorch 流水线
（Real-ESRGAN x4 [+ waifu2x x2] → lossless WebP）执行：

| 倍率 | 全屏画布 | 全量输出像素 | 用途 |
|---:|---|---:|---|
| 4× | 3200×2400 | 23.3 Gpx | 主用：移植版高清素材 |
| 8× | 6400×4800 | 93.1 Gpx | 超高清母版；磁盘/显存开销大，按需 |

- 输出为 lossless WebP；`--scale 8` 默认同时产出 4× 与 8× 两套树。
- 4× 输出本身超过 WebP 单边上限（16383）的 **4 个超宽滚动背景**会被跳过并记录
  （5054×600、4627×600 等，源宽 > 4095）；8× 超限的 **15 个源**按流水线语义以
  4× 内容写入 `_8x` 树（引擎按实际尺寸识别；另有 4 个在 4× 阶段即超限被跳过）。
- 本机 ncnn 工具链（`tools/upscale/run_realesrgan.sh`）只在**没有 GPU 服务端**时
  作为临时备选；2×/3× 方案已作废（见 `UPSCALE-TOOLCHAIN.md` 顶部注记）。
- 磁盘提示：4× 全量约数 GB～十余 GB，8× 全量需数十 GB（当前余量约 63 GB），
  建议先 4×、8× 按需分批。

执行方式（服务端部署、客户端命令、输出布局）见
[`UPSCALE-REMOTE-PIPELINE.md`](UPSCALE-REMOTE-PIPELINE.md)。

### 4.2 输出目录约定（远端流水线）

```
assets/hd/<tag>_4x/<归档内相对路径>.webp      # 4x
assets/hd/<tag>_8x/<归档内相对路径>.webp      # 8x（--scale 8 时同时产出）
```

示例：`assets/raw/bg/ev02_133_big.png` →
`assets/hd/bg_4x/ev02_133_big.webp`（+ `assets/hd/bg_8x/ev02_133_big.webp`）。

- 文件是服务端返回的 lossless WebP **原样落盘**，不二次编码；
  移植引擎需支持 WebP 解码（SDL_image / libwebp）。
- 同内容别名（r18/base 重复）在对应 tag 目录用硬链接补齐，不重复跑 GPU。
- `upscale-plan.json` 里的 `out_rel_path` 是规划阶段的 4× 镜像 PNG 路径，仅作参考；
  实际输出以上述布局为准；每条任务字段为
  `rel_path, logical_key, category, method, scale, target_w/h, priority, reason, aliases`。

---

## 5. 工程目录（本次新增）

```
ef - the first tale/
├── README.md                     # 工程总览与当前进度
├── docs/                         # 逆向规格文档 + 本报告 + HD 工具链文档
│   ├── ASSET-PIPELINE.md         # ← 本文件
│   ├── UPSCALE-TOOLCHAIN.md      # 本机 ncnn 高清化工具链（无服务端时备选）
│   ├── UPSCALE-REMOTE-PIPELINE.md# 远端 PyTorch 4x/8x 流水线：输入/输出适配
│   └── （原有 PAZ_FORMAT / SCRIPT-REFERENCE / SAVE_FORMAT …）
├── tools/
│   ├── paz.py                    # 基准 PAZ 读取（纯 Python，可信对拍实现）
│   ├── paz_fast.py               # 加速读取（numpy rev4 + C RC4），语义一致
│   ├── toc_scan.py               # 13 归档 TOC 导出
│   ├── extract_assets.py         # 图像资源批量解包 + manifest
│   ├── build_image_inventory.py  # 校验/分类/去重/HD 计划生成
│   └── upscale/                  # 超分脚本
│       └── remote/               # 远端 img2_upscale 流水线的 I/O 适配客户端
├── assets/
│   ├── raw/                      # ★ 原始解包资源（5,989 文件 / 1.87 GB）
│   ├── manifest/                 # 抽取记录 + 清单 + HD 计划 + SUMMARY.md
│   ├── hd/                       # 4x/8x 输出（<tag>_4x|_8x/...webp）
│   ├── upscaled/                 # 预留
│   └── fonts/MerriweatherMG.ttf  # 从 exe 提取的原始字体
├── work/
│   ├── toc/entries.jsonl         # 13 归档完整 TOC（12,289 条）
│   ├── verify/                   # 独立验证报告与脚本
│   ├── upscaler/                 # 本机超分环境探测原始输出
│   ├── upscale-remote/           # 远端流水线报告（跳过/失败列表）
│   └── logs/                     # 解包/清单运行日志
└── src/                          # 移植引擎源码（下一步）
```

---

## 6. 复现命令

```bash
cd "/home/lrxin/Ef_Linux/ef - the first tale"
GAME="/home/lrxin/Downloads/ef - the first tale"

# 1) 全量 TOC（13 归档，12,289 条，<1s）
python3 tools/toc_scan.py --game-root "$GAME" \
    --out work/toc/entries.jsonl --stats work/toc/stats.json

# 2) 图像资源解包（6 归档，34s；含与基准实现的抽样对拍 + 归档 SHA-256）
python3 tools/extract_assets.py --game-root "$GAME" \
    --out-dir assets/raw --manifest-dir assets/manifest \
    --jobs 6 --verify-reference 3 --archive-digests

# 3) 校验 + 分类 + 去重 + 生成 HD 计划（约 30s，含 3,758 张 PNG 全量解码）
python3 tools/build_image_inventory.py --project-root . --jobs 8

# 4) 独立验证（84s，基准实现重读对拍；退出码 0 = PASS）
python3 work/verify/verify.py
```

单独函数：

```bash
python3 tools/paz.py        list   "$GAME/bg.paz" --filter '\.png$'
python3 tools/paz.py        extract "$GAME/st.paz" /tmp/st --filter '\.ani$'
python3 tools/paz_fast.py   "$GAME/sys.paz" --samples 3   # 与基准实现逐字节自检
```

---

## 7. 已知限制 / 未覆盖

1. **未做像素级视觉验证**：PNG 校验只证明"结构完整、可解码、尺寸一致"，没有
   参考渲染图证明每张图是游戏期望的美术语义。
2. **`.ani` 内部结构未解码**（仅头部/配对/大小统计）；2× 缩放坐标需要先逆向格式。
3. **归档加载顺序已确认（2026-10-26）**：引擎按 `scr,bg,bg,st,sys,bgm,voice,se,se,mov` 顺序注册 10 个读取器
   （`0x5e0148`，各 0x270 字节；运行时 strace 观测到对应 `.paz` 打开顺序，r18 patch 紧随各自 base）。
   `bg ↔ st` 同名不同内容的立绘由 stage 立绘加载链按 **st 优先、bg 回退** 解析（`0x4df99c→0x5081a0→0x5068e0`），
   移植版已按此实现；详见 `06-RE-ROADMAP.md`。
4. **PSD 未参与高清化**：3 个 PSD 是制作源（icons/msgPanel/gaiji），保留作参考，
   其是否被引擎加载仍未验证。
5. **未做实际超分**：本步只产出"待超分集合 + 计划"；超分本身是下一步（见
   `UPSCALE-TOOLCHAIN.md`）。
6. **音频/视频/脚本**未在本次解包范围内。

---

## 8. 下一步

1. **接上 GPU 服务端**：按 [`UPSCALE-REMOTE-PIPELINE.md`](UPSCALE-REMOTE-PIPELINE.md)
   部署 `img2_upscale` 服务端，先 `--scale 4 --limit 10` 抽样验收画质/alpha，
   再全量 4x；8x 按需（磁盘/网络/GPU 时间更大）。
2. **产物布局**：`assets/hd/<tag>_4x|_8x/<归档内相对路径>.webp`（lossless WebP，
   流水线原样输出）。移植引擎需要 WebP 解码（SDL_image / libwebp）；
   如改用 PNG，后续加一步无损转码。
3. **程序化项**：74 份纯色/渐变遮罩按目标分辨率重绘；465 张缩略图运行时从 4× 高清 CG
   降采样（不预烘文件，1× 回退，不要单独超分）；82 个 UI 元素先区分"装饰图"与"烧死文字"，后者交给
   引擎 FreeType 重绘（见 `docs/03-TEXT-AND-RENDERING.md`）。
4. **逆向 `.ani` 内部结构**（帧数/区域坐标），实现随 PNG 倍率的坐标缩放；
   同时确认 `bg↔st` 同名条目的引擎加载顺序（`06-RE-ROADMAP`）。
5. **进入移植 M1**（`docs/05-PORT-PLAN.md`）：实现 C++/SDL3 数据层，用 `assets/raw`
   做基准、`assets/hd` 做高清替换验证资源加载。
