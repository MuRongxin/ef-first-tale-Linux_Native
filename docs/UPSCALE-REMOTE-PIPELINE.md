# 10 — 远端 PyTorch 4x/8x 高清化流水线：ef 项目适配说明

> 对象：`/home/lrxin/文档/img2_upscale`（原 WA2 8x 放大流水线：远端 PyTorch
> Real-ESRGAN x4 + waifu2x swin_unet x2 → lossless WebP，本地并发客户端）。
>
> 本次**只适配输入与输出**：倍率 **4x / 8x 不变**、模型/推理顺序不变、
> lossless WebP 不变、HTTP 协议不变、`img2_upscale/server/` 一行代码不改。
> 新增的适配客户端：`tools/upscale/remote/ef_client.py`。

---

## 1. 输入映射

| 流水线原假设 | ef 项目适配后 |
|---|---|
| 源目录 `bg/ sprite/ bg_cn/`，仅顶层 | `<proj>/assets/raw/<tag>/...`，6 个 tag（`bg st sys r18_bg r18_st r18_sys`），递归子目录（如 `sys/thumb/`） |
| 输入格式 TGA/BMP | **PNG**（服务端 `server/images.py` 本就支持 PIL 解码 PNG，无需改） |
| 目录扫描决定任务 | 读 `<proj>/assets/manifest/upscale-plan.json`：只取 `method=upscale_art`（默认），自动跳过 thumbnail / mask / tint / `.ani` / `.psd` / 文本 |
| 无 | 4x 输出本身超过 WebP 单边上限的源（本项目 4 个超宽滚动背景）直接跳过并记录，避免必然失败 |

## 2. 输出映射

| 流水线原假设 | ef 项目适配后 |
|---|---|
| `<源目录>_4x/<stem>.webp`、`<源目录>_8x/<stem>.webp` | `<proj>/assets/hd/<tag>_4x/<归档内相对路径>.webp`、`<tag>_8x/...` |
| 输出格式 lossless WebP | **不变**（服务端返回的 WebP 原样落盘，不二次编码） |
| 同目录写入 | 项目目录；`.partial` 原子替换；已存在且尺寸=源×倍率则跳过（续跑） |
| 无 | `upscale-plan.json` 里同内容别名（r18/base 重复）用硬链接补到对应 tag 目录，不重复跑 GPU |

示例：

```
assets/raw/bg/ev02_133_big.png      → assets/hd/bg_4x/ev02_133_big.webp
                                    → assets/hd/bg_8x/ev02_133_big.webp
assets/raw/st/st_hiro01e06.png      → assets/hd/st_4x/st_hiro01e06.webp
assets/raw/sys/thumb/ev02_187.png   → assets/hd/sys_4x/thumb/ev02_187.webp   （默认不做，见 §5）
```

## 3. 倍率语义（未改）

| 参数 | 行为 |
|---|---|
| `--scale 4` | Real-ESRGAN x4 → 4x 输出（800×600 → 3200×2400） |
| `--scale 8`（默认） | x4 后再 waifu2x x2 → 8x（6400×4800）；默认同时保存 4x 与 8x 两套树 |
| `--scale 8 --no-stage4` | 只保存 8x |
| 8x 超 WebP 上限（源宽/高 > 2047） | 按原流水线语义：只请求 4x，并把 4x 结果同时写入 `_8x` 目录（引擎按实际尺寸识别）；本项目有 15 个（多为滚动背景；另有 4 个太宽，4x 阶段即跳过） |
| 4x 超 WebP 上限（源宽/高 > 4095） | WebP 无法承载：**跳过**并写入 `work/upscale-remote/skipped_4x_over_webp_limit.txt`（本项目 4 个，见 §6） |

> 倍率是**相对源图像素**的（与流水线原语义一致）。本项目并非所有源都是 800×600：
> 滚动背景更宽/更高，8 张 `_big` CG 本身已是 1600×1200，因此 4x 后尺寸为 6400×4800
> （相当于逻辑画面的 8x）。移植引擎按图片实际尺寸识别缩放即可。

## 4. 服务端部署（沿用原 README 流程，不改代码）

需要一台有足够显存的 GPU 机器（原设计 RTX 5090；本机 GTX 960M/4GB 不是目标环境）。

```bash
cd /home/lrxin/文档/img2_upscale
python3 deploy.py build
python3 deploy.py upload --host <HOST> --port 22 --user <USER> --no-password
python3 deploy.py env    --host <HOST> --port 22 --user <USER> --no-password
python3 deploy.py setup  --host <HOST> --port 22 --user <USER> --no-password
python3 deploy.py start  --host <HOST> --port 22 --user <USER> --no-password
python3 deploy.py health --url http://127.0.0.1:6006      # 或 AutoDL 公网 https://...:8443
```

本机通过 SSH 隧道访问（与流水线 README 一致）：

```bash
ssh -N -L 6006:127.0.0.1:6006 <USER>@<HOST> &
```

## 5. 客户端用法

```bash
cd "/home/lrxin/Ef_Linux/ef - the first tale"

# 0) 不联网，只看任务/输出映射
python3 tools/upscale/remote/ef_client.py --dry-run

# 1) 小批验收（先 4x 再 8x，各 10 张）
python3 tools/upscale/remote/ef_client.py \
    --api http://127.0.0.1:6006/v1/upscale --scale 4 --categories cg --limit 10
python3 tools/upscale/remote/ef_client.py \
    --api http://127.0.0.1:6006/v1/upscale --scale 8 --categories cg --limit 5

# 2) 全量 4x（推荐先做这个）
python3 tools/upscale/remote/ef_client.py --api http://127.0.0.1:6006/v1/upscale --scale 4

# 3) 全量 8x（同时产出 4x + 8x；磁盘/网络/GPU 时间都更大）
python3 tools/upscale/remote/ef_client.py --api http://127.0.0.1:6006/v1/upscale --scale 8

# 4) 断点续跑：直接重跑（已有且尺寸正确自动跳过）；失败列表重跑：
python3 tools/upscale/remote/ef_client.py --api ... --retry-failed "assets/hd/upscale_failed.txt"
```

常用参数：`--methods upscale_art,upscale_ui_or_redraw`（把 UI 也做上）、
`--categories cg,tachie,bg`、`--match <子串>`、`--limit N`、`--concurrency N`、
`--timings-csv work/upscale-remote/timings.csv`。

## 6. 本项目数据规模与已知边界

- 待放大：**3,112 张**（`method=upscale_art`，去重后），1,454.5 Mpx。
- 4x 输出 ≈ 23.3 Gpx；8x 输出 ≈ 93.1 Gpx。
- 4x 超 WebP 上限（跳过，4 个）：`bg/bg_scl008a01_03.png`(5054×600)、
  `st/bg_scl001a01_03_n.png`、`st/bg_scl001a01_02_n.png`、
  `st/bg_scl001a01_01_n.png`(均 4627×600)。
- 8x 超 WebP 上限（回退为 4x 内容写入 `_8x`，15 个）：滚动背景等源宽 > 2047 的图
  （另有 4 个源宽 > 4095，4x 阶段即跳过，见上一条）。
- 输出体积（参考值，按 lossless WebP 实测/估算）：4x 单张约 1–4 MB、
  8x 单张约 4–10 MB；**8x 全量需要数十 GB 磁盘**（当前可用约 63 GB），
  建议先 4x，8x 按需分批或换大盘。

## 7. 与项目其它工具的关系

- `tools/upscale/run_realesrgan.sh`（本机 ncnn 4x）：只作为**没有 GPU 服务端**时的
  临时备选；一旦远端流水线可用，以本目录客户端为准。
- `assets/manifest/upscale-plan.json`：只用于任务选择（方法/类别/优先级/别名）；
  其 `out_rel_path` 是规划阶段的 4x 镜像路径，**实际远端输出布局以上文 §2 为准**。
- WebP 需要移植引擎提供解码（SDL_image / libwebp 均可）；如需 PNG，
  可在流水线跑完后加一步无损转码（另立任务）。

## 8. 状态 / 待办

- [x] 客户端 I/O 适配（本文件 + `ef_client.py`）
- [x] 4x/8x、续跑、别名、8x 回退逻辑（协议/尺寸用本地 mock 服务端验证过；
      按用户要求不再做本地测试）
- [x] GPU 服务部署与抽样：旧美术批次见 §9；2026-10-28 新节点补齐 UI 批次见 §10
- [x] UI 补齐：2026-10-28 采用 `upscale_ui_or_redraw` 补做两档；烧死文字仍可后续矢量重绘

---

## 9. 实际部署记录（2026-10-24）

### 9.1 远端环境

| 项 | 值 |
|---|---|
| 主机 | `ubuntu@117.50.217.166`（SSH 22，密码认证；本机通过 `work/upscale-remote/` 的 gitignored 凭据自动登录） |
| 系统 | Ubuntu 22.04 / kernel 5.15，8 核 / 62 GB / 97 GB 盘（实际使用前 4.1 GB） |
| GPU | **Tesla P40 24 GB（compute capability 6.1 / Pascal / sm_61）**，驱动 550.127.08 |
| 部署目录 | `/home/ubuntu/wa2-pytorch`（bundle 解压）；运行环境 `/home/ubuntu/wa2-env`（Python 3.10.12 venv） |
| PyTorch | **2.5.1+cu121**（`WA2_TORCH_INDEX=.../whl/cu121`）。cu128 不支持 Pascal；cu121 wheel 含 sm_60 SASS，对 sm_61 向下兼容，实测 matmul/conv2d 正常 |
| 服务端 | `server/wa2_server.py` @ 6006，`WA2_API_TOKEN=wa2-local-5090`（默认），模型已常驻 |
| 旧服务 | 原 ncnn API（`~/wa2/upscale_api.py`）已停止，避免占用 6006 |

### 9.2 P40 生产参数（与 5090 默认不同）

P40 的 fp16 吞吐仅为 fp32 的 1/64，且无 Tensor Core，因此**关闭半精度**：

```bash
cd /home/ubuntu/wa2-pytorch
export WA2_WORK_ROOT=$HOME WA2_REAL_FP16=0 WA2_WAIFU_AMP=0 \
       WA2_WAIFU_TILE=512 WA2_WEBP_METHOD=4 WA2_API_PORT=6006
bash server/run_server.sh
```

- `WA2_WAIFU_TILE=512`：768/1024 会 CUDA OOM（P40 22.4 GiB 可用），512 峰值约 6.5 GB 稳定。
- `WA2_WEBP_METHOD=4`：lossless 不变，method 2/0 提速有限但体积明显变大，选 4 平衡。
- 4× = Real-ESRGAN x4（3200×2400 起）；8× = x4 后 nunif waifu2x **swin_unet** x2（6400×4800 起），一次请求同时返回两档。

### 9.3 实测（800×600 资源）

| 指标 | 值 |
|---|---|
| GPU 耗时 | x4 ≈ 0.9–5.4 s（cudnn 算法切换影响）+ x2 ≈ 10–11 s ≈ **16 s/张** |
| CPU 编码 | lossless WebP：4× ≈ 8 s + 8× ≈ 27 s/张（8 线程并行、与 GPU 重叠） |
| 端到端吞吐 | **GPU 瓶颈**：稳定后约 **280 张/小时**（GPU 100%、SM 时钟 1531/1531 MHz、温度 ~67 °C；CPU 只用 2–4 核做 WebP 编码，空闲属正常） |
| 首轮预热 | 前 ~12 张 x4 约 5.2 s（cuDNN autotune 预热），之后稳定 **~0.95 s**；x2 稳定 ~9.9 s → 单张 GPU ≈ 10.9 s |
| 单张产物 | 4×：1.4–3.2 MB；8×：4.5–9.5 MB |
| 全量估算 | 预热后 3,108 张 ≈ **11 小时**（首版按 180–200 张/小时估 15–17 h 偏保守），产物约 **32 GB**（4× ≈ 8 GB + 8× ≈ 24 GB） |

### 9.4 全量任务（已启动）

- 远端输入：`~/ef-remote/`（`assets/raw` 的 3,112 张 art 源图 1.59 GB + `upscale-plan.json` + `ef_client.py`，tar 上传 1.5 GB）。
- 启动命令（nohup，抗断线）：

```bash
cd /home/ubuntu/ef-remote
nohup python3 tools/upscale/remote/ef_client.py \
  --api http://127.0.0.1:6006/v1/upscale \
  --scale 8 --concurrency 6 --log-file run.log > client.out 2>&1 &
echo $! > client.pid
```

- 输出：`~/ef-remote/assets/hd/<tag>_4x|_8x/...webp`；跳过 4 个 4× 即超 WebP 上限的超宽背景
  （`work/upscale-remote/skipped_4x_over_webp_limit.txt`），15 个 8× 超限按官方语义写 4×。
- 进度：`ssh ubuntu@117.50.217.166 'tail -5 ~/ef-remote/run.log'`；
  失败列表：`~/ef-remote/assets/hd/upscale_failed.txt`；完成后重跑即断点续传。
- 回传：完成后把 `~/ef-remote/assets/hd` 下载到本工程 `assets/hd`（约 32 GB，
  可用 `rsync -av --partial` 或分 tag 打包 scp）。
- 重启服务：远端无 systemd 自启，机器重启后按 §9.2 重新 `run_server.sh` 即可（模型加载约 20 s）。

### 9.5 全量结果（2026-10-24 完成）

| 项 | 结果 |
|---|---|
| 任务 | **3,112** 条 `upscale_art` → 4×/8× 两套共 **6,224** 个输出文件（+ 13 个别名硬链接 → 6,250 个文件） |
| 耗时 | 第一轮 09:53 起跑（1,972 张后因特大图 OOM 中断）；21:09 第二轮补完 1,133 张（0 失败） |
| 体积 | 约 **35 GB**（4× 树约 9.5 GB + 8× 树约 25 GB） |
| 校验 | 全量尺寸校验 **6,224 OK / 0 缺失 / 0 不符**；本地与远端 **SHA-256 全量一致**（6,250 文件） |
| 特殊项 | 4 张超宽背景 4× 即超 WebP 上限 → **PNG 输出**（8× 树放同一 4× PNG）；19 张 8× 超限 → 8× 树放 4× 内容（官方语义） |

**中途事故与修复**：15:54 一张 1581×1637 特大图在 REAL x4 阶段 CUDA OOM（4× 输出 41 MPx），
PyTorch 缓存/碎片占满 22.8 GB 后 16:19 起连锁失败，客户端 16:38 退出（1,972 ok / 1,136 failed）。
处理：重启服务 + `PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True`；把 7 张 x4 > 32 MPx
的特大图从主队列剔除；并发 6→4；重跑后 1,133 张全部成功、0 失败。

**7 张特大图的补跑**：`tools/upscale/remote/fix_large.py`（客户端分块推理）
按 768px 源块 + 96px 重叠切图，逐块请求 `/v1/upscale?stages=4[,8]`，再裁边拼接：

| 源 | 4× 输出 | 8× 输出 |
|---|---|---|
| `bg/bg_scl008a01_03` 5054×600 | 20216×2400 PNG（14.0 MB） | 4× 内容 PNG |
| `st/bg_scl001a01_0{3,2,1}_n` 4627×600 | 18508×2400 PNG（27–39 MB） | 4× 内容 PNG |
| `bg/bg_scl004a01_01` 3575×600 | 14300×2400 WebP（2.9 MB） | 4× 内容 WebP |
| `bg/ev00_004_mugai_h2` 1581×1637 | 6324×6548 WebP（6.1 MB） | 12648×13096 WebP（17.7 MB） |
| `bg/bgw_normal011a01_03_10001_b_sepia` 1452×1696 | 5808×6784 WebP（9.0 MB） | 11616×13568 WebP（24.3 MB） |

> 注：分块拼接在重叠区裁掉边缘，理论上与整图推理有极微小差异；这 7 张已包含在
> 上面的尺寸校验与 SHA-256 清单中。远端服务已停止（GPU 0%），需要重跑时按 §9.2 重启即可。


## 10. 新远端重建（2026-10-28）

新节点：`root@cpod-1x0p9cg91q2n-s3.podtcp.compshare.cn`，SSH 端口 `22143`。
密码不记录在文档、源码或命令行参数中。部署根目录 `/workspace/ef-upscale`。

### 10.1 覆盖范围复核

旧批次“全量完成”指 `upscale_art` 美术资源，不代表全部 PNG 已有两档高清。
本次使用已有 `tools/audit_hd_coverage.py` 按本地 manifest 和实际文件头重新审计：

- PNG 共 3,758 张；CG、立绘和标题的旧批次产物仍在。
- `upscale_ui_or_redraw` 缺口 83 张，对应 82 个去重任务；本次补做两档。
- `procedural_regen` 尚有 84 张，需程序化处理，不混入 AI 队列。
- `runtime_derive_from_hd` 为 466 张缩略图，继续运行时派生，不预烘或 AI 超分。
- 25 张背景的 8× 树仍为已记录的 4× 内容，不计作真正 8× 完成。

本地证据：`work/hd-coverage/coverage.json`、`missing.csv`、`upscale-missing.json`。
这是文件头及尺寸审计，不能替代完整解码、哈希和画质验收。

### 10.2 环境及运维入口

- Ubuntu 22.04；RTX 4090，驱动报告显存 49,140 MiB、驱动 610.57.04。
- 复用镜像 Python 3.12.11 / PyTorch 2.13.0+cu132 / torchvision 0.28.0+cu132；
  在 `/workspace/ef-upscale/venv` 创建继承系统包的隔离环境安装 API 依赖。
- 使用原 `img2_upscale` 本地 bundle 中的 Real-ESRGAN 和 nunif 模型，不更换模型。
- 启动入口：`bash /workspace/ef-upscale/start.sh`；半精度开启，waifu tile 512，WebP lossless method 4。
- API 只监听远端 `127.0.0.1:6006`；随机令牌位于远端权限 0600 的 `api.env`，客户端通过环境变量读取。
- 项目输入及输出：`/workspace/ef-upscale/ef-remote`；输入计划仅包含本次缺口。
- 服务日志：`/workspace/ef-upscale/wa2-pytorch/server/server.log`。

SSH 隧道（本地端口可自行改动）：

```bash
ssh -p 22143 -N -L 16006:127.0.0.1:6006 root@cpod-1x0p9cg91q2n-s3.podtcp.compshare.cn
```

任务使用原 `ef_client.py --methods upscale_art,upscale_ui_or_redraw --scale 8`，
一次请求同时产出 4×/8×；先抽样，核对尺寸、alpha 和画面后再补齐队列。

### 10.3 假扩展名冲突修复

逐文件透明通道验收发现 `sys/iconson.png` 与 `sys/iconson.png-` 是同尺寸、不同内容，
旧规则把二者都写为 `iconson.webp`，并发可覆盖、续跑也会误认为已完成。
`pauseAnime.png` / `.png-` 也有同类命名冲突，但属于程序化队列，本轮未送 AI。
修复后，普通 `.png` 仍输出 `<stem>.webp`；`.png-` / `.png.tmp` 保留完整源文件名，
分别输出 `<name>.png-.webp` / `<name>.png.tmp.webp`，子目录保持不变。
客户端、特大图客户端和 content 导入器使用同一规则；本地覆盖审计随导入器更新。
本次清单仅有三张这类假扩展名 PNG，全部属于 UI，已有 CG / 背景 / 立绘产物路径不变。
冲突图片已强制重新制作，不能仅按旧文件尺寸判定成功。
同内容别名仍分别创建各自路径并共享文件内容。

回归用合成的 `.png` / `.png-` / `.png.tmp` 三张同尺寸、不同颜色和 alpha 的图片，
验证两类客户端路径一致且互不冲突，并验证 content 导入后仍保留各自像素和透明度。

### 10.4 本次验收与回传

- 4× 抽样 10 张、8× 抽样 5 张成功；随后补完其余 77 项，0 失败。
- 命名冲突修复后补跑相关源与别名，最终 82 个去重任务对应 83 张图片、166 个文件。
- 远端 166 文件全部完整解码，尺寸和透明通道存在性检查通过；产物合计 180,986,230 字节。
- 归档回传后逐文件 SHA-256 全部一致；本地 `assets/hd` 新增 164 文件，保留已有两张有效 4× `topmenu2` / `topmenu2Over`。
- 本轮只更新 `assets/hd`，未重新打包运行时 `content/`。
- 导入相关 12 项 Python 测试通过；M0 独立验证 PASS（0 失败）。
- 证据：`work/upscale-remote/cpod-validation.json`、`cpod-independent-verify.log`；均不入库。
- 服务保持运行，可复用；环境包快照位于远端 `/workspace/ef-upscale/environment-freeze.txt`。

远端续跑与查看日志：

```bash
bash /workspace/ef-upscale/client.sh --scale 8 --concurrency 4 --log-file resume.log
tail -20 /workspace/ef-upscale/ef-remote/run.log
```

本次输入包已更新为修复后的客户端；复用历史输入包重建时须以当前仓库 `ef_client.py` 替换旧版本。

最终本地覆盖审计：AI 缺口 0；UI 93 张中 83 张两档齐全，剩余 10 张属于程序化队列。
程序化缺口仍为 84 张，运行时派生缩略图 466 张；25 张超宽背景仍沿用已记录的 4× 回退。


## 11. 25 张超宽背景补齐真 8×（2026-10-28）

本节替代 §9 / §10 中这 25 张背景“8× 树仍保存 4× 内容”的当前状态描述；旧批次记录保留作历史。

- 25 个图片 ID，按源 SHA-256 去重为 19 个 GPU 任务；六个重复项使用硬链接。
- 所有图均为原图实际 8×，高度 4,800，宽度 20,080–40,432；超出 WebP 单边上限，使用无损 PNG。
- 保留已有 4×；替换 8× 树中的旧回退文件，旧文件本地备份至 `work/upscale-remote/true8-old-fallback/`。
- 输出 25 文件，去重体积 1,022,242,005 字节；归档约 975 MiB（硬链接避免重复传输）。
- 远端逐文件完整解码、尺寸检查通过；本地归档 SHA-256 与远端一致，25 个安装文件逐项 SHA-256 一致。
- 归档 SHA-256：`c77517580b15dd02d956d4457b48bdfa2eed8a6f237cdb96c25ca9e29f3b18fd`。
- 本次只更新 `assets/hd`，没有重新打包 `content/`；超宽纹理运行时分块仍是独立工作。

### 推理及利用率

沿用原 Real-ESRGAN x4 + nunif swin_unet x2，非把原 4× 文件直接插值拉伸。
源图按不重叠的 1,024px 核心区域切块，各边额外携带 96px 上下文；每块两阶段推理后裁掉上下文再拼接。
客户端对返回块的 8× 尺寸逐块断言，最终按源尺寸乘 8 校验。

RTX 4090 / 驱动报告 48 GiB 显存，20 CPU / 90 GiB 内存：
6 路图片任务供给一个串行 GPU 推理队列，FP16 / AMP 开启、waifu tile 768。
起初中间 WebP 编码导致 GPU 等待；临时服务 `server-fast` @ 127.0.0.1:6007
改用无损 PNG（compress_level=1）传输块，最终 PNG compress_level=2，未修改模型权重或普通服务的编码实现。

优化后连续 10 秒采样：平均 GPU 利用率约 81.7%，7 次为 100%，峰值功耗约 410W、温度最高 60°C、显存约 12 GiB。
优化这一轮约 212 秒（复用初始阶段已完成的 3 个任务）；从初始任务启动到全部完成约 5 分半，不能把 212 秒当作所有任务从零计算时间。

完成并回传后普通与临时超分服务均已停止，GPU 0% / 1 MiB。
**停止服务不等于停止实例计费，仍须在租用平台关机。**

普通客户端现识别并保留已制作的真 8× PNG，续跑不会重新创建 4× WebP 回退与之冲突；
缺少 4× 时仅修复缺少档位，保留已有真 8×。相关回归还检查 PNG 别名保持扩展名。

本地证据：`work/upscale-remote/true8-validation.json`、`true8-plan.json`、
`true8-run.py`、`true8-install.py`；新全量审计位于 `work/hd-true8-final/`。
远端操作脚本和日志保留于 `/workspace/ef-upscale/true8*`，运行产物不入库。

最终全量审计：3,208 张 AI 类图片均有完整 4× / 8×，8× 的历史 4× 回退为 0；剩余缺口仍为 84 张程序化资源与 466 张运行时派生缩略图。8 项导入/客户端回归、5 项打包回归通过，M0 独立验证 PASS（0 失败）。
