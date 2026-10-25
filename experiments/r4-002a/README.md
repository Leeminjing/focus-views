# R4-002a：单次标准 EDIT Undo 与可恢复 SA/SB

仅支持单 GUI 线程、固定顶层/标准单行 EDIT/BUTTON、ASCII 文本、编译期协作检查点和固定地址布局。不是任意应用或控件兼容层。保留 CET、/GS、CFG；不调用宿主全局输入、不修改 Windows 私有控件内存。

`undo.cpp` 是实验私有资源观察器/重建器，链接在受管进程内。在原生 EDIT 窗口过程前后只读查询实际状态，并把原消息交给原生窗口过程。只接受从空 Undo 状态开始、完整选中文本后的一次可打印 ASCII `WM_CHAR` 替换。前像、后像、选区、修改标志、实际消息和 GUI 线程组成资源记录。不能从 EM_CANUNDO 布尔值推测未知历史。

目标线程在资源准备阶段用 WM_SETTEXT 建立前像、EM_SETSEL 建立原选择，再用 EM_REPLACESEL(TRUE) 重建原生撤销对象。随后读回当前状态；这是已声明的控件资源重建，不重放应用初始化或业务计算。首个/第二个 EM_UNDO 及后续键盘输入另行实测。

`undo-probe.cpp` 在独立验证控件比较原生 WM_CHAR 与资源重建后的 Undo/再次 Undo/继续输入，并核对被采集源控件没有变化。此最小测试本身不证明跨进程执行恢复；控制器的正式正例才验证后者。

控制器先创建一次 S0，源继续变化并退出，然后恢复 A/B 并通过既有受管键盘入口各替换一个字符。根窗口的普通 WM_CHAR 另使受管应用内存产生实际变化。输入完成并关闭后，在仍响应的 GUI 线程采集资源；随后循环在同一组原 CALL 位置进入新的协作检查点，保存真正的 SA/SB 内存、栈、寄存器和资源记录。原 A/B 被确认退出后，新 PID 各自建立合法 CET 链、重建资源，再恢复对应原始现场并正常 RET。终态检查点前的新计算在原 A/B 上执行一次；恢复载体跳过这些业务前缀，调试器独立计数。

本轮只新增实验私有文件，复用旧输入模块/统一事件泵/原生 GUI 线程取图路径的既有边界，未改公共 SDK 或旧实验。输入通道在冻结前释放并关闭；拒绝快照后保留源进程运行和另一分支的输入，不扩展旧输入模块的同 PID 重新安装能力。

## 构建与运行

```powershell
& ./experiments/r4-002a/build.ps1 -MsvcRoot <MSVC-root> -SdkRoot <Windows-SDK-root> -SdkVersion <version> *> ./experiments/r4-002a/out/build.log
& ./experiments/r4-002a/test.ps1 -ResultsDir D:/new-results -Repeats 2
# 单项最小原生机制：
& ./experiments/r4-002a/out/r305a.exe --role undo-probe --log D:/new-probe.jsonl
# 单项完整链路，输出目录必须不存在：
& ./experiments/r4-002a/out/r305a.exe --output D:/new-case --scenario positive
```

正式脚本默认执行十个场景各两轮，并完整调用 c001 → b001 → CET-002 回归。每项错误保留原日志，先生成失败汇总，再跳过依赖证据。`-SkipRegression` 必须被报告为未执行，不能当作完整回归通过。

场景为 `positive`、`multiple`、`IME`、`unknown-edit`、`no-undo`、`wrong-undo`、`stack`、`register`、`memory`、`bad-chain`。IME 场景只验证合成 WM_IME_STARTCOMPOSITION 触发拒绝，不代表实际 IME 组合态兼容。未知编辑用真实 EM_REPLACESEL 走未支持的输入路径验证拒绝。多次输入即使被 Windows 合并 Undo，也拒绝推测历史。重建遗漏 Undo/错误字符必须在资源阶段拒绝；遗漏计算状态分别到达正常 RET 后由真实计算错误拒绝。错误返回链在原始状态写回前拒绝。

原始快照为每个场景的 `S0-*`、`SA-*`、`SB-*`，包含原始数据页、完整/活跃栈、CONTEXT、资源图、元数据、返回链、cookie ABI 和二进制身份。每份有独立 SHA256，测试脚本从文件重新计算，不以控制器的布尔结论代替证据。公开快照是本实验生成的随机数据，不包含外部应用文档。

尚不支持部分选择替换、连续输入/撤销合并、Undo 后再次保存、IME、RichEdit、未知控件及任意应用；人工宿主交互未执行。GATE-002 的创建可靠性门禁独立保留，完整 R4 尚未验收。实际结果见 [正式报告](results/20261025-r4002a/report.md)。
