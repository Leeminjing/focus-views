# R5-004a：调试停止点失败清理

只处理恢复尚未写回源 CPU、目标停在已知 checkpoint 时的失败清理。保留原 R5-004 的失败记录和 ASLR 拒绝；不实现异基址恢复。

## 实现

新实验以 `FOCUS_VIEWS_STOPPOINT_EXPERIMENT` 显式启用。旧应用 EXE、helper 和输入 DLL 逐字节复用；新 manager/client/kernel 使用一致的实验宏重新编译，不改变 Metadata 或 Child 的成员布局。正常窗口仍走既有响应式关闭流程。

停止点失败时：

1. 在目标 GUI 仍响应时，由同 Desktop 常驻助手安装仅针对该线程的 WH_GETMESSAGE。运行时 DLL 注册 VEH 并持有自己的引用；目标 EXE 无改动。真正开始源状态写入前先推进实例相位，此后不允许此回调。
2. 真正到达 checkpoint 后，若首次源内存/栈/cookie/CPU 写入尚未开始就发生拒绝，核对 PID/TID、创建时间和检查点地址，只给该清理通道提交 abort。
3. 在原创建/调试线程以 DBG_EXCEPTION_NOT_HANDLED 继续该断点。Windows 在原线程、当前未改写的上下文中调用 VEH；清理代码不修改 RIP/RSP/SSP。
4. VEH 核对三个已知窗口的线程/父子归属，原生 DestroyWindow 后检查旧身份失效，再 ExitProcess。它不返回到应用计算。清理期间第一机会 C++ 异常仍传给目标正常处理，未知/二次异常则保留证据并终止，不反复吞掉异常。
5. 持有目标实例句柄的助手确认目标退出后结束钩子生命周期；Windows 关闭调试事件句柄，管理器释放自己的句柄和 Desktop。监督器沿用原 R5-004 的物理校验，独立检查实例、Job、全部自有句柄和 Desktop；只有物理清理完成才发布 `.done`。

卸钩返回值与“目标实例已退出、回调不再可执行”分别记录。不能将已退出目标上的 UnhookWindowsHookEx 返回 0 写成 API 成功。DLL 在 VEH 注册期间不卸载，最终随目标进程退出释放。

失败保留原恢复错误和独立的清理错误；不延长原有 Desktop 验证期限、不重试 Snapshot、不伪造 `.done`。

## 复现

先准备 R5-003 的已验证应用构建，再运行：

```powershell
./experiments/r5-004a/build.ps1
./experiments/r5-004a/test.ps1 -ResultsDir D:/focus-views/experiments/r5-004a/results/n
```

输出目录必须不存在。脚本将强制终止提交后的 Manager 1；在 Manager 2 的真实 RA checkpoint 用私有错误映像描述触发原有内核 guard，验证清理；Manager 3 再从未修改的 Session 完整恢复 RA/RB/C。两种 ABI 各重复两次。完整 CPU/CET、Undo、GDI 像素、输入、私有文件和 Artifact 审计复用旧只读 oracle。

`--scenario checkpoint-image-reject` 保留原来的终止路径，`checkpoint-suspend-reject` 仅增加挂起但保留调试关系，`checkpoint-detach-reject` 是解除调试对照（第二轮仍失败），`checkpoint-abort-reject` 使用目标线程原生清理路径。这些是单次诊断对照，不是创建压力循环。`positive` 模式也会为真实停止点恢复拒绝使用新清理。

`build-minimal.ps1` 和 `minimal.cpp` 是普通启动栈上的无恢复 Win32 对照；`inspect.cpp` 只观察指定 Desktop，不清除或接管外部进程。它们不能证明执行恢复。

## 证据与边界

正式结果见 [report.md](results/r5004a/report.md)。探索阶段的成功和失败均另行保留；原生拒绝与恢复成功分别计数。新路径证明的是本实验 ABI v1/v2、同会话、固定布局、已知单 GUI 线程/窗口结构的停止点清理。Windows 内部引用计数的精确原因未由公开接口直接证明，不将机制相关性写成已定位 Windows 内核缺陷。

没有关闭 CET/CFG/GS、ASLR 或宿主保护，没有全局输入、目标业务恢复逻辑或目标 EXE 重编译；只新增有界的目标线程清理 DLL/助手。ASLR 异基址、完整 R5-004、宿主重启/断电和历史 CreateProcessW/5 可靠性门禁仍未通过。

最终 f 的 12 场景/1,538 项核验通过。完整旧回归在 c 中通过，f 未再次执行旧全量。晚退出映像诊断只在已有观察点确认同 pinned 实例签退，不重试查询或延长清理等待。原始包和源字节校验见报告。
