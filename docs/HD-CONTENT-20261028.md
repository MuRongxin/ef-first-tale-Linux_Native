# 全量高清运行包（2026-10-28）

本文记录运行包从“美术 4×/8× + 633 张 1× 原图回退”切换为**无 1× 档位的生产清单**的数据构成、生成方式、运行时实现与验收边界。历史批次（非 UI 美术、UI、超宽 8×）见 [远端超分流水线](UPSCALE-REMOTE-PIPELINE.md) §9～§11。

## 1. 结论

- `content/manifest.json`：`development=false`，共 **3,758 个图像 ID**：
  - **3,292 张具体图像**，每张都有完整 **4× + 8×**（6,584 个档位）；
  - **466 张缩略图**由运行时从高清源图裁剪派生，不预烘文件，不单独走 AI。
- 生产包内 **1× 档位引用为 0**；渲染器不再把原图作为候选或回退。
- 84 张程序化资源（遮罩 / 色带 / 纯色 UI）已确定性生成两档；25 张超宽背景补齐真 8×；83 张 UI 补齐两档。
- 图片、音频、视频、剧本、布局仍按内容哈希去重存放；`content/` 不入 Git，可由原始素材与工具重建。

## 2. 数据构成

| 类别 | 数量 | 处理方式 |
|---|---:|---|
| AI 美术与 UI（含下述 83 张 UI） | 3,208（含 25 张超宽背景） | 远端 Real-ESRGAN 流水线 4×/8×；超宽 8× 因超 WebP 单边上限（16,383）使用无损 PNG |
| UI | 93 | 83 张两档（远端批次），10 张程序化生成 |
| 遮罩 mask | 26 | 程序化：最近邻保留离散样本 |
| 色带 tint | 47 | 程序化：最近邻保留 1px 采样 |
| 程序化 bg 1 张 + 程序化 UI 10 张 | 11 | 程序化：RGBA 图像用 Lanczos，坐标/样本不被模型改写 |
| 缩略图 thumb | 466 | 运行时从对应高清 CG（4×）裁剪缩放，登记 `derived_from`，不产出文件 |

- 程序化资源合计 **84 张**（mask 26 + tint 47 + ui 10 + bg 1），输出 4×/8× PNG。
- 25 张超宽背景为真 8×：高度 4,800，宽度 20,080～40,432，按源 SHA-256 去重为 19 个 GPU 任务，其余用硬链接；替换了旧 `_8x` 树中的 4× 回退文件（旧文件备份在 `work/upscale-remote/true8-old-fallback/`）。
- 缩略图派生源默认按同名高清 CG 匹配（r18 优先）；5 个裁剪位置例外记录在 `examples/thumbnail.sources.json`，其余为整图。

## 3. 运行时实现

- **候选解析**：生产包的 `Content::candidates` 只返回 8×/4×；图片刷新工具严格要求两档齐全。完整导入的 `--hd-only` 禁止 1×、至少要求 4×（8× 可后补）；本次实际生产包两档均完整。派生缩略图没有自带档位，总是取源图的 4× 再裁剪。
- **派生缩略图**：清单字段 `derived_from {asset, crop[4]}`；裁剪使用源图设计坐标，按目标显示尺寸等比换算后最近邻采样，保留 alpha。
- **分块渲染**：超宽/大图保留小预览，后台生成无损分块缓存；按视口上传原倍率像素并预取邻块，不再为详细视口整体缩小源图。详见 [分块渲染与预取](TILE-STREAMING-20261029.md)。
- **PNG 流式降采样**：超过 512 MiB 全图预算的非交错 PNG 用 libpng 逐行读取并做区域平均缩小，避免展开超宽全景的完整 RGBA。
- **缓存**：普通图像 LRU 上限 512 MiB，滤色色带独立 4 MiB；纹理缓存键包含派生尺寸与裁剪，避免不同缩略图命中同一缓存。
- **图鉴**：缩略图从高清 CG 后台派生并写可选磁盘缓存；查看普通图时只驻留一张高清纹理，超宽/大图共用分块路径；4×/8× 跟随窗口状态。

## 4. 生成与更新

```bash
cd "/home/lrxin/Ef_Linux/ef - the first tale"
GAME="/home/lrxin/Downloads/ef - the first tale"   # 只读

# 1) 审计实际高清覆盖（只读文件/格式/头部尺寸，不解码全部像素）
python3 tools/audit_hd_coverage.py --jobs 4

# 2) 程序化资源两档（遮罩/色带/纯色 UI；不使用 AI）
python3 tools/prepare_procedural_hd.py --jobs 2

# 3) AI 资源由远端流水线生成（UI 批次、超宽真 8×）
#    见 docs/UPSCALE-REMOTE-PIPELINE.md §10 / §11

# 4) 全量生产导入（禁止 1×，至少要求 4×；当前资源两档均完整，缩略图运行时派生）
python3 tools/prepare_content.py --all --hd-only --game-root "$GAME" --jobs 4
```

已有完整包、只更新图片时用增量刷新（保留剧本/音频/视频/UI 布局）：

```bash
python3 tools/refresh_content_images.py --jobs 2
```

`refresh_content_images.py` 在包锁内校验现有 `content/` 的图片 ID 集合、源 SHA-256 与设计尺寸都与当前清单一致；任一图片缺少完整 4×/8× 就失败并保留旧 manifest，成功后原子发布 `development=false` 的新清单，并保留旧清单到 `work/hd-replacement/manifest.before.json`。

**假扩展名冲突修复**：归档中存在同尺寸不同内容的 `iconson.png` 与 `iconson.png-`、`pauseAnime.png` 与 `pauseAnime.png-` 等。高清输出名对 `.png-` / `.png.tmp` 保留完整源文件名（`<name>.png-.webp`），普通 `.png` 仍为 `<stem>.webp`；`prepare_content.py`、`ef_client.py`、`fix_large.py` 与覆盖审计使用同一规则。远端续跑会保留已分块制作的真 8× PNG，不重新写入 4× WebP 回退。

缩略图来源与裁剪规则在 `tools/prepare_content.py::thumbnail_sources`：未列出的缩略图必须能唯一匹配同名具体图像，否则导入直接失败，不做猜测。

## 5. 验收

2026-10-28 本机验证（当前工作区）：

- `cmake --build --preset dev` 成功；`TMPDIR=/tmp ctest --preset dev --output-on-failure` **42/42 PASS**（含 `hd_images`、`hd_content_refresh`、`title_artwork`、图鉴/存读档/首页窗口回归）。
- 受影响的 ASan 定向测试 `gallery_catalog` / `core` / `hd_images` **3/3 PASS**。
- `./build/dev/ef_native --content content --audit-resources`：退出码 0，14,955 次引用 / 9,160 种，缺失 0、歧义 0、包内丢失 0。
- `--audit-scripts`：退出码 0，94 剧本仅核心控制流外命令按既有统计记录。
- `--audit-gallery`：退出码 0，2,306 个差分。
- `python3 work/verify/verify.py`：M0 独立验证 **PASS、failures=0**（5,989 文件哈希、254 条抽样重读、3,758 PNG 解码、2,226 ANI 配对）。
- 高清覆盖审计：AI 缺口 0、程序化缺口 0；466 张缩略图按设计不产出预烘文件；8× 历史 4× 回退 0（超宽 25 张已真 8×）。

## 6. 边界与后续

- **未做完整画质验收**：覆盖审计只查文件存在、格式与头部尺寸；远端批次另做了解码/尺寸/透明通道与 SHA-256 校验，不替代逐图人工画质检查。
- **KTX2 未接入**：普通图像与过渡预览仍同步解码 PNG/WebP 并上传 RGBA；超宽图分块的后台读取/预取已接入，KTX2 BC7/ASTC 安装缓存仍是后续项。
- **超宽纹理**：原倍率分块与后台预取已接入；首次预览仍同步解码，44 块以上的缩小总览使用预览，失败原因会记录。KTX2 尚未实现。
- **466 张缩略图**在引用时按需派生；图鉴按页请求并受后台队列与缓存预算影响，生产包不再携带 1× 缩略图回退。
- **`.ani` 2,226 项**仍未解出帧结构，继续静态配对 PNG；不因高清补齐而视为已支持。
- 旧 1× blob 仍保留在 `content/blobs/` 供回退部署，不删除源素材；生产清单不再引用。
- 8× 超宽使用 PNG 而非 WebP，单文件体积明显更大，属于 WebP 尺寸上限的已知代价。


## 7. 继续验收与控制图元数据清理

- 四张横向 `th_*.png` 采用原图裁剪/缩放匹配及人工查看确认来源；`ev02_090a01.png` 对应 `bg/ev02_090.png`。这些参数为移植侧图像匹配值，**不是原版 exe 坐标表逆向结论**。
- 程序化 RGBA 输出不携带源灰阶 ICC 等不适用的元数据，避免 libpng 报配置文件与像素格式不一致；原生解码器对修改前后 10 个档位的 RGBA 逐字节比较一致。
- 原生运行时逐一解码全部 466 个派生缩略图和 198 个 PNG 档位（168 程序化、25 真 8×、5 原有超宽 4×）通过；未重复宣称旧美术 WebP 全量深度解码。
- 真实首页与图鉴窗口检查通过，包括分页、差分、缓存、退出/重入、Borderless/最大化往返。
- 分块接入前的测量：最宽背景在真实 OpenGL 路径分别选择 4×（20,216×2,400）和真 8×（40,432×4,800）来源；受 max_texture=16,384 限制实际上传 16,383×1,945，两次均未选择 1×。
- 最终清单：3,758 图片 ID、6,584 高清档位、466 运行时派生；1×、丢失文件、错误尺寸、包外路径、脚本资源缺失/歧义均为 0。剧本、音视频、字体和布局入口与刷新前一致。
- 最终 M0 再跑 PASS / 0 失败；42 项 dev、3 项定向 ASan/UBSan 与真实窗口回归通过。

新增证据：`work/hd-replacement/FINAL-AUDIT.json`、`runtime-decode.json`、`profile-pixels.log`、`gallery-real-wm.log`、`wide-window.log`、`m0-final.log`。运行产物不入库。
