# 04 — 存档/配置（原版参考与原生存档）

> 2026-10-27：移植版已实现独立 UTF-8 JSON 存档 v1（F2/F3、CLI 恢复），见 [原生存档指南](NATIVE-SAVES-20261027.md)。下面的 GBK/zlib 描述是原版格式研究，用于未来离线迁移，不是移植版的运行时读写协议。

完整规格：`SAVE_FORMAT.md`（中文，含移植速答 §0.1、实现指南 §13、Windows 配置→settings 对照 §14；英文原稿 `SAVE_FORMAT.en.md`）。
解析工具：`tools/save_dump.py`（.sav/.tbr/cgv.dat/sysparams.txt/system.dat/.png）。

## 要点
| 文件 | 格式 | 移植处理 |
|---|---|---|
| `ef%04d.sav` | `u32 0` + `"nnl_ft 1.00\0"` + 备注 + `u32 0` + **明文 zlib** → 文本 payload（`<begin variables>` … `<<begin backlog>>`） | 原生运行时使用独立 JSON；旧格式留作离线迁移参考。手册证实 100 存档槽（10 页×10）+ 9 个快速存档 + 自动存档；本目录 11 个（0000 自动，0010-0019 第1页） |
| `*.tbr` | `u32 0` + zlib → 8192B/65536bit，bit=行 id（MSB-first） | "跳过已读"；跨周目累积 |
| `ef00XX.png` | 标准 PNG 100×75 RGBA8 | 存档缩略图 |
| `cgv.dat` | `u32 0` + zlib → 2236 条 CG 图像名（字母序） | 图鉴解锁表 |
| `sysparams.txt` | CRLF+TAB 纯文本 51 键（GBK 字体名） | settings；键对照表见 §14（`messageSpeedTBR=7`、`messageSpeedRead=16`、`volumeBGM=17/SE=50/voice=50`、`autoSaveCycle=200`、`autoSaveBacklogLimit=100`、`quickSaveFileNumber=1`…） |
| `system.dat` | `u32 0` + 123B 高熵未知 | 原样保留/忽略（移植可自建） |

## 脚本定位
存档 `!script_Filename` / `!script_ID`(=文本行 L0) / `!script_Pointer`(原版编译指令下标)。
移植版重写解释器后 Pointer 不可复用 → 用 **script+行 id(L0)** 为主锚点，还须恢复变量、场景、声音和消息页，不能只把游标移到 backlog 末条；`L1..L4_*` 字段字典见 save 文档 samples/15。

## 编码/转义
原版存档与剧本是 **GBK**；离线旧档处理需保留原始字节，移植版剧本和存档均用 UTF-8 JSON，不向原 `.sav` 写回。处理原格式时，GBK 尾字节可为 0x5C，不应先按反斜杠转义处理；`\n/\a/\v/\w` 原样保留。

## r18 状态
历史记录中的注册表线索为 `SOFTWARE\minori\ef_first_en_r18patch`；补丁实际触发条件应以独立取证为准。移植版计划设置 `settings.r18_patch_enabled`，未来决定导入内容中的补丁优先策略，并结合 `censored` 变量门控；运行时不读取 PAZ。当前存档保留实际程序命名空间及变量，不自行判断原版补丁触发规则。
