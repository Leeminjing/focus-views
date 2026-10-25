# R5-001b 首轮完整矩阵：失败，原始证据保留

本轮使用独立数据记录器与原 fixture，复用原 R5-001a 内核和输入组件；不是最终通过记录。

- fixture：20 次场景，3,746 项证据检查通过。
- recorder：22 次场景，4,037 项检查中两项失败，21 个场景完成证据校验。
- 失败场景：`recorder/unknown-resource-1`，RB 的未知资源已按预期拒绝，但后续 C 的实际 EDIT 输入产生错误内容，`C.final-edit` 断言失败。
- 本轮完整脚本失败；旧 R5-001a、R4-003 及其完整嵌套回归通过。该回归成功不覆盖本轮红项。

## 实际输入差异

[初始诊断](input-startup-diagnosis.json) 来自目标线程真实钩子记录。C 的首个 barrier 尚未应用键态时，`GetKeyState(Shift)=-128`、左 Shift 表项为 128，左键表项为 1（低位）。此时通道还没有处理任何受管键盘按下事件。

后续 Home 被原生 EDIT 按 Shift+Home 处理，选区没有归零；最终输入 C 把 `ROW-91abd66e` 改成 `ROW-91abdC`，实际选区为 `10..10`，而非期望的 `C` / `1..1`。原始消息、键态及资源状态见该场景的 `C/input-1.jsonl`、`C/events.jsonl`、`C/final-edit-resources.json` 和 controller 日志。

复用 DLL 的 `apply()` 读取当前线程键盘表，仅覆盖已有 `owned` 的键。共享客户端此前没有在新通道开始时取得左右修饰键的所有权；因此未接管的原生位会影响第一批业务按键。没有直接证据确认这些初始位来自哪个系统输入源，不能把它归因于特定宿主操作或组件。

后续修正是共享调用方通过既有受管入口提交左右 Ctrl/Shift 的 key-up 与 release，并在目标线程确认松开后才接受业务输入。两款应用使用相同握手；恢复内核、target.lib、helper、输入 DLL、fixture 和原生控件查询均没有修改。新握手不支持恢复按键仍持有的交互现场。

`pre-neutralization-build.zip` 保留本次 manager、两种客户端、second.exe、构建身份，以及握手前的 host/test-app 源码。它们与本目录身份记录对应，不用于冒充修正后的构建。失败不改记为能力通过，不覆盖本目录后重新运行的结果。

## 更早的开发诊断

`development-json-failure/` 保留一个无效目标事件和当时的失败 summary/identity：记录器日志中的 samples 数组曾遗漏结束符，独立证据解析拒绝了全部记录器场景。后来修正日志生成，再构建并运行本轮矩阵。该开发运行不计入成功数量。

修正后的最终验收单独保存在 [新结果目录](../20261025-r5001b-neutral/report.md)。两个目录的结论、构建和原始日志分别保留。
