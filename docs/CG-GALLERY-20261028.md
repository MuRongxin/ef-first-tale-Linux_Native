# 全开放 CG 图鉴（2026-10-28）

标题的「CG 图鉴」进入分页目录，选择后高清查看，Esc/右键逐级返回图鉴、标题。空存档目录即可访问所有分组与差分，不读取 `cgv.dat`、已读位图或通关标志，也不创建存档。

## 清单与对应关系

数据依据为本地 `assets/manifest/images.csv`、content 图像清单及原有 `sys/r18_sys` 的 `thumb/` 条目；运行时只读 content。使用 `--audit-gallery` 输出可复核 JSON（分组、全部图像 ID、原缩略图对应关系、排除项），无需窗口或游戏进度。完整输出属于本地生成物，不入库。

当前安装清单的 `cg` 分类有 2,327 张 PNG，不能直接视为原版图鉴清单。按编号 `evNN_NNN` / `edNN_NNN` / `evhNN_NNN` 合组，保留同编号的字母、数字、`mugai`、`big` 等全部后缀，不以脚本引用数或是否有缩略图删除差分。事件、结局、成人补充按文件编号分区，不把编号解释成人物或剧情章节。相同编号多个旧缩略图全部记录，高清缩略图优先选其中实际存在的对应 CG。无旧缩略图的编号组也可浏览。

本地原图检查排除：

- 7 张非编号的 `ev00_moshi/asaga`、`ev01_tero_last_*`、`ev01_sky_*_sepia`：字幕/天空演出。
- 6 张 `kiridashi`：两套 Cutin 分层组件，不能作为独立完整 CG。
- `ev01_019`（1 张）：眼部切入；`ev01_033`（3 张）、`ev01_074`（3 张）、`ev01_206`（1 张）：无人背景。

其余背景、立绘、UI、遮罩、色带、动画不通过编号白名单。`thumb/bg_ex*` 与 `thumb/th_*` 亦不作为事件 CG。

这是基于实际素材的移植分组规则及视觉分类，**不是原版 exe 图鉴页序/差分表的逆向结论**。原图检查记录在 `work/gallery-review.png`。原版精确页序仍待取证；未来新增素材需重新审核例外，不能把宽泛 `cg` 标签或归档名称当作验收依据。

## 浏览和缓存

- 每页 12 组；方向键选组、PageUp/PageDown 翻页、Home/End 首尾；Tab 或点击分组文字循环分区。
- Enter/点击进入高清查看；左右键、空格或底部按钮循环切换该组差分；滚轮/加减缩放，WASD 平移，0 恢复适应。返回保留目录页及选中项。
- 正常窗口请求 4×，最大化/Borderless 全屏请求 8×，与分辨率无关；F10/F11 沿用全局切换。切换时重新加载所选档位；缺失/损坏按 content 候选顺序回退，失败可返回，不退出应用。
- 缩略图始终从 4× CG 派生，HD 缺失可回退包内低档 CG。单张最大 256×192，保持长宽比；不使用原低清缩略图预烘高清文件。
- 一个后台线程负责解码与缓存，窗口线程负责 GL 上传；新页面/差分/档位替换待处理队列，过时结果丢弃，不把旧差分冒充新图。每帧处理完成结果，加载时仍可导航与返回。
- WebP 在解码器内缩小，缩略图不先展开成完整 RGBA；PNG 回退使用区域平均缩小。完整查看保留 HD 像素，受 GL 最大边长与 8,192 边长上限约束，超限图按比例缩小。
- 高清 GL 纹理仅驻留 1 张，RGBA 上限 256 MiB；缩略图 LRU 最多 48 张，RGBA 上限 9 MiB。打开图鉴先释放场景纹理/FBO 缓存；离开释放全部图鉴纹理和后台结果。此预算不包含字体、驱动或解码器内部内存，不声称整个进程只用这些内存。
- 磁盘缓存为 `$XDG_CACHE_HOME/ef-native/gallery-v1`，否则 `$HOME/.cache/ef-native/gallery-v1`，最多 512 张 PNG（约 100 MiB 原始像素上限）。键包含版本、源路径、文件大小和修改时间；损坏缓存重新生成。磁盘不可写时保留内存缓存与正常浏览；不会写 content 或原版目录。

当前为功能性中文界面，尚未复刻原版图鉴按钮图集/坐标/动画。KTX2 缓存仍是后续独立任务；本轮用单图驻留与有界缩略图解决浏览显存，不更改资源分发契约。

## 验证

```bash
./build/dev/ef_native --content content --audit-gallery > work/gallery-catalog.json
ctest --preset dev -R 'gallery|title_flow|core'
SDL_VIDEO_DRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a python3 tests/title_flow_smoke.py \
  --binary build/dev/ef_native --gallery-only --real-content content --out work/gallery-real
python3 work/verify/verify.py
```

自动验收包含合成目录分类/差分不重不漏/旧缩略图对应/无缩略图组、PNG/WebP 等比缩小及 alpha、空存档分页/差分/4×→8×→4×/缓存/重复进入退出。真实安装目录统计与窗口检查结果见下文验收记录。

### 本机验收记录

- `--audit-gallery`：**459 组 / 2,306 张差分 / 21 张排除项**；与本地 `images.csv` 的 2,327 张 `cg` PNG 逐项对账，覆盖差集 0、越类 0、重复 ID 0。448 组有旧缩略图对应；其余 11 组（1 事件 / 10 成人补充）仍全部开放。
- 事件编号 00/01/02：17/200/186 组；结局 01/02：3/12 组；成人补充 01/02/03/04/05：17/15/3/1/5 组。
- dev **39/39 PASS**；新增分类与缩小解码、既有 core 的 ASan/UBSan **2/2 PASS**。无损 WebP 原尺寸路径保持 alpha/颜色逐字节精度；缩小使用独立路径。含超宽 PNG 坐标乘法溢出回归。
- 真实 content + Xvfb/Openbox：空存档、六页 72 组缩略图、LRU 驻留恰好封顶 48、目录首尾与差分、高清缩放、普通窗口→Borderless→普通窗口→最大化→普通窗口、重复进入/返回全部通过。合成用例另验证损坏 8× 源回退 4×、损坏磁盘缩略图重建、快速连续切换与鼠标/键盘入口。
- 发现 Debug 的第三方 WebP 解码热循环使超高 8× 首次加载约 24 秒；对 FetchContent 版解码/DSP/工具目标的 Debug 配置加 `-O2`（保留调试符号，主工程配置不变）后，同类图约 **2.3–2.4 秒**；首次整页 12 张缩略图约 **1.1–2.9 秒**，逐张后台出现，缓存命中约 **9 ms**，返回标题约 **35 ms**。这里为本机 Debug + 软件 GL 并发验收的等待观测，不是硬件 GPU 或所有文件的性能保证。
- 真实超高 8× `6400×9600` 请求以 `5461×8192` 上传，178,946,048 字节，低于 256 MiB 单图上限。进程 VmHWM 约 635 MiB（含软件 GL/解码器/字体），不将其误称为显存；硬件驱动实际峰值未测。
- M0 独立验证 **PASS，失败 0**：5,989 个源文件、原始归档摘要一致。未写原版目录，未生成预烘 CG 缩略图到 content。

原始报告、timings、截图：`work/gallery-catalog.json`、`work/gallery-real-fast/`、`work/gallery-all-final-tests.log`、`work/gallery-m0-verify.log`，均不入库。窗口状态测试需要 Openbox（可用 `--wm` 指定）；未安装时合成测试明确跳过窗口档位子项，仍检查目录闭环。

收尾回归：分类/图像/图鉴窗口/标题 **4/4**、电影返回标题 **1/1** 再次通过。增加应用 GL 帧缓冲截图及三处中文按钮像素检查，合成/真实包均通过；Xvfb 桌面 ImageGrab 曾遗漏部分字形，直接帧缓冲确认渲染完整，未修改共享字体实现。最终 M0 再跑 **PASS / 0 失败**（`work/gallery-m0-final-verify.log`）。最终可复核页面为 `work/gallery-real-fast/gallery-frame.png`，首次加载计时保留在 `gallery-cold-timings.json`。
