# R5-004a：调试停止点失败清理

只处理恢复尚未写回源 CPU、目标停在已知 checkpoint 时的失败清理。保留原 R5-004 的失败记录和 ASLR 拒绝；不实现异基址恢复。

## 实现

新实验以 `FOCUS_VIEWS_STOPPOINT_EXPERIMENT` 显式启用。旧应用 EXE、helper 和输入 DLL 逐字节复用；新 manager/client/kernel 使用一致的实验宏重新编译，不改变 Metadata 或 Child 的成员布局。正常窗口仍走既有响应式关闭流程。

停止点失败时：

1. 在创建/调试线程核对 Toolhelp 原生线程集合与事件泵登记集合完全一致。
2. 为全部线程打开自有句柄、核对 PID，并增加显式挂起计数。
3. 继续尚未处理的调试事件。显式挂起仍阻止目标执行，不恢复源 CONTEXT、不执行业务前缀。
4. 调用 `DebugActiveProcessStop`，记录 Windows 对事件句柄的释放，再 `TerminateProcess` 并确认进程和线程退出。
5. 关闭自有线程/进程句柄及 Desktop。原 R5-004 监督器独立检查所有实例、Job、句柄和 Desktop；只有实际全部清理才发布 `.done`。

失败保留原恢复错误和独立的清理错误；不延长原有 Desktop 验证期限、不重试 Snapshot、不伪造 `.done`。

## 复现

先准备 R5-003 的已验证应用构建，再运行：

```powershell
./experiments/r5-004a/build.ps1
./experiments/r5-004a/test.ps1 -ResultsDir D:/focus-views/experiments/r5-004a/results/new
```

输出目录必须不存在。脚本将强制终止提交后的 Manager 1；在 Manager 2 的真实 RA checkpoint 用私有错误映像描述触发原有内核 guard，验证清理；Manager 3 再从未修改的 Session 完整恢复 RA/RB/C。两种 ABI 各重复两次。完整 CPU/CET、Undo、GDI 像素、输入、私有文件和 Artifact 审计复用旧只读 oracle。

`--scenario checkpoint-image-reject` 保留原来的终止路径，`checkpoint-suspend-reject` 仅增加挂起但保留调试关系，`checkpoint-detach-reject` 使用新路径。这些是单次诊断对照，不是创建压力循环。`positive` 模式也会为真实停止点恢复拒绝使用新清理。

`build-minimal.ps1` 和 `minimal.cpp` 是普通启动栈上的无恢复 Win32 对照；`inspect.cpp` 只观察指定 Desktop，不清除或接管外部进程。它们不能证明执行恢复。

## 证据与边界

正式结果见 [report.md](results/r5004a/report.md)。探索阶段的成功和失败均另行保留；原生拒绝与恢复成功分别计数。新路径证明的是本实验 ABI v1/v2、同会话、固定布局、已知单 GUI 线程/窗口结构的停止点清理。Windows 内部引用计数的精确原因未由公开接口直接证明，不将机制相关性写成已定位 Windows 内核缺陷。

没有关闭 CET/CFG/GS、ASLR 或宿主保护，没有新增目标进程 VEH、全局输入或目标业务恢复逻辑。ASLR 异基址、完整 R5-004、宿主重启/断电和历史 CreateProcessW/5 可靠性门禁仍未通过。
