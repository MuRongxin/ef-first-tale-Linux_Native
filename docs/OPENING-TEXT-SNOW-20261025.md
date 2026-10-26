# 开场文字时序与雪花接入

## 目标与边界

只读原版 exe：SHA256 9824732313d13136cb2300f33038a655ba480ae01cefc2749028d1b1748b4f9e。
REA MCP CLI + Ghidra12.1.4；使用先前快照，新增证据113–139；已导出 work/re/opening-text-snow-evidence.json 并关闭会话。
本文不代表已完成原版 UI 或逐像素复刻。

## 观察

- FUN_00519a20：a 设置 engine+1c69，w 设置 state3 和10tick倒计时，v 设置 resume_state4 后进入 state2。
- FUN_004e11a0 state2 等 voice slot +1cc8 空闲后恢复保存状态；state3等待100ms回state4；state4文字结束有a则state0，否则state10等待输入。
- 因而 a 在正文处理完成后自动取下一指令，不可丢弃含正文的整条消息；w 是固定时间暂停，不是点击等待；v 是等待语音。
- pragma disable_control 设置+2cb8，enable_control清除该位与+14e1；文本state1/10对应路径把自动等待参数置0并禁止手动推进，等待语音闲置后继续。
- SESnow初始化50粒子，加载snowS/M/L.png；SnowH复用同一组精灵，改为水平发射。
- 粒子基于16.16坐标，MSVC rand递推214013*x+2531011，输出高15位；速度/大小分级见133/134/136证据。
- fadeout设置效果+1f并重置淡化累积计时，不是把整个场景淡黑；update按累积tick>>4递减alpha，清空后结束效果。

## 移植选择与未定项

- 文字按UTF8码点处理，先导入解码，无GBK尾字节反斜杠误判。
- 开发默认40字符/秒，--text-speed0瞬显；原版文字速度配置到CPS的映射尚未校准。
- 点击快显只显示到下一时间/语音屏障；控制锁定段落不响应手动推进。
- 雪花固定seed=1，原版随机种子来源未知；透明混合和重绘采样不保证与原程序逐帧像素相同。
- 雪花更新依据100Hz逻辑时钟以及宿主实际更新间隔，沿用累计tick>>4的alpha公式；原程序性能配置的更新节流尚未复刻。
- 显式兼容[e/j]:.语法是移植兼容选择，保留raw与物理行号并给诊断；原版对这个拼写的解析细节未取证。
- 雪花按内容包资源精确名读取。当前包这三个小精灵只有1x，运行时记录回退；没有偷偷AI放大。
- 开发文字面板、字体布局常量、其他特效、特殊转场、跨曲交叠淡化仍不属本阶段保真完成项。

## 验证

- scr/000_01mugai.sc共1057行、427消息、14个a、1个v；无w。全部stage只有frame/bg/crop，全部transition为type0无mask。
- e/j两变体均从第一行严格执行428条消息：427开场 + scr/100_01.sc首条，未使用text-preview或start-line。
- 无头trace按消息事件序号计数，快进时间，不代替全程画面/声音逐帧对照。
- 真实SDL窗口已验证Snow、矢量中文逐字显示、点击快显、w/v屏障、自动消息、滚动等待与分支切脚本（合成素材）。
- 普通测试11项 PASS，core-only ASan/UBSan/leak9项 PASS；独立M0 verify PASS。
- 引用审计：14955引用，9160唯一，missing=0，文件缺失0，19个既存跨归档歧义未改变。
- 日志：work/runtime/opening-*；真实截图opening-snow.png。

## 证据索引

| 文件 | Evidence ID |
|---|---|
| 108-procedure_pseudo_code.evidence.json | `ev_31fab3e885ae383591500c9399d9aa352c163629396018bf3e6bc5ea9582f34a` |
| 114-procedure_pseudo_code.evidence.json | `ev_032034a50b4ce83aaa0a005eb9e0f6e6581073ebcca0f81a7e9d299ce8566802` |
| 117-procedure_pseudo_code.evidence.json | `ev_ce27a258e00b557877f7d4946ae4a32d086e237912cd3ee53bc052df83c899aa` |
| 119-read_bytes.evidence.json | `ev_73c98db90f1a7677f0581f490adb6da1ddfac18eec86f18a08e0b4feae97f0e1` |
| 124-procedure_pseudo_code.evidence.json | `ev_4c9f55c442218ea67ba0e9fb1ef6ad3881cd6c000ce6058013cf1669d6a226be` |
| 125-procedure_pseudo_code.evidence.json | `ev_af76e0a79b50e713e9b010d3dc3f470aed72327e4dca4c91b78d6cc6c774e0e8` |
| 126-procedure_pseudo_code.evidence.json | `ev_f43deb89cd5422805b394351db81054e630518105523751759e72cfa075e53fc` |
| 129-procedure_pseudo_code.evidence.json | `ev_197f5d0ebc847ef35557881c659b79e868bb8effe7dada738251b874cf42bd23` |
| 130-procedure_pseudo_code.evidence.json | `ev_38d61afa6eb1db1a232a69b45155d0c1f0910b24b7245959fce66ee443a99fca` |
| 133-procedure_pseudo_code.evidence.json | `ev_8d7aebb650009fd14768dd3a22ad20e4b1601133efdb9db5decaae96474abffa` |
| 134-procedure_pseudo_code.evidence.json | `ev_a65bc66879e911f8ced16500a3c52ca8925d75934d58b24eabc09deea450840f` |
| 135-procedure_assembly.evidence.json | `ev_1438b0545251ead6b954badc2bb093f2761472c1a8152d3e89155d0b444985dc` |
| 136-procedure_pseudo_code.evidence.json | `ev_49fb7645eee82a8ca35810f58656d05cda1277a4dbb7fe098471c834123814d6` |
| 137-read_bytes.evidence.json | `ev_76b72d97860ff74148c449c769576308be9237f06aeb6d4a4f8959538f5f889b` |
| 138-procedure_pseudo_code.evidence.json | `ev_928bc96b772e2385745bc15db4aa3b96682482a3455badcebe50bf2bfdcc0a4b` |
| 139-procedure_pseudo_code.evidence.json | `ev_7d37903c4b9fef6cfb8f2588d7df6d75157cc9d26d4242e0e803deb4a6ee87e6` |
