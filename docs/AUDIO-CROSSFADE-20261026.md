# 跨曲音频交叠

## 边界与依据

复用REA/Ghidra只读取证048/050/057/058/064/065/095/096；原目标ef_first_AA.exe SHA256
9824732313d13136cb2300f33038a655ba480ae01cefc2749028d1b1748b4f9e。没有新开原版会话或执行原exe。
057/BGM与058/SE只取engine当前声部索引，给旧对象发淡出，再分配新对象；没有遍历早先退场声部。
048/050解析默认速度，050循环标志检查首字符't'；058循环同文件有早退路径，非循环同文件应重播。
旧对象+0x39被清零可观察，但实际loop字段在+0x38，不能将0x39猜成循环开关。
064原程序索引范围为0..9；原版池满策略未确认。
本轮沿用既有2*gain/speed秒连续插值近似，不升级为已逐步长对拍的原版音量波形结论。

## 实现

- AudioMixer固定4 current槽+10 outgoing槽，活动声部总数最多10。容量检查在任何状态修改前；满时明确拒绝而非抢占，是移植策略。
- 播放新曲把旧current及播放位置/loop/当前gain迁移到空tail，设独立退场包络；更早tail不被重置。
- stop_current把当前声部立即迁tail并清current，后续重复stop不延长旧tail。通道stop可停该通道全部声部，stop_all清全部。
- playing报告包括tail在内的声部；current_playing与gain仅面向逻辑current，避免尾音被误认作新曲。
- script stop清playing_ids/loops，同曲重播新建声部；同BGM只调音量，同循环SE不重播，非循环SE从头重播。
- SE循环解析兼容t/true与其余首字符false的原版规则；`.playSE * 20` 的20是loop字段，不是淡出速度。
- 回调仅执行有界混音、重采样和包络推进，无解码/分配/PCM释放；inactive PCM由collect_finished控制线程锁外回收。
- 独立审阅发现解码新OGG前读取旧gain会导致淡出时长过时，尤其旧曲刚开始淡入gain=0。已改为传线性rate，在Mixer提交锁内读取最新gain换算frames；stop_current/set_gain同样原子计算。

## 语料与验证

94物理脚本BGM427条(207播放/220停止)，SE259(183/76)，SE2 13(6/7)。SE有34条true、SE2有1条true。
最早直接换BGM阻断位于scr/100_01.sc:56。原运行在开场起441条后停止，本轮两variant各602条trace PASS。
1000条探测均在scr/100_01.sc:317遇未实现.screen停止；这仍是明确未知演出，没有跳过。
19项dev CTest、15项核心ASan/UBSan/leak通过，SDL dummy回调及真实Space/1窗口/语音/选择/chain回归通过。
PCM定点测试覆盖互补和不同淡入淡出时长、淡入中连续切换、多尾音独立、重播、当前/全通道停止、静默时钟与真实混音一致、容量异常原子性、PCM延迟释放、rate提交时gain变化。
独立只读审阅及并发修复复核通过。未改音频解码器、PAZ、清单或导入工具，无须重跑M0资源独立验证。

## Evidence IDs

- 048-procedure_pseudo_code.evidence.json: `ev_37275b3edb7f442db99ab849a411555205e0db3734df2d989268592552f61536`
- 050-procedure_pseudo_code.evidence.json: `ev_c5d058da9e49786ee7c1c53932b0ddaec1861b5e522ac844c0617e8e30e37f7f`
- 057-procedure_pseudo_code.evidence.json: `ev_05b6bb41fe1e4eed775b2c44db8419349ad29ab733ac4082f49b40c948fe34cb`
- 058-procedure_pseudo_code.evidence.json: `ev_8a00a4bb7e91c6eecbfae87f89aaf4a03af4553a352b7b2e71ef1bf9fa024653`
- 064-procedure_pseudo_code.evidence.json: `ev_3c84a919d0abd99cf3ba7fb7477c09c9182db0f301f9d102799dbf52d95ff0d7`
- 065-procedure_pseudo_code.evidence.json: `ev_2d47a9b1bf1dd52d1c8fc614d36d9d751ad393a74f3c152f648c29192c9301db`
- 095-procedure_pseudo_code.evidence.json: `ev_e7fdbdd7d328116000ddd52710935e8d6f2f42dedf9a5ed94be37104bed67def`
- 096-procedure_pseudo_code.evidence.json: `ev_b2e1e0dfbe4ebc33c99bd444a8773313d0634de7445cb24c99b5c38760b96fb9`
