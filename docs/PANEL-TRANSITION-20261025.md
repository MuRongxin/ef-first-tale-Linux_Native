# 消息面板切换

目标：原版 ef_first_AA.exe，SHA-256 9824732313d13136cb2300f33038a655ba480ae01cefc2749028d1b1748b4f9e。
使用 reverse-engineer-anything / REA / Ghidra 12.1.4，只读；复用 nvl-analysis.rea，新证据200–216。
已导出 work/re/panel-transition-evidence.json 并 close_binary 到 panel-transition-analysis.rea，MCP已退出。

## 原生观察

- 214 CommandPanel parser(004eb630)单参数时将speed字段+0x44设为-1；211 Execute(004eb5f0)和209 helper(004d8e70)汇编确认speed传给MessagePanel.SetMode。
- 091/210 SetMode(004f43b0)将speed<=0改为16；新建incoming背景/文字图层+0x20/+0x24，alpha初始0；旧current图层+0x18/+0x1c保留。
- 184 helper正常非skip路径设engine state0x11、DAT005ac920=0；skip路径0x12。相同模式且无自定义图时SetMode返回0，不进入等待。
- 114主状态机+207汇编：state0x11将DAT005ac920传给MessagePanel vslot+0x68，返回busy时停止后续取指。
- 205/206 004f2e90：incoming alpha=min(255,speed*ticks)，outgoing alpha=255-incoming；返回ticks<256/speed。
- 085计时器interval为10ms，107将累计的10ms ticks加入DAT005ac920；默认speed16，因此第15拍alpha240，第16拍完成，总计约160ms。
- 202 Finish(004f3c20)销毁旧层，promote新层，alpha设255，再调用+0x84=004f25d0，将文字绘制器重新绑定到当前面板。

## 语料边界与移植实现

独立只读审计：94物理脚本566条panel，0=283/1=230/2=8/3=45；全部单参数。e过滤566，j559；无模式4/5，无物理相邻重复。
移植仅对已支持模式0/1/2/3接入原生默认渐变。MessageUI.transition_to在切换时保存独立旧Scene，逻辑新页立即清理；scene复制快照并分别乘旧/新alpha。
切换中incoming只呈现空底图，完成后释放旧快照，下一消息才生成新文本。重复模式不清NVL、不等待。
ScriptView在.panel执行前捕获旧Scene；host.tick推进Presentation（含音频）和UI，host.busy合并两者。脚本下一条stage/voice/wait因此不会抢先。
Runtime运行态输入忽略，不快进过渡或积累到下一条消息。headless大delta完成过渡；0delta不改变alpha。
恢复/语料审计仍可用立即set_mode；脚本事件用transition_to。

## 观察与推断的界限

上述透明度、时钟、阻塞和旧层生命周期来自静态代码与汇编，不是原版运行截图。
旧文字内容通过旧图层在Finish前保留的生命周期恢复；移植用Scene文字快照而非原版GDI位图。当前字体偏好会作用于快照。
原程序计时器全局相位无法由静态代码确定；移植从切换时刻累计确定性10ms相对时钟，边界可能有不足一拍的差异。
skip state0x12已有证据，但移植尚无跳读功能，不借此新增未经实现的skip策略。整体绘制顺序及像素保真仍需Wine截图对照；运行时无Wine依赖。

## 验收

- 19项dev CTest通过；15项核心ASan/UBSan/leak通过。
- 新定点测试：0/10/150/160ms量化、旧/新互补alpha、old reveal冻结、退出保留/释放、重复模式/NVL、非法参数/大delta。
- 新ScriptView集成测试：message/stage/voice等待160ms，切换中输入无效，panel0后的wait起点正确，重复panel保留NVL，重进不闪回旧文。
- Message UI和真实Space交互NVL窗口回归通过；字体渲染检查调整为30帧，确保在新增面板等待后验证实际消息。
- content实际开场e/j各427消息严格无头trace通过；独立只读审阅无阻断性问题。
- 没有修改PAZ/资源数据层/导入器/清单逻辑，因此本轮不重跑M0独立验证。

## Evidence ID

- 085-procedure_pseudo_code.evidence.json: `ev_8e650a6c6133ff9adcbb8cfff069681b13f4c96b74c5a8c0fabd11fa365825a9`
- 107-procedure_pseudo_code.evidence.json: `ev_1ed17388da215a49db15b9033fe72647f5ae5814274575b173ec535345840b24`
- 114-procedure_pseudo_code.evidence.json: `ev_032034a50b4ce83aaa0a005eb9e0f6e6581073ebcca0f81a7e9d299ce8566802`
- 184-procedure_pseudo_code.evidence.json: `ev_3ac3f66af63cf73b605debd03e7a93b67f6e951c84a041dc6189a91b2255ebaa`
- 202-procedure_pseudo_code.evidence.json: `ev_f0fadd40df03b6925c034982ed2f4e61de4206110205309ae11c7af8c9adba80`
- 205-procedure_pseudo_code.evidence.json: `ev_3c14bd18c3e8413f74a0e5cec3fb829254d09175d42603c61b4649407a6226ce`
- 206-procedure_assembly.evidence.json: `ev_6e878f252bde42d493a55f090b7f24818afd6cc8742848085bd0f53108a976c2`
- 207-procedure_assembly.evidence.json: `ev_0ed7c086a0882fa5de918fa0087594d1269b6c209a7e90caace762a87ef0fa81`
- 209-procedure_assembly.evidence.json: `ev_601fa5885920edd3f84a07444d2ebc323463ed72ac60f718b5cd2c09ded35862`
- 210-procedure_assembly.evidence.json: `ev_a80e0d9e1c84c333430a5c9db25c9eb69c161f00bb02a93b29f00d09dfa3d966`
- 211-procedure_assembly.evidence.json: `ev_43e3dcd03e5a462e835b99605d59feb4e1f06a8ef0c398e0b5d967cfb1191830`
- 214-procedure_pseudo_code.evidence.json: `ev_15accaa1a5df4e4dc53d5d29b0c0ca633b1345fcdba613a0bdb4c4a56013f3fd`
