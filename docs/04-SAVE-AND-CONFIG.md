# 04 — 存档/配置（移植兼容层）

完整规格：`port/docs/SAVE_FORMAT.md`（中文，含移植速答 §0.1、实现指南 §13、Windows 配置→settings 对照 §14；英文原稿 `SAVE_FORMAT.en.md`）。
解析工具：`analysis/save/save_dump.py`（.sav/.tbr/cgv.dat/sysparams.txt/system.dat/.png）。

## 要点
| 文件 | 格式 | 移植处理 |
|---|---|---|
| `ef%04d.sav` | `u32 0` + `"nnl_ft 1.00\0"` + 备注 + `u32 0` + **明文 zlib** → 文本 payload（`<begin variables>` … `<<begin backlog>>`） | zlib+文本读写即可；手册证实 100 存档槽（10 页×10）+ 9 个快速存档 + 自动存档；本目录 11 个（0000 自动，0010-0019 第1页） |
| `*.tbr` | `u32 0` + zlib → 8192B/65536bit，bit=行 id（MSB-first） | "跳过已读"；跨周目累积 |
| `ef00XX.png` | 标准 PNG 100×75 RGBA8 | 存档缩略图 |
| `cgv.dat` | `u32 0` + zlib → 2236 条 CG 图像名（字母序） | 图鉴解锁表 |
| `sysparams.txt` | CRLF+TAB 纯文本 51 键（GBK 字体名） | settings；键对照表见 §14（`messageSpeedTBR=7`、`messageSpeedRead=16`、`volumeBGM=17/SE=50/voice=50`、`autoSaveCycle=200`、`autoSaveBacklogLimit=100`、`quickSaveFileNumber=1`…） |
| `system.dat` | `u32 0` + 123B 高熵未知 | 原样保留/忽略（移植可自建） |

## 脚本定位
存档 `!script_Filename` / `!script_ID`(=文本行 L0) / `!script_Pointer`(原版编译指令下标)。
移植版重写解释器后 Pointer 不可复用 → 用 **script+行 id(L0)** 为主锚点，backlog 末条 L0 与 ID 对齐即可恢复；`L1..L4_*` 字段字典见 save 文档 samples/15。

## 编码/转义
存档与脚本都是 **GBK**；保存时按原始字节写回（GBK 尾字节可为 0x5C，勿做转义转换）；`\n/\a/\v/\w` 原样保留。

## r18 状态
原版只用注册表键 `SOFTWARE\minori\ef_first_en_r18patch`；存档无标志 → 移植版设置为 `settings.r18_patch_enabled`，决定是否优先加载 `r18_cn/*.paz` 并用 `censored` 变量门控。
