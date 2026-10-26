# .stage 逗号位置参数与底部裁切（2026-10-26）

## 结果与范围

语料34处调用、4份脚本（含r18重复），全部形如 `600,75`。已确认其含义是 `center_x,bottom_trim_pixels`，**不是缩放百分比**。
默认普通舞台路径：底部裁掉75个原始设计像素，再按裁后高度贴齐逻辑600底边。HD素材按设计坐标取同一裁切区域，不重采样整张图来代替裁切。

## 原版观察

- CommandStage parser `0x4e4f70` 搜索 `0x5749f8` 的逗号（静态字节 `2c 00`）；逗号左侧进`this+0xbc`中心坐标vector，右侧进`this+0xcc`附加值vector。
- `0x4e5ac0` 重置十个位置和附加值为0，省略逗号默认为0。
- Execute `0x4e5f10` → `0x4dfcb0` → `0x4cf660` 保存这些vector。
- 调度 `0x4df4e0` 在 `0x4df998` 从第三vector读取值至ECX，然后 `0x4df99c` 调用 `0x5081a0`；此ECX是Ghidra伪代码中的隐藏参数。
- `0x5081a0` 把该值交给 `0x5068e0` 的第三参数。`0x506ee5..0x506f26` 在值>0时检查源高，并直接把bitmap的`+0x6c`高度改成`H-trim`，不移动像素起点、不改变宽度或缩放图像。保留原行步长。因此丢弃的是底部行。
- 随后才做HSV、overlay、screen（`0x506f29`之后）。色带底对齐必须使用裁后高度，而不是资产原始高度。
- 返回`0x5081a0` 后普通路径设置 `x=center_x-(W/2整数除法)`、`y=600-(H-trim)`。因此宽389的图在center400时x206，不是205.5。
- 原版trim<=0不裁切；trim>=源高时加载器失败。移植选择对后一种情况严格报错，保持当前舞台不变，避免原版静默缺层。

## 实现

`Presentation` 严格解析单个逗号及两侧整数；每个前景独立记录`Layer.trim_bottom`和裁后绘制高度，下一场景不继承前一层的裁切。
`Renderer` 保留共享原图纹理，UV只覆盖顶部`H-trim`范围；screen/overlay的偏移按裁后高度计算，UV到原图像素的比例仍按完整源高。
裁切底边的采样钳制到最后一个保留的物理像素中心，避免1.5倍等非整数窗口的线性过滤混入已裁掉的下一行。随后普通图片/UI/文字绘制均重置该限制。
旧场景保留自己的裁切与材料状态；未改变`.frame`取景器的独立层序和计时快照逻辑。

## 验证

- 最终 dev 25/25 PASS（含新增底边采样GPU检查），core ASan 18/18 PASS。
- Presentation：75像素裁切、宽度不变、底对齐、奇数宽度整数居中；0/负值/只剩1行、空字段/多个逗号/非整数/溢出/超高拒绝、失败原子性与下一stage重置。
- GPU：4x/8x请求、800/1200宽视口；裁切先于HSV→overlay→screen，alpha与底边滤色锚点、无垂直缩放、删除行不渗入边缘、随后未裁图片恢复正常。
- 所有34处×e/j共68片段到达下一消息：`work/re/stage-placement-20261026/corpus-traces.json`。
- 四份脚本从头各跑250消息，e/j共8项、合计2000消息全部PASS：`full-script-traces.json`。覆盖同脚本多次裁切切换及216_04的Walking结束衔接，不宣称全剧情验收。
- 实际106_01:314起窗口与截图检查：人物保持原比例、头部与上方75像素条带对齐、原版消息窗正常。

## 限制与协作边界

其他位置模式（`+0x46c/+0x46d`）与脚本变量式`@...`未扩展；本轮支持普通舞台的语料形式。原版整画面逐像素、全局采样相位对照仍未完成。
没有新增`.movie`或r18加载策略；这些由用户独立取证。未修改PAZ/解包/清单/导入器或原版数据目录。
文档随docs提交；原始证据、截图、REA缓存和AGENTS.md保留本地不入库。
REA使用独立`work/re/stage-placement-20261026/`证据目录和短socket目录`work/re/stmp/`，不触碰用户movie会话；第一次深路径socket启动失败已记录并通过短路径修复。

## 证据索引

| 记录 | 地址/请求 | Evidence ID |
|---|---|---|
| 旧记录 013 | `0x4e4f70` | `ev_22eb9ed29641a8d96f767a121ba4c93fe2e374eca598879b2485693f48eb052c` |
| 旧记录 014 | `0x4e5f10` | `ev_087c94a4e310e058defdd2aa99c94e97a02101bd41da25ded5f9a6b1e1490006` |
| 旧记录 024 | `0x4cf660` | `ev_ff5937e8ae9be75cfe4c11a924f78b69205947ced9fd67434bf529a5eacb34d5` |
| 旧记录 036 | `0x5081a0` | `ev_0a0a65633e74c331c6f794f841cca7f1c2528e471da5e2670a8377092974a810` |
| 旧记录 309 | `0x5068e0` | `ev_1dec0acbf7bcfe60960df98eb68cea40d7e49bf86ddee8454d5f950571250c78` |
| stage-placement/004 | `{'procedure': '0x4df4e0'}` | `ev_c81484619b5c0ac7a90b80dc139b5fa62e911f411f2dd17566f829780e0803f8` |
| stage-placement/005 | `{'address': '0x5749f8', 'length': 4}` | `ev_233d6bda54be7c28f11fbf5ca37ff9b1013587ebc9ff9b7d893592a756cc1a54` |
| stage-placement/006 | `{'procedure': '0x4e5ac0'}` | `ev_e7054f8746edbe58b6538d63ae82b9924d5d29ad3d83ef850fbc3baf26fb8e88` |
| stage-placement/007 | `{'procedure': '0x5068e0'}` | `ev_54694826e0639bd338d306f2209dc8eeab26a1d6487421a7eca495b9dbbd534a` |
