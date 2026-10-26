# shakeScreen 未确认分支续进（2026-10-26）

## 已实现：显式模式的负时长

`H/V/Z/O` 的模式分派不看时长符号，倒计时统一取绝对值。因此 `.shakeScreen H 100 -100` 仍是阻塞式水平震屏，持续100个10ms tick，不是异步模式。
`Shake` 已支持这些形式，保持原有30ms采样、幅度、固定种子选择与画布冻结规则。先把duration转int64再取绝对值，避免INT_MIN的有符号溢出；移植仍限制绝对时长<=1,000,000 tick。
原版观察来自0x4d9bf0的模式switch与倒计时赋值；主循环0x4e11a0的5/6/7/9分支与普通正时长共用。

测试包括四模式正/负时长逐样本等价、到期边界、INT_MIN/超限拒绝、3/6次连续负时长震屏与输入/等待/消息顺序。使用独立build/shake-branches（ASan）构建，避免覆盖用户movie构建。

## 默认模式负时长：已区分，移植尚未接入

0x4d9bf0只有默认分支依据时长符号选状态：正值→8，负值/0→13（0倒计时立即到期）。R/数字/其它未识别首字符都走默认分支。
负时长默认模式不是状态8的冻结全画面震屏。0x4e11a0状态13调用0x501ce0后继续正常脚本分派，绘制源是活动场景对象，计时仍用abs(duration)。因此不能只在当前Shake模型中去掉负值检查。
状态13取rand()%9；汇编实际准备10组坐标，但只用前9组：
`(0,0),(-a,-a),(0,-a),(a,-a),(-a,0),(0,0),(a,0),(-a,a),(0,a)`。
第十组(a,a)未被该取模使用；不要擅自改成均匀3×3随机。
已追踪0x501ce0：源是场景对象内嵌位图`+0x98`，目标是`*(this+0x3f8)`；把传入base x/y与随机x/y相加作为目的偏移，露出区域填黑，裁切后逐行拷贝。它与0x501140的普通滚动取源裁切算法不同。
这确认了它不是对含消息窗的最终画布做震屏。源缓存与静态立绘/动画缓存的合成边界，以及它与后续stage/scroll的交互，还需逐项核对；不直接把它映射成当前Layer.background位移。默认负时长形式继续严格拒绝。

## T 模式：几何已定位，停止流程仍待确认

- 模式首字符T/t→状态10，与时长符号无关；倒计时abs(duration)。
- 0x4e2c61..0x4e2ccb：角度来自`uint32(elapsed_ticks*1000)/800.0`，超过amplitude后钳制到amplitude并锁定后续几何更新。普通范围内相当于每10ms tick增长1.25度，即125度/秒。
- `0x585938=800.0`、`0x5857b0=180.0`、`0x5737d0=pi`，以同一SHA256原exe的PE节映射只读字节补查，记录于work/re/shake-branches-20261026/pe-constants.json，REA记录005的read_bytes已交叉确认800.0一致。
- x87链fmod(angle,180)/180*pi；旋转锚点(799,0)，调用0x523330→0x51f110。没有O模式的历史帧平均步骤，不能直接复用现有中心旋转叠影。
- 倒计时到期/跳过转到0x4e2940，该路径清输入并复制原图，但在局部路径未看到清除+0x1cdc。正常几何路径也提前return，达到角度上限后仍继续return。是否有外部清理、该未用分支是否原版本身未完成，尚不能仅凭这些局部静态路径下结论。

T暂不实现；不得把普通中心旋转近似后标为完整支持，也不应凭静态疑点宣称已经观察到原版死锁。后续需核查外部状态清理并做受控原版运行验证。

## 协作边界

本轮只修改src/scene/shake.cpp和tests/shake_tests.cpp，以及震屏专题。用户正在接入movie的Presentation/Renderer/音频/应用/CMake文件保持原样，不混入本提交。
原版目录只读。docs可提交，AGENTS.md与work/证据/缓存/日志不提交。REA独立目录work/re/shake-branches-20261026、短socket work/re/shtmp；高负载首次启动330s超时后仅对该会话提高startup timeout至720s，不更改客户端配置。

## 验证结果

独立ASan构建`build/shake-branches`，核心18/18 PASS；震屏专项及3/6次连续负时长阻塞均通过。未修改数据层/解包/导入工具或资源清单。

## 已有REA证据

| 记录 | 请求 | Evidence ID |
|---|---|---|
| 114-procedure_pseudo_code.evidence.json | `{'procedure': '0x4e11a0'}` | `ev_032034a50b4ce83aaa0a005eb9e0f6e6581073ebcca0f81a7e9d299ce8566802` |
| 251-procedure_pseudo_code.evidence.json | `{'procedure': '0x4d9bf0'}` | `ev_ddb0e081ac579c8582d94b59ae301146663329b1f154edc77087a7bd0e152e3e` |
| 265-procedure_assembly.evidence.json | `{'procedure': '0x4d9bf0'}` | `ev_0a6852f1d9d1a6fd1a1cba224d21ba5a101b1e4821336802cc8425199dcfe9b9` |
| 267-procedure_assembly.evidence.json | `{'procedure': '0x4e11a0'}` | `ev_0ed7c086a0882fa5de918fa0087594d1269b6c209a7e90caace762a87ef0fa81` |
| 262-procedure_pseudo_code.evidence.json | `{'procedure': '0x523330'}` | `ev_85824a136d83fa056fade0b4e60e656ace10943eb71b2248acbd9596fa1f4823` |
| shake-branches/004-procedure_pseudo_code.evidence.json | `{'procedure': '0x501ce0'}` | `ev_1d78e0ee2445f71028389b7ca2683ce946be463ee6689dfbf7176fc4a7224316` |
| shake-branches/005-read_bytes.evidence.json | `{'address': '0x585938', 'length': 8}` | `ev_f27bc5b3b8f9855f3025e0971e837b292e04b39037cfba0b62e97c572c6589ff` |
| shake-branches/006-procedure_pseudo_code.evidence.json | `{'procedure': '0x4d74e0'}` | `ev_75472b8334d087a1b4c45da089b147f306cd5d94784b2af12e4b706dcd9942d9` |
| shake-branches/007-procedure_pseudo_code.evidence.json | `{'procedure': '0x527840'}` | `ev_5d88e2c5f97fe0896c5818a56d52da032c44b2abdb29b44f9ae66fa3314372c7` |
| shake-branches/008-procedure_pseudo_code.evidence.json | `{'procedure': '0x501140'}` | `ev_159fe86c9a4256f14a57cfef8181902773b03f3da19dad7a1c55816af3084fca` |
| shake-branches/009-procedure_pseudo_code.evidence.json | `{'procedure': '0x5039d0'}` | `ev_df7ded3522a18bbfc974e7e219ae6ee5c4dc03c27e617c657d751231b457dbfc` |
| shake-branches/010-procedure_assembly.evidence.json | `{'procedure': '0x507140'}` | `ev_42cae559a682f9de42c16f349405cf50329f99235c5ec2c82cc9cefcccea7f2a` |
| shake-branches/011-procedure_pseudo_code.evidence.json | `{'procedure': '0x4d7f00'}` | `ev_817376beaed961dd7fa4a66a7ee2afba01749b09d7a16e63912dae05a2e0146f` |
| shake-branches/012-procedure_pseudo_code.evidence.json | `{'procedure': '0x503fa0'}` | `ev_0a8d5120547db746fee48b3edc3d416f0ecc3e547d840bd07f8857aeb9d27f53` |

REA证据已导出至该独立目录的evidence-bundle.json，并已close_binary保存analysis.rea；不纳入Git。
