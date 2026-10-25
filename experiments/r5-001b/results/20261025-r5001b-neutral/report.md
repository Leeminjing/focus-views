# R5-001b：第二协作应用的受限跨应用恢复通过

2026-10-25，在 Windows 11 x64（NT 10.0.26200.0）、普通用户权限下实际构建并执行。MSVC 19.44.35229.0、Windows SDK 10.0.18362.0，C++20，受管调用链 `/Od /Ob0`，`/GS /guard:cf /CETCOMPAT` 保持启用。未修改系统安全设置，未使用虚拟机、驱动或第三方运行时。

## 结果

| 最终矩阵 | 次数 | 证据检查 | 完整正例 | 预期拒绝 |
| --- | ---: | ---: | ---: | ---: |
| 原 fixture | 20 | 3,766 | 2 | 18 |
| 独立数据记录器 | 22 | 4,248 | 2 | 20 |
| 二进制复用、模块边界、错误客户端与完整回归汇总 | — | 14 | — | — |

**42 个场景、8,028 项本轮检查全部通过**，包括 4,726 项控制器/内核断言。另有一次错误客户端调用在创建目标前被拒绝。38 个负例是正确拒绝，不是恢复成功或新增能力数量。

本体 696 个显式进程实例、244 个 Desktop 清理确认；646 张真实 GDI Frame 核对目标绘制时间和计数像素，正例还核对保存/恢复时 EDIT 像素。原始统计见 [cross-application-statistics.json](cross-application-statistics.json)，脚本结论见 [summary.json](summary.json)、[fixture](fixture/summary.json)、[recorder](recorder/summary.json)。

真实最终画面示例：[原 fixture RA](fixture/positive-1/RA/final.png)、[记录器 RA](recorder/positive-1/RA/final.png)、[记录器 C](recorder/positive-1/C/final.png)。

最终脚本重新执行旧 R5-001a 的全部场景，以及 R4-003、R4-001、R4-002a、c001/b001/CET-002 的完整嵌套回归，全部通过，见 [旧回归汇总](r5-001a-regression/summary.json)。原源码和旧证据不变；R1/R2 与独立 R3 键鼠矩阵没有额外重跑。

## 同一个管理器、同一个内核

两款应用实际使用同一个新 `manager.exe`：

```text
SHA256 37cbbf201ffab5c841dd7cdfa4bbc7c9d3bbde806cc6f1910f20767f88202d19
```

它与 R5-001a 的旧 manager 不同：业务验收客户端从静态链接移到两个显式选择的实验 DLL。共享 `clients/host.hpp` 只有一份编排源码，两个客户端分别负责只读布局解码和独立计算断言。不是复制两套控制器或让管理器依赖第二应用的实现。

**R5-001a 的 kernel.lib 没有修改或重编译**：

```text
SHA256 e132c20d8b90faec3256a565cc6b8e0596ff9b3f303b885130c24ae92e852ffb
```

它与 d5a0730 正式研究包中的原件逐字节相同。target.lib、helper.exe、runtime304.dll 和原 fixture.exe 同样逐字节复用。两种客户端链接同一份 kernel.lib；新应用没有原 fixture 的 Work 或业务实现依赖。链接分组、复用哈希、与基线源码比较及历史包比较见 [reuse.json](fixture/reuse.json)、[链接分组](fixture/link-boundaries.json)、[内核审计](kernel-reuse-audit.json)。

## 两款应用的实际差异

| 项目 | 原 fixture | 新 recorder |
| --- | --- | --- |
| 业务结构 | 360 字节 Work，seed/文本/计数 | 480 字节 State，8 项 Sample、run ID、标签、记录计数 |
| 数据区域 | 0x1B100000000，4 KiB | 0x1B300000000，8 KiB |
| 随机标识位置 | 偏移 0 | 偏移 416；偏移 0 是描述符 |
| 计算 | XOR、加法、ROL13 | 采样加权聚合、加法、乘法、ROL7、样本值 |
| 协作帧 | outer/middle/leaf | emit/fold/sample |
| EDIT / BUTTON ID | 101 / 102 | 201 / 202 |
| 窗口与文件 | 原窗口，document.txt | 新窗口类/标题、采样行，records.csv、RECORDER/2 |

记录器是独立编写、单独编译的 EXE，只有运行时 ABI、已有目标资源库和必要 trap 汇编复用。它自己生成随机数据、创建真实窗口、绘制采样行、处理原生输入、经 BUTTON 使用 CreateFileW/WriteFile/FlushFileBuffers/CloseHandle 导出 CSV，并实际读取两份私有成果、写出自己的处理结果。

报告中的 Observation 是只读测试 DTO，不写回目标；它不是两款应用共同使用的业务内存 ABI。脚本直接读取各自原始数据页、CPU 和三个栈局部量，以两种不同公式独立计算结果。记录器另逐项核对原始 Sample 数组及其固定偏移，整个数据/栈读回只允许七个已登记 HWND 引用重关联。

## 恢复链与拒绝路径

两款应用均实际完成：随机源状态 → S0 → 源继续变化并确认退出 → A/B 在新 PID 正常 CALL 建立各自 CET 链并恢复原始现场 → 原生 EDIT 独立编辑、实际 BUTTON 生成文件并发布固定成果 → 保存带单次 Undo/文件版本的 SA/SB → 原 A/B 确认退出 → RA/RB 真正续接 → C 从原 S0 恢复并正常读取、私有处理成果 → 三者继续输入和绘制。

每个载体使用本应用的 PE/unwind 表和真实 CET_U/SSP 比较，不使用管理器函数表；初始化与业务前缀由调试器独立计数，目标均为零。三个 C++ RET 实际完成，计算依赖恢复的内存、栈和 R10。S0/SA/SB、Artifact 哈希与私有文件对象关系保持，不能用当前文本或 Frame 代替执行恢复。

双方均覆盖不支持 Undo、未关闭写句柄、工作文件版本不匹配、私有恢复写入失败、返回关系不匹配、Artifact 哈希错误、错误映像、ABI 不匹配和未知必要资源拒绝。记录器另覆盖四帧/六返回位置的**声明不兼容**，在客户端契约核对时报告 `client.execution_layout_unsupported / 50`，不发布 RB，RA/C 继续。该用例没有证明真实四帧恢复；当前执行 ABI 仍只支持既有三帧/五返回关系。

错误客户端组合（fixture-client.dll + second.exe）因目标缺少所需契约导出而在创建任何目标前拒绝，见 [原始错误](wrong-client-console.log)。没有自动回退到第一应用或冷启动结果。

## 首轮红项与通道前置条件

[首轮完整矩阵](../20261025-r5001b/report.md) 曾失败，原日志、summary 和握手前构建包保留。一个 C 的新通道尚未接管修饰键时，目标原生键盘表已含 Shift 按下位；Home 被当作 Shift+Home，最终只替换了部分文字。该结果没有改记为通过，初始位的系统来源未直接定位。

共享客户端现先通过只读 barrier 记录目标线程状态，再经现有输入入口发送左右 Ctrl/Shift 的 key-up 和 release，确认原生查询松开后才接受业务输入。两款应用使用相同流程，没有修改内核、输入 DLL、原生控件查询或期望文本。原 `C.final-edit` 断言保留。

最终 226 个输入会话中，4 个记录器会话实际再次出现初始非中立状态；全部在握手后核对松开，并正确完成后续业务输入。逐会话的 `input_session_boundary` 记录保留初始值、最终值、目标 hook 和输入序号。该机制要求检查点无持有按键/未完成输入，不等于任意交互现场已经恢复。

## 构建包、复现与边界

- [可运行 Windows x64 研究包](r5-001b-windows-x64.zip)，6,422,079 字节，包含同一 manager、两个客户端、两款应用、helper、输入 DLL、原库及 map/汇编/构建记录。
- 包 SHA256：`7bd2cf3ed797806b52b2585c80914bed73b7e6c6d1c143477b969d2cb668e428`；所有条目与实测输出逐字节核对，见 [清单](binary-package.json)。
- [源码、模块边界及复现命令](../../README.md)。本轮基线 d5a0730，工作树源码及实际二进制哈希分别记录于两份 identity.json。

只声明**同一内核可以复用于两款显式协作、同一受限执行/资源 profile 的独立应用**。继续限定单 GUI 线程、固定地址、编译期同 CALL 位置、单行 ASCII EDIT、一次全选替换 Undo、已知三窗口和已关闭同步工作文件。不是未经改造应用兼容、任意栈/控件/文件系统隔离、稳定 SDK 或跨管理器重启持久恢复。

人工宿主交互未执行；不把静止观测改记为人工验证。历史 CreateProcessW / 5 根因未定位，`gateClosed=false`。没有追加 ETW/创建压力循环，没有关闭 CET/GS/CFG，也没有将整体项目标为完成。
