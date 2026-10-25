# R4-001-GATE-001：回归故障定位与修复

本轮修正旧 b001/c001 的生命周期核对，补齐失败汇总与原生启动诊断，不增加 Artifact、输入或 Snapshot 能力。历史失败记录保持原样。

## 构建和执行

使用原有 C++20 / MSVC x64 / Windows SDK 工具链。下例路径是本机实际工具链；其他机器应传入自己的安装路径。CET、/GS、CFG 保持开启，无管理员权限或系统设置修改要求。

```powershell
$compilerRoot = 'D:\focus-distinct-browser\.tools\msvc\VC\Tools\MSVC\14.44.35207'
$sdkRoot = 'D:\Windows Kits\10'
foreach ($experiment in @('r3-005a-cet-002','r3-005b-001','r3-005c-001','r4-001')) {
    New-Item -ItemType Directory -Force "./experiments/$experiment/out" | Out-Null
    & "./experiments/$experiment/build.ps1" -MsvcRoot $compilerRoot -SdkRoot $sdkRoot -SdkVersion 10.0.18362.0 *> "./experiments/$experiment/out/build.log"
}
& ./experiments/r4-001-gate-001/build.ps1
& ./experiments/r4-001-gate-001/test.ps1 -ResultsDir D:/focus-views/experiments/r4-001-gate-001/results/<新的目录名>
```

probe 构建脚本使用 b001/out/build.json 中的工具链配置。构建日志重定向前需确保对应 out 目录存在。完整测试按顺序执行：生命周期合成反例与历史轨迹、调试句柄正反对照、实际创建拒绝的失败汇总、b001 独立全量（含 CET-002）、c001 全量（含 b001/CET-002），最后 R4 本体及全部嵌套回归。结果目录必须不存在，禁止覆盖旧证据。

## 修改内容与判定

- `lifetimes.ps1` 按事件时序维护当前活跃 PID；前一实例退出后可再次使用同 PID。重叠、重复或缺失退出、未释放 Desktop 仍拒绝。每次运行写入 `lifetime-audit.json`。
- 旧目录整体不变断言改成**精确文件清单**：只有本轮审核的 CET-002/b001/c001 修复文件允许变化；所有历史结果及其余旧实现仍须不变。`baseline-changes.json` 保留实际差异与允许清单。没有删除控件、CET、内存或像素断言。
- b001 和 CET-002 不再手动关闭调试 CREATE 事件中的 process/thread 句柄；系统在对应 EXIT 的 ContinueDebugEvent 时管理它们。文件句柄及 CreateProcess 自有句柄仍按各自所有权释放。c001 原有正确事件泵保持。
- 三个旧实验每次创建记录 EXE、参数、Desktop、标志、UTC 时间、调用 PID/TID、token SID/完整性/提升状态、句柄与 GUI 资源计数、EXE 读/执行权限、Desktop DACL、子进程策略、Job 状态和可用物理内存。失败立即保存原 Win32 错误，之后诊断不覆盖该错误；不重试、不修改权限。
- 控制和取图复用每个 View 的常驻 courier，顺序请求、单在途操作和五秒期限；退出前确认桥进程死亡。原消息、原生绘制、CPU/控件恢复逻辑和断言继续验证。创建、命令或清理失败仍拒绝，不启动备用桥重试。
- b001 的按钮标题空白反例保留后，私有 fixture 增加与 c001 一样的只负责取图的运行时消息：目标 GUI 线程创建 DIB/DC、调用原生 WM_PRINT/PRF_CHILDREN、GdiFlush，再传输真实像素给桥编码。原 EDIT/BUTTON 业务过程不变，像素断言保留。这是协作式采集，不是通用未改造窗口支持。
- 每个场景的依赖证据校验单独捕获错误，保存原 API 失败并继续其他场景，最后仍输出失败 summary。缺少 S0 不再遮蔽原创建失败。

`-GateDeniedLaunch` 是 b001 的显式反例选项，仅在第一轮 positive 用目录作为 image 路径，令真实 CreateProcessW 拒绝；它不修改 ACL，也不模拟返回值。该子套件必须返回非零并保持 false；总 gate 只检查它是否正确保留错误/汇总/清理，不能把它算成恢复成功。普通场景均使用原 EXE。

`handle-probe.exe --early-close` 有意重现旧句柄所有权误用，并检查被复用的 Event 是否遭系统关闭；正常模式保留系统句柄、要求 Event 存活。两者都只操作本实验创建的进程和对象。此反例确认句柄误关机制，**不能单独证明历史 CreateProcessW/5 的唯一根因**。

`launch-probe.exe`、`launch-probe.exe --debug` 以及增加首个参数为现有 `r305a.exe` 路径的两种调用，分别执行 500 次纯创建/退出。现有 EXE 在该诊断中仅收到故意缺少映射的 bridge 参数，退出 1 是预期行为，不算业务或恢复通过。每次创建失败立即停止、不重试，诊断不调用输入或 Snapshot 路径。`--debug` 仅用正常调试事件 API，不修改目标内存。

用 `./experiments/r4-001-gate-001/test-launch-probes.ps1 -ResultsDir <新的目录>` 执行全部四组；每组第一次创建失败即停止该组，继续其他诊断组，finally 保存身份和完整 summary，任一组失败则脚本退出非零。`-AssessExisting` 只分析已有四份原始日志；它不补造未记录的进程退出码。

SA/SB、EDIT 撤销历史与完整 R4 不在本轮范围。各次失败与最终判定分别保留，不能用某次通过改写已复现的偶发创建拒绝。

源码变更、各轮失败与最终结果见 [实际报告](report.md)。
