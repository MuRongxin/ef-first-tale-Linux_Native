# `.playVoice` 尾随参数兼容（2026-10-27）

## 结论

`scr/205_01.sc:615` 的 `.playVoice * * 0` 应立即停止当前语音。原版只读取第一个参数，尾随的 `* 0` 被忽略，并非 voice 的淡入/淡出参数。先前的异常来自移植版按“恰好一个参数”检查，以及离线 JSON 编译器的相同限制；现已修复。

## 原版观察与证据

只读目标：`/home/lrxin/Downloads/ef - the first tale/ef_first_AA.exe`，SHA-256：
`9824732313d13136cb2300f33038a655ba480ae01cefc2749028d1b1748b4f9e`。

使用 REA / Ghidra 12.1.4 静态分析，证据在本地 `work/re/voice-20261027/`。证据包已经导出，分析会话已经 `close_binary`；未执行或修改原 exe。以下地址属于该版本，伪代码不是原始源码。

| 观察 | 位置与本地证据编号 |
|---|---|
| `.playVoice` 字符串在 `0x5786b0`，引用点 `0x4cc657`；构造器 `0x4ea8a0` 写入 `CommandPlayVoice` 虚表 `0x576e44` | 002、005、006 |
| 虚表 `+4` 为 Parse `0x4ea6e0`，`+8` 为 Execute `0x4ea950`，`+0x6c` 为资源名 setter `0x42d380` | 009、011 |
| Parse 检查参数 vector 非空后，只把首个 string 交给 setter，没有访问第二、第三项，也没有参数上限检查 | 003、007 |
| 行解析器 `0x4ca510` 在 `0x4ca832..0x4ca87a` 将长度恰为 1 的 `*` 转为空字符串；比较常量 `0x574250` 为 `2a 00`，空串常量 `0x55fe1c` 为零 | 025、026、027、028 |
| Execute 首先调用 `0x4d7f00` 停止旧语音，然后只在语音未被禁用且名称长度 `this+0x54` 非零时调用 `0x4da8b0` 加载新语音 | 004、010、013 |
| 停止路径处理语音句柄 `context+0x1cc8`，调用 `0x4c5bf0`，并将句柄设为 `-1` | 008 |

因此，此处首项 `*` 经行解析后为空，Execute 停止原语音后不加载新语音；尾项不能改变这个结果。这是由静态控制流和字段读取推出的行为结论，未声称完成原版现场音频录制对拍。

原剧本上下文：615 行之前是宫子与京介的对白；616 行停止 BGM，617 行播放 `miyako_kakato.ogg`，620 行显示“咚！”，随后震屏。上下文与停止语音一致，但结论依据是上述二进制路径。

全量语料有 12 行 `.playVoice`；11 行只有 `*`，唯一带尾项的就是此行。

## 实现与内容更新

- 旧 SC 离线编译只保留 `.playVoice` 的第一个参数；尾项不解释为淡化参数，也不进入 JSON 运行时契约。
- 正式 JSON 仍为 `{"op":"voice","params":{"asset":null}}`，沿用 `ef.script` v1，无需改变 Schema。非空资源名对应播放语音。
- 场景宿主允许旧离线调用带尾项，并使用首项；生产 JSON 解码器继续拒绝未知字段。
- 缺少资源字段的 JSON、缺少参数的 SC 仍按现有严格契约拒绝；不扩展语料外的无参数用法。
- 对拍器按原版实际消费的首项比较 voice 参数，保留对其它命令的完整参数比较。

已迁移的 JSON 包可升级，不需要恢复 SC，也不读取原游戏：

```bash
cd "/home/lrxin/Ef_Linux/ef - the first tale"
cmake --build --preset dev --target ef_native ef_script_verify
python3 tools/migrate_scripts.py --upgrade-json --check-only
python3 tools/migrate_scripts.py --upgrade-json
./build/dev/ef_native --content content --audit-scripts
./build/dev/ef_native --content content --audit-resources
```

`--upgrade-json` 仅重试显式 unsupported 节点的已保留命令/参数，使用当前离线编译器重新映射。仍不能解释的命令继续保持 unsupported。已有合法节点、来源位置及语料溯源哈希保留；JSON blob 哈希、索引与资源目录同步更新。候选验证通过后原子发布，失败恢复旧 manifest；旧 blob 保留，可用同一工具的 `--restore` 恢复升级前入口。重复升级仅验证。

本机本次变更恰好 1 个节点。活动剧本索引：

```text
blobs/6defe260ad016a2f826b6c75110c7492a42716243e79fee0fec49e171dfeb458.scripts.json
```

升级备份：`work/script-migration/20261026T234221Z-3efpp1_d`。其中 `migration.json` 保留修改前的命令、参数、原文和原因，`manifest.before.json` 保留升级前入口。

## 验证

- dev 30/30、ASan 21/21 通过。新增回归覆盖：尾项不影响停止/播放、停止即时生效且其它通道继续播放、JSON `asset:null` 真正到达 mixer、缺参数拒绝；升级预检、验证失败不发布、正式发布、资源目录同步、重复运行、未知命令保持 unsupported 及恢复入口。
- 以旧 SC 备份作只读基准，对拍全部 94 个剧本：31,170 条物理指令、17,168 条对白、66,922 条 e/j 展开指令、20 组有界 VM 轨迹一致。显式 unsupported 为 0。比较工作包使用硬链接副本，SC 没有放回正式 `content/`。
- 正式 JSON 包 `scr/205_01.sc` 从 609 行开始，e/j 各连续 20 条消息通过，均跨过 615 行；测试不恢复更早的全局状态，不代表整章所有分支验收。
- 资源引用 14,955，缺失 0、歧义 0、不可用文件 0；正式包剩余 SC 文件 0。
- `python3 work/verify/verify.py` PASS，失败 0。

这消除了 JSON 迁移记录里的唯一显式异常，不代表未确认特效分支、存档、UI 或全部剧情演出已完成。
