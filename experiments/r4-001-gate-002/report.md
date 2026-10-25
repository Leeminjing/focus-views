# R4-001-GATE-002：有界诊断完成，原生创建根因未定位

**2026-10-25，代码已编译、已运行。** 本轮正式旧基线 2,000 次与增强诊断 1,024 次创建均成功，没有再次复现自然发生的 CreateProcessW / 5。Kernel-Process 的 ETW 启用请求被当前非管理员 token 拒绝，实际采集仅覆盖内存事件与关联标记。**根因未确认、问题未解决、gateClosed=false**，不进入 R4-002 或 SA/SB。

| 必须分开的结论 | 本次结果 |
| --- | --- |
| 既有功能回归 | c55efb0 的 b001/c001/CET-002/R4 完整成功记录保持；原实现与原始证据未修改，本轮未重跑 |
| 原生创建拒绝再次复现 | 未复现；两套固定矩阵 3,024 次自然创建全部成功，不能覆盖历史第 55 次拒绝 |
| 拒绝根因得到直接证据 | 没有；确认的是 EnableTraceEx2 的访问边界，不是 CreateProcessW 的拒绝来源 |

## 身份与范围

基线提交 c55efb0ce95867a17636ac78dac7bcd5976e6c6d；Windows NT 10.0.26200 / AMD64，MSVC 19.44.35229.0、SDK 10.0.18362.0，C++20 /O2 /MT /GS /guard:cf /CETCOMPAT。创建者及本轮 collector 均未提升，RID=8192。见 [构建记录](results/20261025-instrumented/build.json)、[实际输出](results/20261025-instrumented/build.log)、[源码与 EXE SHA256](results/20261025-instrumented/identity.json)、[当前 token/工具/文件权限](results/20261025-inventory/inventory.json) 和两份实际 loadconfig。

原 b001 EXE 在基线前后均为 SHA256 `a830f36770c3591aaa0bac1a257f3bc31475509c054dda107d712af490ffe8f0`，见 [独立比较](results/20261025-instrumented/fixture-identity-comparison.json)。目标为该 EXE 时，仅传入故意缺失映射的 bridge 参数；它退出 1 是预期诊断结果，不执行 fixture 业务。own 子程序立即返回 0。

本轮没有 Snapshot、线程上下文恢复、输入注入、控件绘制或业务计算。调试事件可能包含加载器/运行库创建的线程，这些线程仍按真实事件记录，不能称为应用业务重放。

## 固定实验与原始结果

没有“直到成功为止”的循环。原程序基线四组各 500 次，随后增强版四组各 256 次；另做各 3 次的两组开发冒烟与一次已知无效 image 的拒绝路径校验，不把它们混入自然复现次数。每组第一次失败立即终止该组，不重试该次启动。正式增强版每组另有 60 秒期限。

| 对照 | 原程序基线 | 增强诊断 |
| --- | --- | --- |
| 最小 EXE，普通创建 | 500 / 500 | 256 / 256 |
| 现有 EXE，普通创建 | 500 / 500 | 256 / 256 |
| 最小 EXE，调试创建 | 500 / 500 | 256 / 256 |
| 现有 EXE，调试创建 | 500 / 500 | 256 / 256 |

见 [旧程序本轮独立输出](results/20261025-baseline/summary.json)、[增强版完整 summary](results/20261025-instrumented/summary.json) 与 [一致性分析](results/20261025-instrumented/analysis.json)。增强版诊断脚本最终退出 **2**，原因是进程提供程序不可用；四个 actor 均退出 0，collector 退出 2，decoder 退出 0。**不能把 collector 的错误 5 或脚本退出 2 写成目标创建再次失败，也不能把证据检查通过写成 gate 通过。**

历史故障只读核对见 [历史回放](historical-replay.json)，原 [第 55 次失败](../r4-001-gate-001/results/20261025-launch-probes/fixture-debug.jsonl) 与 [后续 2,000 次成功](../r4-001-gate-001/results/20261025-launch-script/summary.json) 原样保留。回放不算本轮自然复现。

## 生命周期与资源观测

每次创建前、成功子进程退出并关闭自有句柄后采样句柄数、private bytes、working set、可用物理内存及 commit；记录真实查询失败码。创建调用前后分别记录精确 UTC，所有日志另有 QPC。每个调试事件记录 PID/TID、事件类别、异常、Continue 状态，以及系统拥有的线程/进程句柄自动关闭结果。

| 模式，各 256 次 | 自有 process-info 句柄关闭 | 系统调试线程 / 进程句柄关闭 | 创建者首个 before → 最后 after 句柄数 |
| --- | --- | --- | --- |
| own | 512 | 0 / 0 | 121 → 123 |
| fixture | 512 | 0 / 0 | 121 → 123 |
| own-debug | 512 | 1,024 / 256 | 121 → 124 |
| fixture-debug | 512 | 1,024 / 256 | 121 → 124 |

增强矩阵合计 1,024 个子进程全部确认退出，2,048 个自有句柄关闭成功；调试场景的 2,048 个线程句柄与 512 个进程句柄在相应 EXIT 继续后查询为 ERROR_INVALID_HANDLE (6)。CreateProcess 返回的自有 process-info 句柄仍有效，身份核对后再关闭。四个 Desktop 关闭成功。

上述首尾增量不应擅自解释成已知缓存，也不构成所有资源无泄漏的证明；本轮没有出现句柄身份失配、重复自有关闭或随 256 次迭代线性增加的模式。原始 [own](results/20261025-instrumented/own.jsonl)、[fixture](results/20261025-instrumented/fixture.jsonl)、[own-debug](results/20261025-instrumented/own-debug.jsonl)、[fixture-debug](results/20261025-instrumented/fixture-debug.jsonl) 及 [资源对比](results/20261025-instrumented/resource-comparison.json) 可复核。

[拒绝路径反例](results/20261025-controls/forced-refusal.jsonl) 只将 image 指向一个真实目录：第一次 CreateProcessW 实际返回 5，立即保存原错误、前后资源、Desktop/Job/策略状态；输出 PID 和句柄均为 0，没有发布或重试子进程，Desktop 已关闭，原生程序退出 2。[反例判定](results/20261025-controls/assessment.json) 明确 naturalRecurrence=false。这不能解释历史可执行文件为何被拒绝。

## ETW 提供程序、权限及实际采集

本机 logman 确认 Kernel-Process 和 Kernel-Memory 可查询；独立的 Kernel-Object 名称未找到，不据此推断不存在其他对象跟踪途径。完整命令与返回码见 [inventory](results/20261025-inventory/inventory.json)。当前 token 属于 Performance Log Users，但没有列出 SeSystemProfilePrivilege。**成员资格和缺少某项 privilege 都不等于已经证明拒绝原因。**

使用一个固定描述性名称的独立会话，不占用 NT Kernel Logger；配置循环文件 64 MiB、最多 32×64 KiB 缓冲、180 秒上限。实际正式采集约 23 秒即主动停止。实现依据是 [StartTraceW](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-starttracew)、[EnableTraceEx2](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-enabletraceex2) 和 [会话控制权限说明](https://learn.microsoft.com/en-us/windows/win32/etw/controlling-event-tracing-sessions)，没有修改 provider ACL、用户组或系统策略。

| 实际调用 | 原始状态 |
| --- | --- |
| StartTraceW | 0 |
| Kernel-Process，0x470=process/thread/image/job | EnableTraceEx2 返回 5 |
| Kernel-Process，仅 0x10=process | 返回 5 |
| Kernel-Process，0x70=process/thread/image | 返回 5 |
| Kernel-Memory，0x20=MEMINFO | 0 |
| 自研关联标记 | 0 |
| ControlTraceW STOP | 0 |
| 停止后 QUERY | 4201，确认会话不存在 |

0x10/0x70 是单变量权限预检，各仅采集一秒，结果在 [权限目录](results/20261025-permissions/identity.json)；正式组合原始调用见 [trace-controller.jsonl](results/20261025-instrumented/trace-controller.jsonl)。这些都是采集 API 结果，不是 CreateProcess 错误。

实际 ETL 13,107,200 字节，EventsLost=0、LogBuffersLost=0。默认 ProcessTrace 时间转换为系统时间，按 actor/子进程生命周期±2ms 过滤，仅导出受管标记、相关进程字段和纯数字全局内存；不公开无关进程命令行/用户栈。时间语义依据 [EVENT_HEADER](https://learn.microsoft.com/en-us/windows/win32/api/evntcons/ns-evntcons-event_header)。

[实际事件导出](results/20261025-instrumented/related-system-events.jsonl) 包含 **3,072 个标记、44 个 Kernel-Memory 样本、0 个 Kernel-Process 事件**，TDH metadata/property 错误均为 0，decoder 返回成功。进程/线程/映像/Job 的解码分支因提供程序被拒绝而**未实测**。内存是全局背景数据，标记由测试程序产生，二者都不能替代拒绝来源证据。

原 ETL 留在本机被忽略的 out/traces/ 中，路径、SHA256、大小见 [ETL 清单](results/20261025-instrumented/etl-manifest.json)。未将潜在无关系统数据上传到公共仓库。collector 的截止时间、父进程死亡检测、正常停止及停止后查询都在源码中；本轮仅正常停止被触发，强杀/紧急停止入口未触发。

## 结论与仍缺少的证据

三条预先声明的假设中，本轮未找到句柄生命周期失配；文件内容哈希保持一致，未出现可供比较的新自然拒绝；进程 ETW 流在当前非管理员权限下不可获得。因此**没有新证据确认 CreateProcess 拒绝来自 Windows Defender、某项 ACL、Job、资源压力或调试子系统**，也没有依据更改原生执行域架构。

未进行管理员采集，未触发 UAC，未改变测试程序 token、安全策略或创建方式。独立高权限 collector 的人工步骤已在 [README](README.md) 准备；它不能从高权限进程启动 probe，且提升后能否取得所需事件仍待验证。

本轮诊断按预定边界结束，不追加循环追求再次失败或“全绿”。保留未解决的可靠性门禁，将历史功能回归通过与系统创建边界分开判定；R4-002、可恢复 SA/SB 和完整 R4 均未开始/完成。
