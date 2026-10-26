# SC → JSON 移植指南

更新：2026-10-27。目标是把旧 SC 作为离线输入，运行时只读取工程自己的 UTF-8 JSON 剧本。原游戏目录始终只读。

## 1. 已完成的迁移

本机 `content/` 的 94 个剧本已发布为 JSON；目录内剩余 `.sc` 文件为 0。图像、字体、音频、视频不重新转码。原 SC、旧索引和旧 manifest 已移到工程内的备份目录：

```text
work/script-migration/20261026T232012Z-du07tb4q/
```

`migration.json` 记录备份文件的 SHA-256、候选索引和发布状态；`verification.json` 是迁移前语义对拍报告，`post-publish-audit.log` 是发布后 JSON 审计结果。备份、内容资源和验证产物不提交 Git。

初次迁移的索引（下方说明后续升级）：

```text
blobs/762faf759647cc80cc84447ddcc734e62e0020e8e6a863263e6e63fe51faf8fe.scripts.json
```

2026-10-27 已进一步修复 voice 尾项兼容；当前活动索引与备份见 [语音参数兼容](PLAYVOICE-20261027.md)。`content/manifest.json` 的 `script_index` 选择活动索引，资源目录中的 script 路径同步指向 JSON。生产程序不链接 SC 词法解析器；旧解析器仅在离线验证库及需要它的测试中使用。

## 2. 对其他旧内容包执行迁移

先构建当前版本的程序和离线验证器：

```bash
cd "/home/lrxin/Ef_Linux/ef - the first tale"
cmake --preset dev
cmake --build --preset dev --target ef_native ef_script_verify

# 生成候选 JSON 并对拍，活动 manifest 保持原值
python3 tools/migrate_scripts.py --check-only

# 对拍通过后发布、复查，并移出旧 SC 文件
python3 tools/migrate_scripts.py

# 已经是 JSON 时只复查，可重复运行
python3 tools/migrate_scripts.py
```

默认处理工程的 `content/`。其他包使用 `--content "工程内子目录"`；替代验证器用 `--verifier "可执行文件路径"`。已有包的迁移不读取原游戏目录，也不需要 PAZ 或 exe。`--check-only` 会留下候选哈希文件及验证记录，因此需要少量额外磁盘空间。

工具持有 `.import.lock`，先检查旧 UTF-8 源哈希，再生成候选文件，对拍全部剧本后备份旧文件，原子替换 manifest。发布后审计失败会恢复旧 manifest；旧 SC 清理发生在审计成功之后。若进程在清理阶段中断，应检查备份报告并执行恢复，再重新迁移。

新导入的数据直接输出 JSON，不再经过运行时 SC 阶段：

```bash
GAME="/home/lrxin/Downloads/ef - the first tale"
python3 tools/prepare_content.py --all --game-root "$GAME" --jobs 4
```

全量导入还会准备图片、音视频等资源，耗时和磁盘占用明显高于单独迁移。只有离线原始导入才需要用户自己的原游戏数据；部署后的运行时只需要 `content/`。

## 3. JSON 契约

| 层级 | 版本与用途 |
|---|---|
| 内容 manifest | 保持既有版本；`script_index` 选择活动索引 |
| 剧本索引 | `version: 2`、`format: "ef.script.v1"`、`encoding: "utf-8"` |
| 单剧本 | `format: "ef.script"`、`version: 1`、`id`、`instructions` |
| 指令 | `op`、命名参数 `params`、递增的 `source_line`，可选 `variant` / `colon_variant` |

格式定义见 [JSON Schema](../schemas/script-v1.schema.json)，命令与参数映射见 [共享指令表](../schemas/script-ops.json)，完整合成示例见 [script-v1.example.json](../examples/script-v1.example.json)。共享表在构建时嵌入程序，运行包不需要携带开发用 schema。

保留 `scr/000_01mugai.sc` 等逻辑 ID，是为了稳定跨剧本引用、L0 对白锚点和现有启动命令。这个字符串不代表磁盘上存在 SC 文件。物理路径由索引映射到 `blobs/<sha256>.script.json`。

参数使用 JSON 整数、布尔值、字符串、对象及数组；清空资源用 `null`。旧 BGM/SE 淡化参数中的 `*` 显式编译为原执行语义中的整数 2；省略的可选参数仍按对应命令默认值处理。`.stage` 使用具名的背景裁切、角色 `center_x` 和 `bottom_trim`。内部执行接口继续复用既有场景命令，不再对外暴露 SC 行文本协议。

对白示例：

```json
{
  "source_line": 10,
  "op": "dialogue",
  "params": {
    "id": 100,
    "voice": "",
    "speaker": "旁白",
    "events": [
      {"type": "text", "value": "第一行\n第二行"},
      {"type": "pause", "ticks": 10},
      {"type": "wait_voice"},
      {"type": "auto_advance"}
    ]
  }
}
```

`ticks` 为百分之一秒，10 即 100ms。JSON 字符串的 `\n` 是标准 JSON 换行转义；文字事件里的字面反斜杠不会再被解释成旧 SC 控制码。旧 `\w`、`\v`、`\a` 在离线导入时分别编译为 pause、wait_voice、auto_advance。GBK 先严格转为 UTF-8，再处理旧转义，避免把 GBK 尾字节 `0x5C` 当作反斜杠。

e/j timing 分支以 `variant: "e"` / `"j"` 保存，原冒号变体以 `colon_variant` 保存。`source_line` 是原物理行号，L0 是对白 ID；编辑时应保持已发布锚点稳定。运行时拒绝重复键、未知字段、未知 op、浮点数代替整数、错误类型、NUL 和超预算输入。Schema 描述数据形状，运行时还会检查标签、引用、范围与执行语义；不能仅以编辑器通过 Schema 作为验收。

## 4. 验证与已知边界

迁移前，对全部剧本逐指令比较命令、参数、来源位置、对白事件，再分别展开 e/j、比较 include 展开和标签。结果：

| 项目 | 结果 |
|---|---:|
| 剧本 | 94 |
| 物理指令 | 31,170 |
| 对白 | 17,168 |
| e/j 展开指令总数 | 66,922 |
| 有界 VM 对拍 | 20 组 |
| 资源引用 | 14,955 |
| 缺失 / 歧义 | 0 / 0 |
| 保留的未支持节点 | 1 |

VM 对拍使用 5 个入口、两种变体、首/末选项策略，每组上限 250 条消息及 20,000 步。它比较控制流、对白状态及记录的宿主命令，不解码音视频，也不能代替全部剧情分支和画面保真验收。

初次迁移时保留的唯一异常 `scr/205_01.sc:615` 的 `.playVoice * * 0` 已于 2026-10-27 通过原 exe 取证解决：原解析器只消费首项，尾随参数忽略，此处立即停止语音。当前包显式 unsupported 为 **0**；上表的 1 是初次迁移报告的历史值。完整证据与回归见 [语音参数兼容](PLAYVOICE-20261027.md)。

已有初次 JSON 包使用 `python3 tools/migrate_scripts.py --upgrade-json --check-only` 预检，再执行 `python3 tools/migrate_scripts.py --upgrade-json` 发布即可，无需恢复 SC。工具保留升级前 manifest 和旧 blob，并报告每个升级节点；当前编译器仍不认识的节点继续保留。`--audit-scripts` 的 `preserved_unsupported` 列出显式异常；原有 `unsupported` 统计表示核心控制流外的宿主命令，不等同于尚未实现的功能数。

可自行复查：

```bash
python3 tools/generate_script_schema.py --check
ctest --preset dev
ctest --preset asan
./build/dev/ef_native --content content --audit-scripts
./build/dev/ef_native --content content --audit-resources
./build/dev/ef_native --content content --script scr/000_01mugai.sc --variant e --trace-messages 428
./build/dev/ef_native --content content --script scr/000_01mugai.sc --variant j --trace-messages 428
python3 work/verify/verify.py
```

本次 dev 30/30、ASan 21/21 测试通过；独立资源验证 PASS，0 失败。新增测试覆盖类型错误、文字事件与字面转义、JSON-only 内容包、旧索引拒绝、迁移失败不发布、重复执行和备份恢复。迁移后 e/j 各连续 1,694 条消息的无头运行通过；消息窗的合成素材渲染检查通过。真实键盘联动 smoke 需要 Openbox；当前环境未安装，未计入通过项。

## 5. 恢复旧包

```bash
python3 tools/migrate_scripts.py --restore \
  work/script-migration/20261026T232012Z-du07tb4q
```

工具先验证所有备份哈希，再恢复 SC、旧索引及旧 manifest。恢复后需要迁移前的引擎版本（例如 `3470b76`）；当前生产程序有意拒绝旧索引。新 JSON 哈希文件会保留为未引用文件，不影响旧包。恢复流程已经在合成测试包中验证；本机正式内容保持 JSON 发布状态。

## 6. 后续修改剧本或扩展指令

JSON 是新的运行时契约。`tools/script_json.py` 只负责离线编译；不要把它或旧解析器接回生产运行路径。通用字段先更新 `schemas/script-ops.json`；复杂操作同步修改 Python 编译器与 C++ JSON 解码器，随后运行 `tools/generate_script_schema.py` 重新生成 Schema，并重新配置、构建、测试。未确认语义继续留显式 unsupported 节点，确认后再补映射及回归用例。

`blobs/` 是内容寻址产物，不能直接修改而保留旧哈希文件名。当前没有独立的可视化剧本编辑器或通用 JSON 发布工具；自有 JSON 编辑流程需要重新序列化、计算文件 SHA-256、写入新 blob、更新索引路径与 `json_sha256`，同步资源目录中的 script 路径，并以新 manifest 发布。先在包副本中验证全部脚本和资源，再更新活动入口。来源哈希保留作溯源，不应伪装成修改后文本的哈希。

若要重新从旧 SC 编译，使用备份副本或离线原始导入，生成新包并验证；无需恢复正式运行包。当前不提供原版存档的一次性迁移，JSON 更换也不代表存档系统或全部演出分支已经完成。
