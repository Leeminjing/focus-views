# R1-001 实验报告

日期：2026-10-24（Asia/Shanghai）。对应 FV-02/FV-09、CAP-01/CAP-02/CAP-19 的受限子集。

**结论：在记录的系统、构建和执行约束下，已验证受管计算片段的内存、栈和所需线程上下文可以在源进程退出后，在新进程中恢复并续接。** 未验证通用 Windows 应用快照恢复，未推进 R2–R4。

## 环境与构建

Windows 11 家庭版 x64，版本 10.0.26200；Intel Core i9-13900HX；普通用户、非管理员。MSVC 19.44.35229、MASM x64；Windows SDK 10.0.18362.0。C++20，`/O2 /MT /W4 /WX`，链接 `/DYNAMICBASE /NXCOMPAT /CETCOMPAT`。没有驱动、虚拟机或第三方运行依赖，没有修改系统安全配置。

工具链使用机器上已有的 MSVC 和 SDK，通过显式路径传入，未导入其他项目源码或模块。源文件、可执行文件的 SHA256、工具链绝对路径及时间见 [summary.json](summary.json)。构建身份还包括 fixture 源文件标识及完整 fixture.exe 的 SHA256；恢复时验证二者。

[build.log](build.log) 记录实际构建输出；[dependencies.txt](dependencies.txt) 显示 fixture 依赖 KERNEL32，控制器依赖 KERNEL32 和 bcrypt；[workload-disassembly.txt](workload-disassembly.txt) 可核对实际汇编，没有调用或返回指令。

源和目标进程查询的 `ProcessUserShadowStackPolicy` 原始值均为 `0x105`，动态代码策略为 `0x0`。旧版 SDK 尚无影子栈策略枚举名，代码按官方枚举 ABI 的索引 15 查询一个 DWORD 并原样记录；未调用策略设置 API。此结果不能外推为已恢复影子栈。

## 实际链路

控制器启动 fixture，识别 ntdll 启动断点后继续到精确的准备断点。随后创建受管区域并启动任务，独立记录任务入口断点和检查点。检查点后的执行不依赖源进程的系统结构或运行库。

| 区域 | 地址 | 字节数 | 权限 |
| --- | --- | --- | --- |
| 代码 | `0x1b000000000` | 4096 | execute/read |
| 私有数据 | `0x1b000010000` | 4096 | read/write |
| 私有执行栈 | `0x1b000020000` | 65536 | read/write |

数据与栈由任务根据运行时随机种子产生，没有应用保存文件。R15 的中间值在检查点之前没有写入数据页或栈。快照读取上述原始字节及整数/控制上下文，保留在控制器内存。后半段的结果为：

```text
ROL64((memory + R15 + CF) XOR stack_local, 9)
```

控制器根据源启动前生成的随机种子独立计算预期值，并在捕获时核对三类贡献及进位标志。它不按变量含义填充恢复进程。恢复器只写回原始区域和上下文。

本次正向记录：源 PID **6620** 在目标创建前收到退出调试事件，控制器继续该事件后确认进程句柄已终止。同一 S0 先后恢复到 PID **32772** 和 **40556**。恢复地址为 `RIP=0x1b00000004b`、`RSP=0x1b00002ff70`，直接执行检查点后半段。

两个新进程均得到 **`0x1f27ccda1746da14`**，与预期一致。源任务初始化事件 1 次、源检查点 1 次；每次恢复均为初始化 0 次、检查点 0 次、结果事件 1 次。事件证据来自控制器的调试事件记录，不依赖目标内存里的初始化计数。见 [positive.log](positive.log)。

## 反向与拒绝测试

| 原始日志 | 实际退出码 | 实际行为 |
| --- | --- | --- |
| [omit-memory.log](omit-memory.log) | 2 | 数据页保持新分配的零值，结果不匹配 |
| [omit-stack.log](omit-stack.log) | 2 | 受管栈保持零值，结果不匹配 |
| [omit-register.log](omit-register.log) | 2 | 将恢复的 R15 置零，结果不匹配 |
| [omit-flags.log](omit-flags.log) | 2 | 将恢复的 CF 清零，结果不匹配 |
| [address-conflict.log](address-conflict.log) | 3 | 精确分配失败：VirtualAllocEx，487 / `0x1e7` |
| [build-mismatch.log](build-mismatch.log) | 3 | 注入快照身份不匹配；写回之前拒绝，1306 / `0x51a` |
| [timeout.log](timeout.log) | 4 | 准备点前等待超时：1460 / `0x5b4`，随后确认子进程终止 |
| [unexpected-exception.log](unexpected-exception.log) | 1 | 准备点前注入异常 `0xe0421001`，报告并确认子进程终止 |

这 8 项是预期失败或拒绝的计算，不计为恢复成功。测试脚本的 9 项检查均符合预期；所有创建的子进程均有对应终止确认。套件完成后按本实验 fixture.exe 的精确路径查询，残留进程为 0。

## 已证明与未覆盖

已证明：受管原始数据和栈在源进程死亡后仍可恢复；所需 R15、R12、RIP、RSP 和 CF 的写回与实际计算一致；同一内存快照可重复恢复；初始化未重放；选定的失败路径有错误和清理记录。

仅保存 `CONTEXT_CONTROL | CONTEXT_INTEGER`；系统可规范化部分标志位，实验只对实际依赖的 CF 作语义断言，不声明整个 EFLAGS 位级相等。栈是预分配的私有执行区域，不是复制普通线程的启动栈。没有调用或返回，因此不检验 C++ 调用栈展开、SEH 或影子栈恢复。

普通堆、对象图和别名、TLS、SIMD/浮点/XSTATE、多线程、未完成 I/O、文件/配置/句柄、图形输入、快照磁盘格式、跨构建或跨重启恢复均未覆盖。超时和异常注入发生在准备点前；它们证明相应路径清理，不代替所有恢复中途故障的验证。

复现命令和各 case 语义见 [实验说明](../../README.md)。本报告关联的源码和二进制身份以 summary.json 为准；以后重新构建产生不同二进制时，应生成新结果目录。
