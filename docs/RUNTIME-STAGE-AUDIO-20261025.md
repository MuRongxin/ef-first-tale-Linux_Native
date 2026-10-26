# 场景与音频接入记录（本地，不提交）

目标：只读原版 exe，SHA256 9824732313d13136cb2300f33038a655ba480ae01cefc2749028d1b1748b4f9e。
使用 REA CLI MCP + Ghidra 12.1.4，数据库、日志和快照均在工程 work/re，原版目录未修改。

## 观察

- Stage 语法：frame [frame_x frame_y] background crop_x crop_y [foreground center_x]；最多十个前景。
  默认前景横向以 center_x 居中、底部对齐 600；带分隔符的额外位置标志尚未接入。
- 原版 timer 每 10ms 增加一个 tick，wait 直接把参数放入递减计数器，因此 200=2s；旧毫秒描述错误。
- type0 转场 progress=elapsed_ticks×speed/10，256 完成；速度 >255 直接完成。
- 滚动每 tick 前进 signed_speed/10 虚拟像素；endScroll f 等待完成，t 强制结束。
- BGM 参数：file, in_speed(默认1), out_speed(默认1), volume(默认100)；SE/SE2：file, loop, in_speed(默认2), out_speed(默认2)。
  可选速度字段 `*` 或无效数值在原版落到2；当前实现对非数值/非*仍严格拒绝。
- 音量表对应 20log10(percent/100)，故移植 mixer 使用线性振幅 percent/100。
- 音频淡变约每2 ticks改变 speed 个音量百分比点。当前实现保留该速率、按采样连续插值，尚非逐步长波形对拍。
- 原版可跨文件交叠淡化；后续已补齐多个退场声部、循环SE兼容及原子速率提交，详见AUDIO-CROSSFADE-20261026.md。仍按采样连续插值，未声称原版离散波形对拍。

## 开发近似与限制

frame 在当前开发场景按覆盖层绘制，组合顺序未作逐像素原版对照；非零 panel 统一显示开发文字叠层。
非默认立绘标志、特殊转场/掩码、Snow/fadeout 等效果、\a/\w/\v 的 timing 仍会严格停止。
`.effect *` 只清空空状态；此 host 尚不能启动任何持续效果。
`--start-line` 仅开发入口，不恢复先前状态；从15行验证7条语音消息、47行验证7条带滚动消息，不能称全开场通关。

## 验证

- C++ 全部6196音频解码 PASS：work/runtime/audio-native-all.json。
- CTest9项、整场景合成像素与EGL合成 PASS；键盘 Space/1 +真实SDL dummy输出 +滚动等待 +选择/切脚本 PASS。
- 独立 M0 verify PASS（0失败）；完整最终日志见 work/runtime/stage-*。
- 真实包4×场景已出帧，图像使用3200×7200背景和3200×2400 frame。

## 证据索引

| 本地证据文件 | Evidence ID |
|---|---|
| 013-procedure_pseudo_code.evidence.json | `ev_22eb9ed29641a8d96f767a121ba4c93fe2e374eca598879b2485693f48eb052c` |
| 014-procedure_pseudo_code.evidence.json | `ev_087c94a4e310e058defdd2aa99c94e97a02101bd41da25ded5f9a6b1e1490006` |
| 018-procedure_assembly.evidence.json | `ev_fa75e57b36d653af8f26554ed762926760b02787bf2b3e2832870d1f250af9e9` |
| 022-procedure_pseudo_code.evidence.json | `ev_32e589db68d556ceda4ed80c1ab7db67c845df7dba52b07446adf716f8657cf1` |
| 036-procedure_pseudo_code.evidence.json | `ev_0a0a65633e74c331c6f794f841cca7f1c2528e471da5e2670a8377092974a810` |
| 048-procedure_pseudo_code.evidence.json | `ev_37275b3edb7f442db99ab849a411555205e0db3734df2d989268592552f61536` |
| 049-procedure_pseudo_code.evidence.json | `ev_69c6edaf4e35dfd0b5c61eccaf988f15a9b8e6c483697715ffb14c0bc4abbace` |
| 050-procedure_pseudo_code.evidence.json | `ev_c5d058da9e49786ee7c1c53932b0ddaec1861b5e522ac844c0617e8e30e37f7f` |
| 051-procedure_pseudo_code.evidence.json | `ev_a1bc74bc41c2238a9602f3689389835c126471c19be1b385e580c8e64bfd3cb9` |
| 057-procedure_pseudo_code.evidence.json | `ev_05b6bb41fe1e4eed775b2c44db8419349ad29ab733ac4082f49b40c948fe34cb` |
| 058-procedure_pseudo_code.evidence.json | `ev_8a00a4bb7e91c6eecbfae87f89aaf4a03af4553a352b7b2e71ef1bf9fa024653` |
| 067-procedure_pseudo_code.evidence.json | `ev_a1352de7259f2dbb762fb48f7fe6b65636f29edb02d1e9f759cdd83e10752b4e` |
| 069-procedure_pseudo_code.evidence.json | `ev_556b0adbc94354296e038f0622cbe1eb6da34321c0a108da4b6f6d4e1d6aadf9` |
| 074-procedure_pseudo_code.evidence.json | `ev_6abdfdb04a29381b56dd526dd41ea5016e5749076df84d2755a5a3e30f1de872` |
| 077-procedure_pseudo_code.evidence.json | `ev_c6946736e91fac90bb134e73ad6981b4a2c2d3715a0f3da5bd31db331d1462e2` |
| 085-procedure_pseudo_code.evidence.json | `ev_8e650a6c6133ff9adcbb8cfff069681b13f4c96b74c5a8c0fabd11fa365825a9` |
| 095-procedure_pseudo_code.evidence.json | `ev_e7fdbdd7d328116000ddd52710935e8d6f2f42dedf9a5ed94be37104bed67def` |
| 099-procedure_pseudo_code.evidence.json | `ev_e1c72c184bb53b5412bd5028409da9b4937467df19c33506471f18ad6637743a` |
| 104-procedure_pseudo_code.evidence.json | `ev_d94cd4fd571d81d7f000f233e1cb79a7a1d636a7d99c82b4f91c9b121061df22` |
| 105-procedure_pseudo_code.evidence.json | `ev_886aeb89df2fdfcc8c17ec7a9622c17555a8020ca73309efa1ac4c1a0c5b96cd` |
| 107-procedure_pseudo_code.evidence.json | `ev_1ed17388da215a49db15b9033fe72647f5ae5814274575b173ec535345840b24` |
