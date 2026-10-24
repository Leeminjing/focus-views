# R1：受控 x64 执行片段的跨进程续接

已在 Windows 11 x64 实际编译和运行。结果见 [实验报告](results/20261024-r1/report.md) 和 [原始汇总](results/20261024-r1/summary.json)。仅覆盖本实验片段，不代表普通 Windows 应用恢复能力。

## 构建与运行

在安装了 x64 MSVC、MASM 和 Windows SDK 的环境中执行：

```powershell
.\experiments\r1\build.ps1
.\experiments\r1\test.ps1
```

构建脚本会使用开发者终端环境或 vswhere。非标准工具链安装可显式指定：

```powershell
.\experiments\r1\build.ps1 -MsvcRoot '<MSVC版本目录，包含bin/include/lib>' -SdkRoot '<Windows Kits\10目录>'
```

构建产物为 `out/controller.exe` 和 `out/fixture.exe`，静态链接 C++ 运行库，不需要第三方运行依赖。脚本只在当前进程内配置编译环境，不安装工具、不修改系统策略。`-OutDir` 可覆盖产物目录；测试脚本同样接受 `-OutDir`，以及 `-ResultsDir`。

单独执行正向实验：

```powershell
.\experiments\r1\out\controller.exe --fixture "$PWD\experiments\r1\out\fixture.exe" --case positive --timeout-ms 10000
```

## 测试语义

| case | 控制器退出码 | 预期含义 |
| --- | --- | --- |
| positive | 0 | 源终止后，同一 S0 在两个新进程中分别续接成功 |
| omit-memory | 2 | 不写回数据页，计算结果不匹配 |
| omit-stack | 2 | 不写回受管栈，计算结果不匹配 |
| omit-register | 2 | R15 置零，计算结果不匹配 |
| omit-flags | 2 | 清除进位标志，计算结果不匹配 |
| address-conflict | 3 | 故意占用目标数据地址，精确分配被拒绝 |
| build-mismatch | 3 | 故意修改快照构建身份，写回之前拒绝 |
| timeout | 4 | 宿主在准备点之前挂起，超时并清理 |
| unexpected-exception | 1 | 宿主在准备点之前抛出异常，报告并清理 |

`test.ps1` 检查退出码、原始日志标记、源退出与目标创建顺序、任务事件次数，以及每个创建的 PID 是否都有终止确认。负向项在汇总中标记 `computationSucceeded=false`；`harnessPassed=true` 只表示它按预期失败或拒绝。

## 实验边界

- 单个受管执行线程，同机、同架构、同一 fixture 二进制及相同受管虚拟地址。
- 保存的是代码页、数据页、私有受管栈的原始字节和 `CONTEXT_CONTROL | CONTEXT_INTEGER`；S0 仅保留在控制器内存。
- `fixture.cpp` 只提供元信息和准备点；任务前半段由汇编正常执行，恢复由控制器完成。
- 检查点是一个单字节 `INT3`；控制器核对异常地址和 RIP，续接地址为该断点后一字节。
- 续接后的片段只用整数指令和本地受管内存，在结果断点结束。没有调用、返回、系统 API、运行库或其他线程依赖。
- 不恢复普通堆、系统启动栈、TEB/PEB、TLS、SIMD/XSTATE、影子栈、文件句柄或任何图形资源；不证明跨重启持久性、多线程一致性或未改造应用兼容性。

调试接口停止线程后读取上下文，再通过新线程的上下文直接续接；没有调用任务入口来恢复。相关平台依据：[调试事件循环](https://learn.microsoft.com/en-us/windows/win32/debug/writing-the-debugger-s-main-loop)、[SetThreadContext](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadcontext)、[精确地址分配](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualallocex)、[进程缓解策略](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-process_mitigation_policy)。
