# 消息窗和文字排版接入

目标为原版ef_first_AA.exe（SHA256 9824732313d13136cb2300f33038a655ba480ae01cefc2749028d1b1748b4f9e）；REA+Ghidra12.1.4只读分析。
恢复先前snapshot，新证据142–185；完成后export与close。环境未安装Wine，因此本阶段没有原版运行截图，不能声称逐像素验收完成。

## 观察与坐标

- MessagePanel::SetMode 004f43b0 + UpdateGeometry 004f25d0；构造初始化004f2410。
- 普通panel1使用msgPanel.png；图247高，layer y=600-247+64=417。姓名rect相对(38,39,764,231)，正文rect相对(38,39,786,247)。
  屏幕原点(38,456)，有可显示姓名时正文先换一行。正文宽748；姓名宽726。初始屏幕可见正文144高，原生内部缓冲208高。
- 长消息原版004f3980会在文字接近底部时移动panel；移植预排版后按已揭示行数利用额外64px上移，不丢正文。
- panel2读取fullPanel.png，name=(15,12,125,588)，body=(125,12,785,588)；该阶段只按单条消息显示；后续已补齐跨消息累积，见NVL-FLOW-20261025.md。
- panel3默认素材BLACK800x75.png（不是375px），图层放(0,0)/(0,525)；name=(37,530,125,610)，body=(125,530,763,610)。
  运行时将可见矩形限制到600下沿，故字幕框高70，正文宽638。
- body/name字体默认22，ruby10，行高=22+10+全局0=32；Glyph advance默认额外2px，见00519640。
- 姓名处理004f30e0返回0普通、1@未知、2#隐藏；004f3390将1显示为? ? ?，2姓名文字置空。普通mode1姓名拼接« / »，源码原始字段保留。
- 推进条pauseAnime.png，22px方格，12帧；00518a40每10ticks换帧，X=(cursor_x+5)&~1，Y=cursor_y+line_height-frame_size+2。
- namePanel.png虽然存在，但未在当前MessagePanel路径看到引用；不按文件名猜测其用途。

## 移植选择与限制

- examples/message.ui.json为必要运行时数据契约；导入为content-addressed blob，由script_index.message_ui引用。
- 刷新命令 --refresh-ui 使用同一.import.lock，仅原子替换入口manifest和新索引，不重复复制37GiB资源，旧blobs保持不变。
- FreeType使用Noto Sans CJK；实际像素生成字形，逻辑字宽按font units计算，缩放不改变换行。
- 完整正文预布局+逐码点可见数，中文常见标点禁则，不在逐字揭示时重排；矩形超出时明确报错，未静默删字。
- 复刻了基本1/2/3模式；后续已补齐模式2正文积累及panel交叠淡入淡出，见NVL-FLOW-20261025.md和PANEL-TRANSITION-20261025.md。NNL专门mode10尚未复刻。
- 字体和字体光栅器不等同原版GDI/黑体，基线、描边、阴影须实际原版截图继续校准。
- UI位图当前包仍是1x回退，未冒称4x/8x UI重绘；文字始终矢量光栅化。

## 验证

- 14项普通CTest、12项core-only ASan/UBSan/leak（含FT布局与UI元数据测试）为本轮目标。
- 开场两个变体各427消息，按content实际Noto字体font units测量所有文本框，0 overflow。
- 软件GL合成素材验证normal/cinema/135字长消息、真实透明混合、姓名/#隐藏、CJK、12帧atlas裁片；旧键盘/窗口4x8x回归。
- 独立M0 verify PASS，0失败；日志见work/runtime/message-ui-*。
- 导入器测试验证索引/脚本/资源不改写、metadata幂等、旧blob不变、缺资源回滚、目录约束和互斥锁。

## 证据索引

| 文件 | Evidence ID |
|---|---|
| 147-procedure_pseudo_code.evidence.json | `ev_654bd9fe11d98f41c1c52ebf6a83716d1c00a838bf5f96791dda553c150e1651` |
| 148-procedure_pseudo_code.evidence.json | `ev_6b8b40ce36f7155be978abaebe61fe279ea74d3a4e8b92de8fa640e7e4d66cab` |
| 149-procedure_assembly.evidence.json | `ev_a80e0d9e1c84c333430a5c9db25c9eb69c161f00bb02a93b29f00d09dfa3d966` |
| 150-read_bytes.evidence.json | `ev_e441f8690f559ce51f564cdc21d476390e5f217e88862e766608e5261ccf38c8` |
| 151-procedure_pseudo_code.evidence.json | `ev_b296d62bc070c0cd8be96e608dcbed5be1a859c7bc79fa71527e4cbefd9eeab0` |
| 152-procedure_pseudo_code.evidence.json | `ev_00c7bc56060102968d4ad482f3df446b558a795ec726f466f46fd21fd44945d5` |
| 156-read_bytes.evidence.json | `ev_f1d4bd9420087d5aec435205882260f15cc217c814e2f94a07036b887d032093` |
| 158-procedure_pseudo_code.evidence.json | `ev_6f7f262c31e239a6ab26ebd1d4cc06974af6f46f7d6f3601801136f582f9e290` |
| 160-read_bytes.evidence.json | `ev_ee860388f6f9d44bba5ca87d5c883bc4f08dbfecd6ce504eef9355366ad461ef` |
| 161-procedure_pseudo_code.evidence.json | `ev_60635ddd7e758ed8fbbfee939fceb568f18db2fb50bdcd658165c0f29a0110b8` |
| 164-procedure_pseudo_code.evidence.json | `ev_6b3ff4fe9909379aae94b520e5a2f1c15add328c93fddf01be24ce92909beaaf` |
| 167-procedure_assembly.evidence.json | `ev_deffefdeda91e452dcb121d240def919d1ca39f7f82bcf22d65bdc3f0113096e` |
| 169-procedure_pseudo_code.evidence.json | `ev_e4e6adf4899089ef8eccaa798ccc446baff30aec00e91f58b4e45b5a5ab37e30` |
| 170-read_bytes.evidence.json | `ev_e4fc4f98ac0a25f543d375375b679a72595a81c25e9e668c421672226f572593` |
| 175-read_bytes.evidence.json | `ev_b6cb1462b2872310c612728cfa59c76aa0fcc07622bc7969f92465fb68467f82` |
| 179-procedure_pseudo_code.evidence.json | `ev_bea14c1f6229953e7c332dd9426b6656aadb1cfddee2fc9c4c827a551d15a9aa` |
| 180-procedure_pseudo_code.evidence.json | `ev_b43ea34f74a529d9a415d8c2b6b771afd891be86e447c6b5fc7954d33eca8022` |
| 181-procedure_pseudo_code.evidence.json | `ev_31b64233eebafad624e189b9109cd94322a66c7d905c6ad351b47a97a4a91148` |
| 182-procedure_pseudo_code.evidence.json | `ev_a9d07782c583b5aae85e82d5fe2e1b01288e5eab6e21fe068f292d2aedffa170` |
| 184-procedure_pseudo_code.evidence.json | `ev_3ac3f66af63cf73b605debd03e7a93b67f6e951c84a041dc6189a91b2255ebaa` |
