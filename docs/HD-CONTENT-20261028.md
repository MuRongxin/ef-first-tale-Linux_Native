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
| CG / 背景 / 立绘 / 标题等美术 | 3,208（含 25 张超宽背景） | 远端 Real-ESRGAN 流水线 4×/8×；超宽 8× 因超 WebP 单边上限（16,383）使用无损 PNG |
| UI | 93 | 83 张两档（远端批次），10 张程序化生成 |
| 遮罩 mask | 26 | 程序化：最近邻保留离散样本 |
| 色带 tint | 47 | 程序化：最近邻保留 1px 采样 |
| 程序化 bg 1 张 + 程序化 UI 10 张 | 11 | 程序化：RGBA 图像用 Lanczos，坐标/样本不被模型改写 |
| 缩略图 thumb | 466 | 运行时从对应高清 CG（4×）裁剪缩放，登记 `derived_from`，不产出文件 |

- 程序化资源合计 **84 张**（mask 26 + tint 47 + ui 10 + bg 1），输出 4×/8× PNG。
- 25 张超宽背景为真 8×：高度 4,800，宽度 20,080～40,432，按源 SHA-256 去重为 19 个 GPU 任务，其余用硬链接；替换了旧 `_8x` 树中的 4× 回退文件（旧文件备份在 `work/upscale-remote/true8-old-fallback/`）。
- 缩略图派生源默认按同名高清 CG 匹配（r18 优先）；5 个裁剪位置例外记录在 `examples/thumbnail.sources.json`，其余为整图。

## 3. 运行时实现

- **候选解析**：`Content::candidates` 只返回 8×/4×；具体图像必须两档齐全（`--hd-only`）。派生缩略图没有自带档位，总是取源图的 4× 再裁剪。
- **派生缩略图**：清单字段 `derived_from {asset, crop[4]}`；裁剪使用源图设计坐标，按目标显示尺寸等比换算后最近邻采样，保留 alpha。
- **按显示约束解码**：纹理上传尺寸同时受 GPU 最大纹理边长和 512 MiB 缓存预算约束；超限时直接从高清源按边长/预算解码，不再退回原图。
- **PNG 流式降采样**：超过 512 MiB 全图预算的非交错 PNG 用 libpng 逐行读取并做区域平均缩小，避免展开超宽全景的完整 RGBA。
- **缓存**：普通图像 LRU 上限 512 MiB，滤色色带独立 4 MiB；纹理缓存键包含派生尺寸与裁剪，避免不同缩略图命中同一缓存。
- **图鉴**：缩略图从高清 CG 后台派生并写可选磁盘缓存；查看时只驻留一张高清纹理，4×/8× 跟随窗口状态。

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

# 4) 全量生产导入（要求每张具体图像都有 4×+8×，缩略图登记运行时派生）
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
- 高清覆盖审计：AI 缺口 0；程序化 84 张、运行时派生 466 张为预期非 AI 项；8× 历史 4× 回退 0（超宽 25 张已真 8×）。

## 6. 边界与后续

- **未做完整画质验收**：覆盖审计只查文件存在、格式与头部尺寸；远端批次另做了解码/尺寸/透明通道与 SHA-256 校验，不替代逐图人工画质检查。
- **KTX2 未接入**：仍为 PNG/WebP 同步解码并上传 RGBA；KTX2 BC7/ASTC 安装缓存与后台预取仍是后续项。
- **超宽纹理**：目前按显示约束降采样解码；分块上传/分块绘制超宽全景尚未实现，24,000 宽级的 8× 源不会被整张上传。
- **466 张缩略图**首次打开图鉴时按需派生，受后台队列与缓存预算影响；生产包不再携带 1× 缩略图回退。
- **`.ani` 2,226 项**仍未解出帧结构，继续静态配对 PNG；不因高清补齐而视为已支持。
- 旧 1× blob 仍保留在 `content/blobs/` 供回退部署，不删除源素材；生产清单不再引用。
- 8× 超宽使用 PNG 而非 WebP，单文件体积明显更大，属于 WebP 尺寸上限的已知代价。
