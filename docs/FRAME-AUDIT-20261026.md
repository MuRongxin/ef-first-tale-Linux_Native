# .frame 取景器静态取证（2026-10-26）

状态：已确认下列原版行为并实现语料范围，测试结果见文末。特殊转场沿用用户最新提交 3004b44，不重新实现。
本文件按用户最新要求随 docs/ 提交；取证产物与 AGENTS.md 仅本地保存。原版目录始终只读。

## 观察：命令与图层

语料共28次调用，7份脚本（含r18重复）：14次 finder.png，14次 *。
CommandFrame parser 0x4ee1f0取第一个字符串；Execute 0x4ee460 调用0x4dcda0(engine,name,false)。
0x4dcda0更新 pending 图片/层（+1d1c、+1da4），*清除；普通命令不立即写当前层。
0x4d8b70在转场快照期间切换+1d35/+1dad使当前/待切层分别参与旧/新场景合成；0x4df4e0调用该路径。
0x4db320提交 +1d1c 到 +1d18/+1d2c 并设置 enabled。
0x4dfcb0初始化本层优先级0xe6398=943000；已确认普通stage frame为941100，效果为930000。
因此.frame叠在stage边框之上，是随下一舞台切换生效的独立叠层。

## 观察：finderCounter

0x4b5860只观察全局本地变量池且变量名为finderCounter：写入值*100到DAT_005ac91c，清除计时缓存。
0x5353a0把公共时钟增量累加到该计数器；0x5354c0建立10ms基准。时钟不依赖finder是否可见。
0x4b6570加载finder时从变量池（或原版恢复状态）取初始秒数，调用0x4b6290绘制。
0x4b6a60每次更新取counter/100，上限35999；0x4c94d0无通知地回写变量避免循环重置。
必须能够观察重复赋相同数值的.set，不能只比较变量前后值。

0x4b6290/0x4b58a0绘制H MM SS五位数字；atlas为finderNumber.png，原生30×506，11行，每行46。
0x5798a8五坐标：(597,13)、(641,13)、(672,13)、(716,13)、(747,13)。
原版使用位图数字；移植产品要求矢量文字，未来实现需要明确区分原生位图证据和FreeType字体选择，不能声称数字外形已逐像素对齐。

电量位于(28,12)，原生尺寸59×35。秒数<=18000为batFull.png；18001..25200为batMid.png；25201..32400为batLow.png；>=32401为batZero.png。
依据0x4b6290的严格大于比较、0x5a9bb8指针表与0x579768字符串。

## 实现范围与限制

- 已实现 pending/current分离、独立叠层、旧快照隔离及跨stage保持。
- 已实现 .set观察与无通知的定时回写，包含相同值重置、小数时间累计、隐藏时钟语义；只回写已存在的local，不新增变量或覆盖global。
- 数字矢量渲染已接入舞台合成，保留转场和消息窗层序；使用当前字体face，固定46逻辑字号和浅蓝色是移植选择，不套用正文缩放和外观覆盖。原生数字外形另行校准。
- 已实现四档电量阈值与开关，实际28处×e/j共56片段回归通过。取景器底图自带满电图标；分片绘制排除(28,12,59,35)后单次合成电量RGBA，避免低电量叠在原满电上。非整数缩放边界仍存在与原版预合成插值顺序不同的限制。
- timer输出钳制35999，显式赋值仅支持0..35999（移植范围约束，超出严格报错）；原版35999附近可能触发的场景退出尚未复刻，不能宣称完整支持十小时持续计时。
- 原版运行画面逐帧对照尚未完成。
- 原版读档/回想分支已有证据，移植存档接口尚未接入，不能宣称完整支持。

## 证据索引

| 记录 | Evidence ID | 请求 |
|---|---|---|
| 418 | ev_fa30e9df37d8250be1e90ce93083213a2335b6690d7065922204e96f0f746144 | {'name': 'CommandFrame::vftable'} |
| 421 | ev_1407aa0102baee62434d8477407c1aa5136afd934da8905164aec644089eca12 | {'procedure': '0x4ee1f0'} |
| 422 | ev_4e5203e2f69361bdb91432bcd0805051ba33c63280cbd6d218d44b4ff94815e1 | {'procedure': '0x4ee460'} |
| 424 | ev_d91a2e3256a8f588c499176d1a241d13ff3bea9decf69f252ace0ab303c79524 | {'procedure': '0x4dcda0'} |
| 425 | ev_15e08fb82d79b1b950fbafd13720d993c3a095c34d856183808d107180d95240 | {'procedure': '0x4b5873'} |
| 427 | ev_5c7b809db427fc950e6e5667ff2ea1cb49a4f288a73f7ff0c4362e1c388934f8 | {'procedure': '0x4b65b7'} |
| 428 | ev_518b6604473f3e2908a7280e3ddb150b6671834c8fc6eba7433b2b479e984b0c | {'procedure': '0x4b6afd'} |
| 429 | ev_408dfd8134400746fa3e76aab8c6a713802c2cf2c02a8fdc2f499995a89bf56c | {'procedure': '0x4b6290'} |
| 431 | ev_51bb4d2b2ff749e2839d21d98e8837d23dff7ec3b78e305f3b909c4323589835 | {'procedure': '0x4b5860'} |
| 432 | ev_ebad858f37aa8a63fcb1e9070aaa00a27c6b166f9f2645778ea1d7af4c77ed4e | {'procedure': '0x4b58a0'} |
| 433 | ev_01f2f27296b701235ad7416482255ddebe93f88b70d9c72748d17d10806fc81f | {'procedure': '0x4b61a0'} |
| 434 | ev_0fe783c388289ad0528e7f266bd4e2e247bea161fe0ba696dba5dad9eb3f82d9 | {'address': '0x5798a8', 'length': 40} |
| 435 | ev_4c2eac9a6b7666177b40b03bd0185cfc419b0e05881292f9ef474ed730dadd06 | {'procedure': '0x4b6290'} |
| 438 | ev_6fd074e582e6dad4d71edf8759574a6a7d16f2770dac494785f59ef4fa12d9f9 | {'procedure': '0x5353eb'} |
| 439 | ev_c021c9d4a6e05269c01c6b85b824ba86eb2e6f27b0ed5b7f45735dd2b3e16bff | {'procedure': '0x53551e'} |
| 440 | ev_6a6df4e84a120ce922c37eaf6832441a52bcaa5d44a26653b2bf7d52349aac5f | {'address': '0x5a9bb8', 'length': 16} |
| 442 | ev_1a2ba8b93eab3b72f54aa1b94e381c0c993ea14bcf80448c74b20c48795ad0a2 | {'address': '0x579768', 'length': 52} |
| 443 | ev_7ff3ba52bd39f7a3b974c904c757c850420072179f33ecfedd47a5ee4402f5e9 | {'procedure': '0x4db320'} |
| 389 | ev_945929256052d9fc86981d95c5beaf9dbd415aec5fd12614c6d1267fec1cabdf | {'procedure': '0x4dff10'} |
| 441 | ev_634ee1d8b82e79004d41627ae7506d89e478998f471bd729e23bfbabe6204618 | {'procedure': '0x4df4e0'} |
| 308 | ev_586bdaf927ead780af1f983cf1707489c3b2c1c5d15d39b6cb29576ec2f8c7c8 | {'procedure': '0x4d8b70'} |

## 验证记录

- Presentation/Script：计时跨秒、隐藏累计、同值赋值重置、同frame不重置、H/MM/SS坐标、全部电量边界、pending开关、旧文字快照、非法更新原子性，以及paused message时回写变量并供后续脚本算术使用。
- GPU：FreeType数字在1x/2x及自定义face下参与舞台FBO和旧快照冻结；RGBA电量替换在4x/8x请求及800/1200宽视口下验证无重叠alpha、满电残影或接缝空洞。
- 真实语料：work/runtime/frame-corpus-traces.json，28处×e/j共56项全部到达下一消息；scr/215_01:130起e跨662消息到scr/216_01选项，无.frame阻断（1000请求因到选项退出，非1000条PASS）。
- 开场：e/j各1694消息PASS；真实215_01:136起窗口与截图检查通过，计时和消息窗顺序正确。
- 独立M0：work/verify/verify.py PASS，失败0，5989文件和归档digest一致。
- 本轮工作区另有外部资源优先级修改；本轮仅提交.frame代码和相关测试/README，数据解析修改保留给其所属工作。

### 贴图拷贝补证

REA记录449（ev_492a378a346421433efa0ed18cb0b39a8b0e94630bc41a13abf86434e74152cd）：0x522d20在非零目标偏移的CPU路径逐行memcpy，复制字节数=目标每像素字节数×裁后宽度，没有alpha混合。0x4b6290电量目标为(28,12)，因此分片排除旧电量再合成新RGBA有直接依据。
转场期间incoming finder文字/电量也冻结为stage时快照，计时器独立继续；转场提交后刷新到当前秒数。对应0x4d8b70先构造old/new场景与0x4db320提交图层的路径；不声称全局时钟相位已逐帧对拍。

最终验证：dev 25/25 PASS，core ASan 18/18 PASS，包含incoming HUD冻结/提交刷新测试。
