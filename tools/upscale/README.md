# 高清化工具

这些离线工具读取本地生成的素材清单，输出 4×/8× 高清素材或检查报告。
原始数据和产物不包含在仓库中；先按根目录 [README](../../README.md) 生成图像清单。

- `remote/ef_client.py`：对接兼容的远端高清化服务，接收 4×/8× WebP。
- `remote/fix_large.py`：超大图片的分块处理；具体选项见 `--help`。
- `run_realesrgan.sh`：本机 ncnn 4× 备选，默认 dry-run，执行时显式加 `--execute`。
- `alpha_pipeline.py`：带透明通道的图像处理。
- `compare_montage.py`：生成画质对照图。
- `make_manifest.py`：统计本地素材并生成任务清单。

```bash
python3 tools/upscale/remote/ef_client.py --help
python3 tools/upscale/remote/ef_client.py --dry-run
python3 tools/upscale/remote/fix_large.py --help
```

本机 ncnn 工具需自行安装，通过 `REALESRGAN_BIN`、`REALESRGAN_MODELS` 等环境变量
指定路径；服务端地址、GPU 和模型按自己的环境配置。
