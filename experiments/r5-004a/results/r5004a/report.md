# R5-004a：写入前停止点失败清理的受限验证

日期：2026-10-27。实现源码：[a01643fd8](https://github.com/Leeminjing/focus-views/commit/a01643fd8)。本报告记录本机 Windows 实际构建和执行结果。

## 结论

在当前两款协作应用、ABI v1/v2、已知窗口结构和**首次源状态写入前**的检查点范围内，清理及后续独立恢复通过受限验证。最终 `f` 为 **12 个场景、1,538 项证据核验、0 项未通过**；其中 6 组是在真实检查点拒绝后销毁窗口、结束实例，再由新 Manager 完整恢复。另 6 组是错误线程、缺失运行时映射、写回后故障的安全反例，不算恢复能力成功。

36 个管理器实例、86 个 Desktop 的最终清理均由监督器独立确认。2 个 RB 拒绝用例还确认 RA 继续原生观测和绘制。所有 12 个新发起的后续操作实际完成磁盘加载、原始执行续接、Undo、输入、GDI、私有文件与固定成果流程。固定 Snapshot/Artifact 未被修改；没有 dirty 实例进入目标侧清理回调。

**完整 R5-004 未通过，ASLR 重定位未实现。** Windows 内部为何保留旧 Desktop 引用的精确根因仍未由公开接口直接证明。本轮证明了一条可靠性更好的受控退出路径，不将其称为 Windows 内核缺陷已定位。

[汇总](summary.json) · [最终矩阵原始 summary](final-matrix-summary.json) · [逐操作及历史评估](assessment.json)

## 环境与源码身份

- Windows 11 x64，NT 10.0.26200.0，普通用户；MSVC 19.44.35229.0，Windows SDK 10.0.18362.0。
- C++20；CET、CFG、/GS 保持；未修改系统保护、ASLR、宿主输入桌面或物理鼠标。
- fixture.exe、second.exe、原有 helper.exe/runtime304.dll 逐字节复用，见 [reuse.json](reuse.json)。manager/client/kernel 与新清理 DLL/助手以一致实验宏构建。
- [build.json](build.json) 的基础 HEAD 是构建时的 `21e009d`，当时有未提交实现；**不能把它当成干净源码版本**。其中 25 项输入 SHA256 已逐项与最终实现工作字节核对，结果在 assessment 中；对应实现提交为 a01643fd8。运行包另保存这些输入的原始字节。

## 实际失败与实现选择

普通启动栈上的最小 Win32 程序，在持有断点时 TerminateProcess/ContinueDebugEvent 能确认 Desktop 消失。但真实受管 GUI 的同位置拒绝仍会保留 Desktop：原始 CPU 未写回，所有退出事件已继续、进程/Job/自有句柄已释放，CloseDesktop 成功仍不足以证明消失。

“额外挂起全部线程，再解除调试、终止”的候选先通过 a 的 4 个用例，却在 b 的 recorder-1 再现 `crash.desktop_retained/170`。b 保留 false summary；不能据 a 宣称该候选修复。只挂起而保留调试关系的对照也有失败。只读 Desktop 诊断没有枚举到顶层窗口，但枚举返回 0/错误 0，不能据此证明内部没有其他引用。

最终路径在目标仍响应时，由同 Desktop 的常驻助手安装**线程级** WH_GETMESSAGE；助手持有并核对进程与线程实例、创建时间和存活状态。目标 DLL 注册 VEH 并保留引用。到达真实 checkpoint、源状态尚未开始写入时，管理器以 DBG_EXCEPTION_NOT_HANDLED 交回原异常，Windows 在该线程当前现场进入清理回调。回调核对三个窗口的归属及父子关系，实际 DestroyWindow，核对旧身份失效，然后 ExitProcess；不修改 RIP/RSP/SSP，不回到原业务链。

依据：[VEH 调用顺序](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling)、[注册与 DLL 生命周期](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-addvectoredexceptionhandler)、[DestroyWindow 的线程归属与子窗口行为](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroywindow)。这些接口依据不替代本轮实测。

初次回调里 DestroyWindow 未返回；原停止逻辑把第一机会 C++ 异常当成已处理，反复继续故障现场。改为将第一机会 C++ 异常交给目标正常处理，未知/二次异常保留后终止。后续实际 DestroyWindow 和进程退出均确认。没有将这些异常擅自归因于某个 Windows 组件。

## 安全边界及反例

| 验证 | 实际证据 |
|---|---|
| RA 写回前拒绝 | 两款应用各两轮，原 guard 返回 1306；没有源 CONTEXT 写入、业务前缀或 View 发布；原线程窗口销毁与退出确认 |
| RB 写回前拒绝 | RA 已恢复并写入自身状态；RB 独立清理，RA 仍查询、绘制；随后新 Manager 完整恢复 |
| 错误原生线程 | 使用本实验 Manager 自己的线程制造 PID/TID 不符；原生归属检查返回 1307，安装钩子前拒绝，未操作无关线程 |
| 缺失运行时映射 | DLL 的 OpenFileMappingW 实际返回 2；仅尝试一次，原 API/错误经有界调试诊断传回，不改成安装成功 |
| 写回后故障 | 真正成功调用 SetThreadContext 后注入拒绝；dirty 相位在首次源写入前已推进，没有目标侧 abort 分发，只终止并独立监督 |
| 诊断错误与实例退出竞态 | 一次实际 QueryFullProcessImageNameW/5 被保留；下一已有观察点由同 pinned handle 确认签退，未重试查询、未增加等待；仍活动则拒绝 |

DLL 在 VEH 存在时不卸载。目标退出后卸钩返回值仍按原样记录，本轮有返回 0/错误 0；释放依据是已固定的目标实例确实退出，不将其改写为健康卸载。内部 channel ack 不能生成 `.done`；实例签退、Job 为空、自有句柄关闭、Desktop 实际消失仍由监督器分别判定。Desktop 原有等待期限没有延长。

代码审查后增加了“真实事件线程、非致命采样”“原生线程实例固定”“首次源写入前推进相位”及原始输出目录不可覆盖等保护，见 [双轴审查](review.md)。

## 回归与历史

| 记录 | 场景 / 检查 | 状态 |
|---|---:|---|
| a | 4 / 521 | 解除调试候选首轮通过，后续被 b 推翻；非最终支持证据 |
| b | 4 / 412 | 失败：Desktop 保留；回归构建未显式传工具链而中止 |
| c | 4 / 530 | 当时的新用例及**完整旧回归通过** |
| d | 10 / 1,184 | 2 项未通过：监督器快速退出映像查询竞态；原 false summary 保留 |
| e | 12 / 1,538 | 新矩阵通过；后续审查补了最后一次 Job 采样的诊断确认记录 |
| f | 12 / 1,538 | 最终源码对应新矩阵全部通过 |

完整旧回归在 c 中实际执行：R5-003 → R5-002 → R5-001b → R5-001a → R4-003，以及其 R4-001/R4-002a/c001/b001/CET-002 嵌套链。c 的 R5-003 summary 为 1,052 项、regressions=passed。后续修正仅启用在新停止点实验宏中，最终 f 重跑受影响的新矩阵；**没有把 f 的 regressions=not_run 改写成又一次完整复验**。R1/R2 底层工具未变，本轮未额外重跑。

探索目录还保留首次断点地址判断错误、旧 Session 取证时 OpenProcess.crash_identity/5、回调异常及普通栈对照等原始结果。部分探索临时构建未冻结全部二进制，因此不纳入最终可复现能力结论。原 R5-004 及其他历史报告未覆盖或改写。

最小诊断的两个正向退出对照通过；注入创建前错误 995 时仍写出原始 false summary，另一个观察进程确认该 Desktop 不存在。空/已存在输出路径拒绝，旧日志哈希不变，见 [诊断审计](diagnostic-protocol.json)。

## 真实图像

所有图片直接复制自目标产生的 PNG，未经重绘；[来源及 SHA256](frame-sources.json)。它们只作为 CPU/CET/原生资源证据的补充，不能单凭图片判定恢复。

[RA 在 RB 失败后继续绘制](fixture-RA-survived-RB.png)

| 应用 | RA | RB | C |
|---|---|---|---|
| fixture | [终态](fixture-RA-final.png) | [终态](fixture-RB-final.png) | [终态](fixture-C-final.png) |
| recorder | [终态](recorder-RA-final.png) | [终态](recorder-RB-final.png) | [终态](recorder-C-final.png) |

## 复现和原始包

从源码准备 R5-003 的已验证依赖后，执行 experiments/r5-004a/build.ps1，再执行 test.ps1，使用新的短输出路径。默认会运行旧回归；`-SkipRegression` 明确只运行当前矩阵。原生运行包保留已测试依赖与逐文件哈希。归档入口与重组命令见 [archives.md](archives.md)。每个归档逐项以流式解包核对原文件长度/SHA256；大包拆为 32 MiB 部分，重组哈希另行验证，不需要重复展开一整棵证据目录。

## 尚未覆盖

只适用于当前显式协作、安全点、同会话、固定布局、ABI v1/v2、已知 EDIT/BUTTON 和无业务退出副作用的窗口过程。DestroyWindow 会进入窗口过程，本轮不推导任意第三方窗口过程可安全清理。

写回后现场不受目标侧清理支持；本轮对应强制终止也完成了物理清理，但不据此承诺该路径普遍可靠。旧失败 Session 的未确认归属不会被追溯伪造完成。本轮未处理 ASLR 代码/引用重定位、其他 GUI 资源、持有输入的快照、宿主重启或断电。人工宿主交互未执行；历史 CreateProcessW/5 可靠性门禁仍独立保留。没有进入 R5-005。
