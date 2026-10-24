#!/usr/bin/env bash
# =============================================================================
# run_realesrgan.sh — ef 高清化：Real-ESRGAN(ncnn Vulkan) 批量超分驱动（骨架）
#
# 设计目标：
#   * 默认 --dry-run：只打印计划、规模、预计时间/体积，不碰 GPU、不写输出
#   * 真正执行必须显式 --execute
#   * 按类别分批（cg/tachie/bg/scroll_bg/ui/other），用硬链接暂存目录喂给
#     realesrgan-ncnn-vulkan 的目录模式，避免每张图重复加载模型（约省 1.5-2 s/张）
#   * 幂等：--skip-existing 跳过已产出文件；--limit 便于先抽样 20 张验收
#
# 前置（本机已具备，无需安装）：
#   /home/lrxin/文档/realesrgan-ncnn-vulkan/realesrgan-ncnn-vulkan  (ELF x86-64, 11 MB)
#   /home/lrxin/文档/realesrgan-ncnn-vulkan/models/*.{param,bin}     (45 MB)
# 若换机器：下载 https://github.com/xinntao/Real-ESRGAN/releases
#   realesrgan-ncnn-vulkan-20220424-ubuntu.zip ≈ 72 MB 解压即用
#
# 用法示例：
#   tools/upscale/run_realesrgan.sh                          # 默认 dry-run，打印 4x 全量计划
#   tools/upscale/run_realesrgan.sh --scale 4 --execute      # 真跑 4x 全量（本机备选）
#   tools/upscale/run_realesrgan.sh --class cg --limit 20 --execute   # 抽样 20 张验收
#   tools/upscale/run_realesrgan.sh --class tachie --alpha-mode split --execute
#   tools/upscale/run_realesrgan.sh --class thumb --execute   # 不推荐：缩略图应降采样生成
#
# 注意：本脚本只写 --out 目录与 work/upscaler/{logs,staging}；绝不修改 assets/raw。
# =============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST_PY="$ROOT/tools/upscale/make_manifest.py"
DATA_DIR="${DATA_DIR:-$ROOT/assets/raw}"
WORK_DIR="${WORK_DIR:-$ROOT/work/upscaler}"
OUT_DIR="${OUT_DIR:-$WORK_DIR/out}"
STAGING_DIR="${STAGING_DIR:-$WORK_DIR/staging}"
LOG_DIR="$WORK_DIR/logs"

# ---- 本地已有工具（可用环境变量覆盖；不自动安装/下载）-------------------------
RR_BIN="${REALESRGAN_BIN:-/home/lrxin/文档/realesrgan-ncnn-vulkan/realesrgan-ncnn-vulkan}"
RR_MODELS="${REALESRGAN_MODELS:-/home/lrxin/文档/realesrgan-ncnn-vulkan/models}"

# ---- 默认参数 ---------------------------------------------------------------
SCALE=4
MODEL="realesr-animevideov3"
CLASSES=(cg tachie bg scroll_bg)
GPU=1                 # 0=Intel HD530, 1=GTX 960M, 2=llvmpipe(CPU, 极慢)
TILE=0                # 0=自动；4GB 显存下 800x600 无需切块
JOBS="1:2:2"
LIMIT=0
CHUNK=0               # 0=每类一次目录批处理
FORMAT="png"
ALPHA_MODE="passthrough"   # passthrough | split(调用 alpha_pipeline.py)
SKIP_EXISTING=0
DRY_RUN=1
LOG_FILE=""

usage() { sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

need_val() { [[ $# -ge 2 && -n "$2" ]] || { echo "ERROR: $1 需要参数值" >&2; exit 2; }; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage ;;
    --execute) DRY_RUN=0 ;;
    --dry-run) DRY_RUN=1 ;;
    --scale) need_val "$1" "${2:-}"; SCALE="$2"; shift 2 ;;
    --model) need_val "$1" "${2:-}"; MODEL="$2"; shift 2 ;;
    --class) need_val "$1" "${2:-}"; IFS=',' read -r -a CLASSES <<< "$2"; shift 2 ;;
    --gpu) need_val "$1" "${2:-}"; GPU="$2"; shift 2 ;;
    --tile) need_val "$1" "${2:-}"; TILE="$2"; shift 2 ;;
    --jobs) need_val "$1" "${2:-}"; JOBS="$2"; shift 2 ;;
    --limit) need_val "$1" "${2:-}"; LIMIT="$2"; shift 2 ;;
    --chunk) need_val "$1" "${2:-}"; CHUNK="$2"; shift 2 ;;
    --format) need_val "$1" "${2:-}"; FORMAT="$2"; shift 2 ;;
    --alpha-mode) need_val "$1" "${2:-}"; ALPHA_MODE="$2"; shift 2 ;;
    --out) need_val "$1" "${2:-}"; OUT_DIR="$2"; shift 2 ;;
    --skip-existing) SKIP_EXISTING=1; shift ;;
    *) echo "ERROR: 未知参数 $1（-h 查看用法）" >&2; exit 2 ;;
  esac
done

case "$SCALE" in 2|3|4) ;; *) echo "ERROR: --scale 只能是 2/3/4" >&2; exit 2 ;; esac
case "$ALPHA_MODE" in passthrough|split) ;; *) echo "ERROR: --alpha-mode 只能是 passthrough/split" >&2; exit 2 ;; esac
case "$MODEL" in
  realesr-animevideov3) ;;
  realesrgan-x4plus-anime|realesrgan-x4plus)
    [[ "$SCALE" == 4 ]] || { echo "ERROR: $MODEL 只支持 4x，请用 --scale 4" >&2; exit 2; } ;;
  *) echo "ERROR: 未知 --model $MODEL" >&2; exit 2 ;;
esac
GPU_OK=1
[[ -x "$RR_BIN" ]] || { echo "WARN: 找不到可执行 $RR_BIN（dry-run 仍可打印计划）" >&2; GPU_OK=0; }
[[ -d "$RR_MODELS" ]] || { echo "WARN: 找不到模型目录 $RR_MODELS" >&2; GPU_OK=0; }

mkdir -p "$LOG_DIR" "$OUT_DIR"
LOG_FILE="$LOG_DIR/run_$(date +%Y%m%d_%H%M%S)_${SCALE}x.log"

# ---- 生成/复用类别清单 ------------------------------------------------------
WORKLISTS="$WORK_DIR/worklists"
if [[ ! -d "$WORKLISTS" ]] || [[ -z "$(ls -A "$WORKLISTS" 2>/dev/null)" ]]; then
  echo "[*] 生成类别清单 -> $WORKLISTS"
  python3 "$MANIFEST_PY" --emit-worklists "$WORKLISTS" >/dev/null || exit 1
fi

# ---- 组装待处理文件 ---------------------------------------------------------
LIST_FILE="$(mktemp /tmp/ef_upscale_list.XXXXXX)"
trap 'rm -f "$LIST_FILE"' EXIT
TOTAL_N=0
for cls in "${CLASSES[@]}"; do
  wl="$WORKLISTS/$cls.txt"
  [[ -f "$wl" ]] || { echo "WARN: 类别 $cls 无清单（跳过）" >&2; continue; }
  while IFS= read -r rel; do
    [[ -n "$rel" ]] || continue
    src="$DATA_DIR/$rel"
    [[ -f "$src" ]] || { echo "WARN: 源文件缺失 $src" >&2; continue; }
    echo -e "$cls\t$rel" >> "$LIST_FILE"
    TOTAL_N=$((TOTAL_N+1))
    if [[ "$LIMIT" -gt 0 && "$TOTAL_N" -ge "$LIMIT" ]]; then break 2; fi
  done < "$wl"
done
[[ "$TOTAL_N" -gt 0 ]] || { echo "ERROR: 待处理文件为 0（检查 --class 与清单）" >&2; exit 1; }

# ---- 估算（基于 make_manifest.py 的实测系数）--------------------------------
read -r EST_BYTES EST_SEC < <(python3 - "$LIST_FILE" "$SCALE" "$MODEL" "$ROOT" <<'PY'
import sys, os
list_file, scale, model, root = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
sec = {2: 3.7, 3: 3.9, 4: 4.6} if model == 'realesr-animevideov3' else {4: 13.5}
bpp = {'cg':1.30,'tachie':1.08,'bg':1.00,'scroll_bg':0.55,'ui':1.60,'thumb':1.95,
       'mask_procedural':0.10,'other':1.10}
import json
# 用 manifest 快速查尺寸
man = os.path.join(root,'assets','manifest')
idx = {}
for tag in ['bg','st','sys','r18_bg','r18_st','r18_sys']:
    p = os.path.join(man, f'entries_{tag}.jsonl')
    if not os.path.exists(p): continue
    for line in open(p):
        r = json.loads(line)
        if r.get('kind')=='png' and r.get('width'):
            idx[r['rel_path'].replace('\\','/')] = r
tot_b = 0.0; tot_s = 0.0
for line in open(list_file):
    cls, rel = line.rstrip('\n').split('\t', 1)
    r = idx.get(rel)
    if not r: continue
    px = r['width']*r['height']
    if cls in ('thumb','mask_procedural'): continue
    tot_s += px/1e6*sec.get(scale, 4.6)
    tot_b += px*scale*scale*bpp.get(cls, 1.1)
print(int(tot_b), int(tot_s))
PY
)
[[ -z "${EST_BYTES:-}" ]] && EST_BYTES=0
[[ -z "${EST_SEC:-}" ]] && EST_SEC=0

echo "=============================================================="
echo " ef 高清化 Real-ESRGAN 批量计划"
echo "   源目录   : $DATA_DIR"
echo "   输出目录 : $OUT_DIR/${SCALE}x/<class>/"
echo "   模型     : $MODEL  倍率: ${SCALE}x  类别: ${CLASSES[*]}"
echo "   GPU      : -g $GPU (0=Intel HD530 / 1=GTX960M / 2=llvmpipe)  tile=$TILE jobs=$JOBS"
echo "   待处理   : $TOTAL_N 张"
echo "   预计输出 : ~$((EST_BYTES/1024/1024)) MiB"
echo "   预计耗时 : ~$((EST_SEC/60)) min (~$((EST_SEC/3600)) h)"
echo "   日志     : $LOG_FILE"
echo "=============================================================="

if [[ "$DRY_RUN" == 1 ]]; then
  echo ""
  echo "[DRY-RUN] 以下为将要执行的命令（每批一次目录调用）："
  echo "  for cls in ${CLASSES[*]}; do"
  echo "    ln 硬链接 $DATA_DIR/<rel> -> $STAGING_DIR/\$cls/  (或直接软链)"
  echo "    \"$RR_BIN\" -i $STAGING_DIR/\$cls -o $OUT_DIR/${SCALE}x/\$cls \\"
  echo "        -n $MODEL -s $SCALE -m $RR_MODELS -g $GPU -t $TILE -j $JOBS -f $FORMAT"
  echo "  done"
  echo ""
  echo "[DRY-RUN] 未执行任何 GPU 任务。确认后加 --execute 真跑。"
  [[ "$GPU_OK" == 1 ]] && echo "[DRY-RUN] 本机工具链检查：OK（$RR_BIN）" || echo "[DRY-RUN] 工具缺失，参考 docs/UPSCALE-TOOLCHAIN.md 安装章节"
  exit 0
fi

[[ "$GPU_OK" == 1 ]] || { echo "ERROR: 工具缺失，拒绝执行。见 docs/UPSCALE-TOOLCHAIN.md" >&2; exit 3; }

# ---- 真正执行 ---------------------------------------------------------------
exec 3>&1 1> >(tee -a "$LOG_FILE") 2>&1
echo "[*] $(date '+%F %T') 开始，共 $TOTAL_N 张"
FAIL=0; DONE=0
for cls in "${CLASSES[@]}"; do
  sub="$LIST_FILE.class.$cls"
  awk -F'\t' -v c="$cls" '$1==c{print $2}' "$LIST_FILE" > "$sub"
  n=$(wc -l < "$sub"); [[ "$n" -gt 0 ]] || { rm -f "$sub"; continue; }
  stage="$STAGING_DIR/${SCALE}x/$cls"; out="$OUT_DIR/${SCALE}x/$cls"
  mkdir -p "$stage" "$out"
  # 分批：CHUNK=0 表示整类一次喂给工具；否则每 CHUNK 张一组（便于断点/观察）
  batch_size=$n; [[ "$CHUNK" -gt 0 ]] && batch_size=$CHUNK
  idx=0
  while [[ "$idx" -lt "$n" ]]; do
    mkdir -p "$stage"; rm -f "$stage"/*.png "$stage"/*.webp 2>/dev/null
    fb=0
    while IFS= read -r rel; do
      base="$(basename "$rel")"
      dst="$out/${base%.*}.png"
      if [[ "$SKIP_EXISTING" == 1 && -f "$dst" ]]; then continue; fi
      ln -f "$DATA_DIR/$rel" "$stage/$base" 2>/dev/null || cp -f "$DATA_DIR/$rel" "$stage/$base"
      fb=$((fb+1))
      idx=$((idx+1))
      [[ "$fb" -ge "$batch_size" ]] && break
    done < <(tail -n +"$((idx+1))" "$sub")
    [[ "$fb" -gt 0 ]] || break
    echo "[*] $cls: 第 $((idx-fb+1))..$idx / $n 张 -> $out"
    if [[ "$ALPHA_MODE" == "split" ]]; then
      python3 "$ROOT/tools/upscale/alpha_pipeline.py" \
        --input-dir "$stage" --output-dir "$out" --scale "$SCALE" \
        --bin "$RR_BIN" --models "$RR_MODELS" --gpu "$GPU" --tile "$TILE" \
        --jobs "$JOBS" --format "$FORMAT" --execute || { echo "WARN: alpha 批次失败" >&2; FAIL=$((FAIL+1)); }
    else
      "$RR_BIN" -i "$stage" -o "$out" -n "$MODEL" -s "$SCALE" -m "$RR_MODELS" \
        -g "$GPU" -t "$TILE" -j "$JOBS" -f "$FORMAT" -v 2>&1 | grep -vE '^[0-9.]+%$' || true
    fi
    DONE=$((DONE+fb))
  done
  rm -f "$sub"
done
echo "[*] $(date '+%F %T') 结束：处理 $DONE 张，失败批次 $FAIL"
echo "[*] 输出目录： $OUT_DIR/${SCALE}x"
echo "[*] 下一步：抽查 20 张 -> tools/upscale/compare_montage.py；全量验收见 docs/UPSCALE-TOOLCHAIN.md"
exit 0
