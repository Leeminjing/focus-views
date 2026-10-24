# R3-005a：真实 GUI 执行现场恢复实验

**本机已编译并实际运行；恢复未通过。** 原始内存、普通 C++ 栈字节与基本 CPU 上下文可以读回一致，已知窗口资源和三个活跃 HWND 引用可以重建/重关联，但新进程在检查点后的第一个 RET 触发 CET 影子栈错误。没有发布可用 View，没有恢复后的业务画面或输入成功记录。

[实际报告与原始记录](results/20261024-r305a/report.md) 分别说明各层结果。不要把 `evidenceChecksPassed=true` 误读为实验通过；本机 `experimentPassed=false`，程序与测试脚本保留失败状态 2。

## 运行

```powershell
.\experiments\r3-005a\build.ps1 -MsvcRoot '<MSVC 目录>' -SdkRoot '<Windows Kits\10>' -SdkVersion '<SDK 版本>'

# 最小 RET 失败复现：真实 GUI，但无输入钩子、无 PrintWindow
.\experiments\r3-005a\test.ps1 -Scenario ret-probe -ResultsDir '<全新目录>'

# 源 GUI 操作与图像、恢复、四项拒绝，以及旧 R3 完整回归
.\experiments\r3-005a\test.ps1 -ResultsDir '<另一个全新目录>'
```

默认完整脚本还使用原有 `experiments/r3-004/out`，并运行其嵌套 R3-003/R3-002 回归；可用 `-R304OutDir` 指定。R1/R2 没有修改，也未在本轮重跑。无需管理员、虚拟机、驱动或第三方运行库。CET、/GS 和系统保护保持开启，不提供关闭保护的补救开关。

本轮保留了一次 R3-004 回归的拖拽子窗口图像检查失败；最新重跑通过，但未修复该偶发采集问题。两次记录分别保留，详情见报告。

单独运行 EXE：`out/r305a.exe --output <新目录> --scenario ret-probe`。`minimal` 含源输入与三个真实图像，不运行四项负向；`all` 增加拒绝路径；`ret-probe` 去掉输入桥和图像采集后仍执行真正的跨进程上下文续接。

## 本次实际边界

- 单受管 GUI 线程、一个固定类/几何的自绘 GDI 顶层窗口、固定地址数据页、运行时拥有的已提交栈。普通 Win32 消息循环与多层 C++ 函数在此栈运行，函数保留正常 CALL/RET、/GS 检查和原生 API 调用；不是 R1 的无调用计算片段。
- 引导仅做固定地址分配、日志/IPC 和窗口创建，源实例随后进入业务初始化。新承载进程只运行引导，不调用业务入口；恢复器写回原始字节与执行位置，不重放任务或键鼠操作。
- 安全点位于受管输入已结束、输入模块正常解除、消息已排空、没有活动捕获/绘制/窗口过程时。源保存后继续执行同一函数返回链，随后真实 WM_CHAR 修改文本并更新计数，再确认进程退出。
- 窗口用逻辑身份 1 和实际原生描述记录；仅允许已知类、样式、矩形、所属 PID/TID 和 GWLP_USERDATA。数据页内一个、C++ 栈上两个活跃 HWND 槽显式登记，恢复时逐项验证旧值和地址范围并映射到新窗口。没有整数扫描或复用旧 HWND。
- 接入未修改的 R3-004 输入模块与 R3-001 PrintWindow 辅助进程。自绘窗口本身处理 WM_CHAR、左键和捕获。未新增输入 API，未加入原生 EDIT。

栈引导是明确的实验 ABI：小段汇编切换 RSP，设置当前 NT_TIB 的 StackBase/StackLimit，随后执行普通 C++。未复制 TEB/PEB。GetCurrentThreadStackLimits 的下界仍报告本进程系统原分配栈，原始栈/TLS/异常跨引导边界、自动扩栈及完整 XSTATE 均未恢复。未将这一路径称为任意普通启动栈恢复。

MSVC cookie 及 complement 被作为已知 CRT ABI 字段捕获/写回，未关闭 /GS；它们的跨进程返回有效性因更早的 CET 失败尚未证明。CONTEXT 恢复覆盖控制/整数/SSE；CET_U 通过官方 XSTATE 接口只读采集。没有写影子栈页、修改其权限或将源 SSP 数值冒充新进程有效影子栈。

## 证据与失败判定

`S0-data.bin`、`S0-active-stack.bin`、`S0-context.bin` 与目标续接前的相应文件允许独立比对，唯一允许的字节差异是声明的 HWND 槽。控制器用 Windows unwind 元数据解析真实 leaf/middle/outer 栈帧；脚本另从原始栈和寄存器重算预期 receipt。

`controller.jsonl` 保留调试事件、源退出确认、原生窗口描述、引用映射、普通上下文写回、CET_U/SSP 读取和异常参数。四项拒绝测试实际触发构建比较失败、CreateWindowExW 缺失类、引用范围检查及缺少 THREAD_SET_CONTEXT 权限的 Windows API 失败。失败分支不发布。

CET 阻断时，故意遗漏栈/寄存器/cookie 的后继测试标记为未执行：首个 RET 会掩盖它们，不能拿同一个故障冒充三种检验都通过。目标准备窗口的真实图像仅证明窗口创建和原生绘制，明确不是恢复后的业务 Frame。

资源均由本实验创建并由 Job/RAII 管理。调试事件按 PID/TID 核对、仅由创建线程接收；本实验一次只保留一个被调试 GUI 进程。异常捕获后保留原始错误，清理自己拥有的进程与 Desktop；不会切换或借用宿主输入桌面。人工宿主交互未执行，旧 R3-004 宿主不确定结论保持不变。
