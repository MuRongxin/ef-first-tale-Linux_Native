# 剧本导入、基础执行与文字校阅（2026-10-25）

> 本文保留 2026-10-25 阶段记录。2026-10-27 起，运行时只读 JSON 剧本，旧 SC 索引已停止支持；当前迁移步骤及验证结果见 [SC → JSON 移植指南](SC-TO-JSON-MIGRATION.md)。下文的兼容性与能力限制属于历史状态。

## 当前能力

已在原生框架上加入 `src/script/`。全量包以 manifest 的 `script_index` 为入口，兼容旧预览包的 `scripts/index.json`。
离线工具严格 GBK→UTF-8，拒绝错误字节、NUL 和不能按位往返的转码；保留 CRLF、空字段与原文。
运行时不需要原始游戏目录。

全量资源导入与 C++ 类型化引用检查已完成，见 `NATIVE-RUNTIME.md` 的 2026-10-25 记录。
`--audit-resources` 检查图片、扩展名为空的语音、BGM、SE、视频和脚本引用，覆盖 e/j 两种分支；
14,942 次 / 9,157 种引用，缺失 0，2 张 bg/st 同名立绘造成 19 次歧义，退出码为 2。
旧 `asset-audit.json` 的 3,150 项是文件扩展名正则口径，未覆盖全部消息语音，不与新类型化统计混用。

| 内容 | 状态 |
|---|---|
| `.sc` 词法/参数解析 | UTF-8、空语音/空名字、`#名字`、完整消息尾部空格、物理行号、L0、e/j 变体 |
| `#include` | 同命名空间查找、文本展开、循环/深度/指令数量限制 |
| `.message` / `.wait` | 消息暂停与推进、毫秒等待；声音/特殊文字时序仍待实现 |
| `.label` / `.goto` / `.if` | 标签索引、重复/缺失检查，整数比较与跳转 |
| `.set` / `.setGlobal` | 整数赋值、变量加减、溢出检查；当前仅内存状态 |
| `.select` | 选项暂停、数字键/鼠标选择、跳转 |
| `.chain` / `.end` | 跨脚本衔接/终止，保留 script + L0 + 物理源行定位 |
| 未实现命令 | 默认严格报错；只有显式文字校阅模式才记录并跳过 |

`.stage/.transition/.panel/.screen/.overlay/.effect`、音视频、原版 UI、存读档尚未接通。
不能把“100 条消息通过”解释为原版起始演出已复刻。

## 使用

```bash
python3 tools/prepare_content.py
python3 tools/import_scripts.py --game-root "/home/lrxin/Downloads/ef - the first tale"
cmake --build --preset dev

# 全量语法/引用审计：两个变体分别编译，保留未支持命令统计
./build/dev/ef_native --content content --audit-scripts

# 原剧本的文字校阅：明确不播放画面演出和音频
./build/dev/ef_native --content content --script scr/test.sc --text-preview

# 无窗口推进检查，遇到选项/结束而未达目标时失败；不会擅自选分支
./build/dev/ef_native --content content --script scr/test.sc \
    --text-preview --trace-messages 100
```

点击视口、空格、回车推进消息；数字键 1–9 或点击选项选择分支。
F10/F11/退出键继续可用。剧本会话暂不支持 R 热重载，需重启，以免覆盖当前执行状态。

不加 `--text-preview` 是严格模式，原版 `test.sc` 当前会在尚未实现的 `.panel` 处报错，
这是预期诊断。基础控制流可用仅含已支持命令的开发脚本验证。
校阅 UI 全部来自 `examples/script.scene.json`，是明确标注的开发页面，不是原版 UI 结果。

默认 `--variant e` 是**开发选择**，也可显式选 `--variant j`；没有断言原版汉化使用哪一套。
校阅模式仅将 `\n` 显示为换行，`\w/\v/\a` 不执行原版时序，保留原文并报告诊断。
`#名字` 的标记保存在模型中，仅在校阅页面去掉前导 `#` 展示。

局部变量跨 `.chain` 保留以支持语料中的跨文件路线计数；未定义变量初始为 0。
全局变量也仅保留在本次会话，存档/周目持久化待 M3。上述为当前实现规则，非新逆向证据。

## 数据与引用

导入工具只读 `scr.paz` 和可选 `r18_cn/scr.paz`，记录归档 SHA-256、每个源脚本 SHA-256、
UTF-8 SHA-256、编码与物理行数。内容按哈希放在 `content/blobs/`，最后原子更新脚本索引。
重新准备图片包不会删除脚本索引；所有脚本文本/字体副本/运行截图仍不入库。

脚本 ID 明确为 `scr/<name>.sc` 或 `r18_scr/<name>.sc`。引用优先当前命名空间；
r18 脚本找不到时允许回到基础 `scr`。基础脚本不会自动升级到 r18 覆盖；正式 r18 切换/覆盖映射待实现。
导入两套并不表示已经实现游戏的 r18 开关。

导入必须先完成编码转换再解释反斜杠：GBK 的 0x5C 可能是多字节尾字节。
测试专门覆盖这一情况。禁止使用带 `errors=replace` 的猜测转码作为运行时来源。

## 语料观察与未知

本次基准 PAZ 读取导入 75 + 19 = **94** 个脚本；物理行数 **31,182**
（scr 25,839 + r18_scr 5,343），非空解析项 **31,181**，消息 **17,168**。
这里统计的是当前基础与补丁两套文件，保留同名文件，不是覆盖后的单一路线。
旧文档“94 文件 / 25,912 行”没有与当前全量口径一致，后续引用以本次审计为准。

有 **73 条未识别行**，包括 `[j]:.wait` / `[j]:.stage` 这类带冒号的变体和制作备注。
参考 Python 解析器曾把部分异形行输出成 `..wait` / `..stage`；新解析器保留原始行为
`<unparsed>`，不擅自把它们当作正常命令或已确认注释。严格模式到达这些行时失败，
校阅模式记录诊断。具体原引擎行为仍待 REA/运行对照确认，见 `06-RE-ROADMAP.md`。

## 验收

- `ctest --preset dev`：核心、剧本、导入 3 组通过；导入组包含 5 项 Python 测试。
- 94 个剧本在 e/j 两套选择下均完成解析、包含展开、标签与 chain 引用检查；未知命令保留统计。
- 真实 `scr/test.sc`→`scr/000_01mugai.sc` 校阅模式连续 100 条消息通过，带源文件/L0/行号。
- `tests/script_window_smoke.py` 使用 Xvfb/Openbox/XTest，实际发送 Space 与 1 键，验证
  include、variant、等待、选项、条件跳转、chain 和最终中文帧缓冲。CI 用合成剧本，不带游戏数据。
- `work/verify/verify.py` 已重新执行，M0 独立验证 PASS。

```bash
# 与框架渲染测试同样的环境，另需 libXtst.so.6（Ubuntu: libxtst6）
xvfb-run -a -s '-screen 0 1600x1200x24' python3 tests/script_window_smoke.py
```

本机审计/追踪/画面放在忽略目录 `work/runtime/`，交互测试日志在 `build/script-window-smoke/`。
