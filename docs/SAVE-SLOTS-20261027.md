# 原生存档槽位（2026-10-27）

已实现 100 个普通槽位、9 个轮换快速槽位，以及命令行选择、启动读档和只读列表。存档本体仍为 `ef.save` v1，保存时机和候选会话恢复规则见 [原生存档指南](NATIVE-SAVES-20261027.md)。本次完成槽位后端与现有 F2/F3 操作接入；原版存读档菜单、缩略图、自动存档及跨周目系统进度尚未实现。

## 使用

以下命令在工程根目录执行：

```bash
# 槽位模式：F2 按 q1→…→q9→q1 轮换；F3 读取最近一次快速保存
./build/dev/ef_native --content content --script scr/000_01mugai.sc \
  --save-dir work/my-saves

# 普通槽位 1：F2 覆盖该槽；F3 读取该槽，范围为 1..100
./build/dev/ef_native --content content --script scr/000_01mugai.sc \
  --save-dir work/my-saves --save-slot 1

# 重启恢复普通槽位；后续 F2/F3 继续使用该普通槽位
./build/dev/ef_native --content content --save-dir work/my-saves --load-slot 1

# 恢复指定历史快速槽或最新快速槽；后续 F2 继续轮换、F3 读取最新快速槽
./build/dev/ef_native --content content --save-dir work/my-saves --load-slot q9
./build/dev/ef_native --content content --save-dir work/my-saves --load-slot latest

# 无窗口、只读 JSON 列表；包括空槽位，共 109 行
./build/dev/ef_native --content content --save-dir work/my-saves --list-saves
```

省略 `--save-dir`、仅指定 `--save-slot`、`--load-slot` 或 `--list-saves` 时，默认目录为 `$XDG_DATA_HOME/ef-native/saves/slots`。XDG 路径未设置或为相对路径时，回退 `$HOME/.local/share/ef-native/saves/slots`。

未指定任何槽位参数的普通启动保持单文件模式，F2/F3 使用原来的 `quick.json`；`--save-file`、`--load-save` 也继续可用。`--save-file` 不与槽位参数混用；`--load-save` 与 `--load-slot` 互斥。已有原生单文件可以这样另存为普通槽位：

```bash
./build/dev/ef_native --content content --load-save work/my-progress.json \
  --save-dir work/my-saves --save-slot 1 --trace-messages 1
```

`--trace-messages N --save-slot N` 在最后一条指定对白的可保存时机写入。仅提供 `--save-dir` 或 `--load-slot` 的 trace 不会自动覆盖存档。恢复后 trace 的第一条包含当前对白，与单文件模式一致。

## 列表与验证范围

`--list-saves` 不创建尚不存在的目录，不写入索引，也不打开窗口/音频设备。每行包含槽位 ID、类别、编号及状态；已占用槽位还有单调递增的 `sequence`：

| 状态 | 含义 |
|---|---|
| `empty` | 索引中没有该槽位 |
| `compatible` | 元数据可读，剧本和消息布局绑定与当前包一致 |
| `incompatible` | 元数据可读，但剧本或布局绑定不同 |
| `unreadable` | 对应文件丢失、损坏、版本不支持，或元数据无效；附 `error` |

元数据包含时间、剧本、timing 变体、暂停类型、行锚点、显示姓名及 UTF-8 文本摘要（最多 192 字节）。列表不加载全部图片、音频或重建场景；`compatible` 不能代替完整读档。实际读档仍先在新会话中验证状态和媒体，成功后才替换当前进度。单个坏槽不妨碍其余槽位列出；索引本身损坏时整体报错，避免静默重建并覆盖已有记录。

## 存储与一致性

目录结构为 `catalog.json`（`ef.slots` v1）和 `objects/slot-XXXXXX.json`（原生存档对象）。索引只记录槽位→对象文件、递增序号及最近快速槽；不把媒体放进存档。

写入时先创建独立新对象并完成原子写入，再原子替换索引，最后删除被替换槽位的旧对象。目录共享/独占 `flock` 覆盖完整读取/写入，防止多个本机进程同时保存时丢失索引更新，或读者仍在读取时删除旧对象。最新快速槽和轮换只依赖序号，不依赖系统时钟或文件修改时间。达到序号上限会拒绝继续写入。

目录名/对象引用严格校验；拒绝索引及对象符号链接、路径逃逸、重复对象/序号和未知索引版本。未引用文件不自动清理，崩溃可能留下孤立对象，但不会据此猜测并重建索引。这里依赖本机 Linux 文件锁与原子重命名；没有网络文件系统或断电故障注入验收。

## 验证

```bash
cmake --build --preset dev
ctest --preset dev
cmake --build --preset asan
ctest --preset asan
python3 work/verify/verify.py
```

`save_checkpoint` 覆盖普通槽位边界、11 次快速保存轮换、相同时间戳排序、坏槽隔离、包不兼容、坏索引拒绝、失败写入保留旧档、8 个并发写入及孤立文件保留。`save_ui` 在 Xvfb 中验证 F2/F3 的 10 次快速保存、轮回、最新读档、普通槽位 100、单文件导入槽位、列表只读和参数冲突，并保留原有 NVL 像素与失败读档隔离回归。

本机验收：dev **33/33 PASS**、core ASan **23/23 PASS**；M0 独立验证 **PASS，失败 0**。运行日志与真实存档仅保留本地。
