# R3-005c-001：同一 S0 的并存原生 GUI View

基于 R3-005b-001 的固定顶层窗口、标准 EDIT/BUTTON、七个显式 HWND 引用及 CET-002 编译协作续接，验证同一份 S0 恢复 A/B，分别改变真实控件和受管内存，再在两者继续运行时恢复 C。没有扩大 EDIT 或输入 API 的支持范围。

## 构建与运行

~~~powershell
New-Item -ItemType Directory -Path '.\experiments\r3-005c-001\out' -Force | Out-Null
.\experiments\r3-005c-001\build.ps1 -MsvcRoot '<MSVC 根目录>' -SdkRoot '<Windows Kits\10>' -SdkVersion '<版本>' 2>&1 | Tee-Object '.\experiments\r3-005c-001\out\build.log'
.\experiments\r3-005c-001\out\r305a.exe --output '<全新目录>' --scenario AB
.\experiments\r3-005c-001\out\r305a.exe --output '<另一个全新目录>' --scenario ABC
.\experiments\r3-005c-001\out\r305a.exe --output '<另一个全新目录>' --scenario fail-B
.\experiments\r3-005c-001\test.ps1 -ResultsDir '<全新结果目录>'
~~~

默认三个场景各执行两轮，然后运行未修改的 R3-005b-001 全量回归（内含 CET-002 全量回归）；需要先构建这两个旧实验。-Repeats 1 -SkipRegression 仅供诊断，未执行的回归会明确标记。每次使用新目录，保留旧结果。三个完整场景成功时 EXE 返回 0；运行失败返回 2，不能把 B 的预期拒绝单独当成整项通过。

## 本轮机制

- 一个创建/调试线程拥有唯一的 WaitForDebugEvent 入口，以 PID 查找分支、以 TID 核对线程归属。每个分支有自己的调试停点、等待阶段、期限、异常和退出状态。等待 A 的辅助进程时仍分发 B/C 事件；不会把其他分支的退出当作 A 的退出。
- 每个 View 有独立的原生进程、GUI 线程、后台 Desktop、输入桥和共享输入通道。只有当前恢复目标在 CET 检查点停止；已发布的其他 View 不冻结，计数继续更新。
- S0 仅保存一次，作为 const Snapshot 传入各次恢复。完整受管内存/栈、CPU、原生资源描述、显式引用表、返回关系、cookie ABI 和二进制身份均保留分项及整体 SHA256，脚本独立从原始字节重算。每个目标只重建自己的真实窗口和 CET 返回链，再写入自己的原始现场。
- 资源采集仍在源 GUI 线程可响应时完成，冻结检查点之后不再同步查询控件。目标在结构检查点之前重建原生控件状态。恢复 S0 后的图像、输入、资源记录及原始目标字节位于各自 source/A/B/C 子目录。

## 验收路径

AB：源随机未保存文本和选择范围固定为 S0，源继续修改并退出。A/B 从 S0 完成正常 C++ 返回；原生 EDIT 分别编辑为 A1/B2，根窗口的正常字符输入还独立改变各自受管文本。交错操作前后同时核对未操作分支的文本、选区、撤销、焦点、引用和非时钟内存；自主计数允许继续增加。

ABC：在上述 A/B 修改后创建 C。C 仍得到 S0 的原始文本、选区、空撤销和计算结果。创建过程中 A/B 的计数继续增加、原修改保持；随后交错编辑为 A13/B24/C，并分别继续真实 GDI 绘制。

fail-B：实际让 B 的 BUTTON 属于 EDIT，运行时原生读回错误关系并拒绝资源准备；B 不发布、不执行源计算，创建对象确认清理后，A 仍能编辑和绘制，C 仍可从原 S0 恢复。该场景同时验证 A 的状态和 S0 指纹保持。

调试路由对照使用 Windows 实际报告并正确保留在 B/C 中的准备断点，将事件副本送入 A 的归属校验，必须在修改状态或 ContinueDebugEvent 前拒绝；另测试错误 TID。它不是向 Windows 伪造调试事件。所有实际事件的接收 PID、分发 PID、TID 和调试线程均记录，脚本分别核对。

## 严格边界

仍依赖单 GUI 线程、固定地址、同一构建、/Od /Ob0、已知 CALL 位置及 CRT cookie ABI。标准 EDIT 仅支持既有单行 ASCII、默认配置、已知字体、无水平滚动及向前选区；S0 的空撤销历史来自源的正常初始化及选择操作，快照器不清空撤销。

A/B/C 编辑后产生真实非空撤销，可继续运行，**没有再保存为可恢复 SA/SB**。本轮不实现 Artifact、成果导入、自动合并、完整 R4、任意第三方程序或通用窗口树恢复。没有新增鼠标/键盘 API，也没有更改历史 CET 直接恢复失败、采集限制和宿主人工未执行结论。

实际判定见 [正式报告](results/20261025-r3005c001/report.md) 及其 summary.json、controller.jsonl、分支原始数据与图像。控件/执行恢复、并存隔离、预期拒绝和完整关口分开报告。

正式采集由实验私有 capture.cpp 在目标 GUI 线程创建本地 DIB/DC，对真实窗口树请求 WM_PRINT 并刷新 GDI；桥进程通过共享内存取得像素后编码。中间跨进程 DC 请求的实际空白帧与错误记录保留在正式报告的 diagnostics 中，不算成功，不回退到旧帧或宿主取图，也不据此改写旧实验的独立采集结论。
