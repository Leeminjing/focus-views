# R4-001-GATE-002：原生创建拒绝的有界诊断

只调查 CreateProcessW 与原生调试事件，不改 Artifact、Action、Snapshot、GUI 或输入机制。当前实际结论见 [报告](report.md)：本轮自然创建拒绝未复现，非管理员启用 Kernel-Process 提供程序被拒绝，根因未定位，gateClosed 仍为 false。

## 已执行的构建和实验

在仓库根目录的普通 PowerShell 中执行；沿用本机 b001/out/build.json 中已有的 MSVC/SDK 路径。没有第三方依赖，原生程序使用 C++20、/GS、CFG、CETCOMPAT。

```powershell
New-Item -ItemType Directory -Force ./experiments/r4-001-gate-002/out | Out-Null
& ./experiments/r4-001-gate-002/build.ps1 *> ./experiments/r4-001-gate-002/out/build.log
& ./experiments/r4-001-gate-002/test.ps1 -ResultsDir D:/focus-views/experiments/r4-001-gate-002/results/<新目录> -Count 256
```

已有 b001 原始 EXE 必须存在；不在诊断中重建或更改它。新机器先按 b001 README 构建。输出目录必须不存在。脚本要求创建实验保持非管理员 token，不自行提升采集进程。

本轮固定边界：未改动的 GATE-001 基线四组各 500 次；增强诊断四组各 256 次；另一次目录作为 image 的单次已知拒绝只验证失败日志/清理。三次每次一秒的提供程序权限预检分别选择 0x470、0x10、0x70。没有失败重试，没有根据成功与否追加循环。

`probe.exe` 的四个模式是 own、fixture、own-debug、fixture-debug。每组第一次创建失败立即停止该组；计划中的其他独立对照组照常运行。existing fixture 接收故意不存在的桥接映射，目标退出 1 属正常诊断行为，不是业务恢复成功。own 的子程序立即退出 0。

```powershell
& ./experiments/r4-001-gate-002/out/probe.exe --mode fixture-debug --fixture D:/focus-views/experiments/r3-005b-001/out/r305a.exe --count 256 --log D:/新的结果目录/fixture-debug.jsonl
```

每组总期限 60 秒，每个调试等待有期限；错误时仅回收成功创建且持有自有句柄的子进程。失败 API 返回的原错误先保存，之后的查询不覆盖它；失败输出不是句柄所有权凭据。记录创建前/关闭后资源、过程/线程句柄、调试事件及 Continue、系统句柄自动关闭与自有句柄关闭，UTC 和 QPC 同时记录。

## ETW 范围

`trace.exe` 是独立采集进程，不能启动创建实验。固定描述性会话名 `focus-views-GATE-002-process-diagnostic`，不使用 NT Kernel Logger。最长 180 秒，循环文件上限 64 MiB、缓冲区上限 32×64 KiB；父进程死亡、停止文件或期限均触发停止。

- Kernel-Process GUID 22fb2cd6-0e7b-422b-a0c7-2fad1fd0e716，0x470=process/thread/image/job，由本机 logman 清单确认。
- Kernel-Memory GUID d1d93ef7-e1f2-4f45-9943-03d245fe6c00，0x20=MEMINFO。
- 自研标记只关联 attempt 序号和阶段，不能代替系统拒绝原因。

本机 StartTraceW 成功，但 Kernel-Process 的 EnableTraceEx2 返回 5；内存/标记可用，所以保留明确标注的部分采集。采集器退出 2 和诊断脚本退出 2 是这一限制的原始结果，不等于目标 CreateProcessW 再次失败。管理员采集本轮未执行，不修改用户组、provider ACL 或安全策略。

原始 ETL 可能包含无关系统事件，仅保留在忽略的 out/traces/ 中。交付其路径、大小、SHA256，以及按目标 PID/生命周期±2ms 过滤的标记/进程事件和纯数字全局内存样本。没有公开无关进程的命令行、用户栈或进程列表；全局内存不是目标专属因果证据。当前进程提供程序被拒绝，对其 process/thread/image/job 事件的解码路径未实测；实际 TDH 解码验证覆盖内存事件。

`ControlTraceW` 只停止本程序实际创建的会话。`--mode stop-owned --etl <原ETL路径>` 的紧急入口先核对固定 GUID 与文件路径；不停止不匹配的会话。主实验的正常停止与查询确认已执行，强杀/紧急停止入口未触发。

## 单独高权限采集：未执行的人工步骤

以下只准备可复现步骤，不表示本轮已执行或提升后一定能启用。测试程序始终在普通终端启动，管理员终端只运行 collector/decoder，不更改系统策略。

1. 普通终端创建新的结果目录，记下 `$PID`，保持该终端运行；约定 `raw.etl`、`ready.json`、`stop` 和 `collector.jsonl` 的新路径，原始 ETL 放 out/。
2. 在独立管理员终端仅运行以下 collector，`--parent` 填普通终端 PID：

```powershell
& D:/focus-views/experiments/r4-001-gate-002/out/trace.exe --mode record --parent <普通终端PID> --seconds 180 --etl D:/新本地目录/raw.etl --ready D:/新本地目录/ready.json --stop D:/新本地目录/stop --log D:/新本地目录/collector.jsonl
```

3. 普通终端核对 ready 的 processEnableStatus；非零就保留限制、不更改权限。在 collector 已开始时直接运行 probe.exe 的四组命令，每组至多 256 次，保留全部 JSONL。不要运行会另建同名 collector 的 test.ps1。
4. 从四份日志生成生命周期文件，三列是 PID、开始 FILETIME、结束 FILETIME：actor 用 case_start 到最后一条记录，子进程用 process_created.beginUtc 到同 sequence 的 process_exited.utc。写入约定的 `pid-lifetimes.txt`，再创建 stop 文件；collector 自身也有 180 秒硬期限。
5. collector 停止并确认会话不存在后，使用 decoder；需要高权限读取 ETL 时仍只提升 decoder，不能提升 probe：

```powershell
& D:/focus-views/experiments/r4-001-gate-002/out/trace.exe --mode decode --etl D:/新本地目录/raw.etl --ranges D:/新的结果目录/pid-lifetimes.txt --log D:/新的结果目录/related-system-events.jsonl
```

应同时核对普通 actor 的 launch_configuration.elevated=0 与 collector 的真实提升状态。即使获得进程启动事件，也不能把没有失败事件当作根因证明。

## 分析和判定

```powershell
& ./experiments/r4-001-gate-002/analyze.ps1 -ResultsDir D:/focus-views/experiments/r4-001-gate-002/results/20261025-instrumented
```

分析输出已存在时拒绝覆盖；本仓库已生成 analysis.json，复现时应针对新实验目录调用。分析分别列出历史功能回归、本轮创建结果、ETW 权限/数据与根因判定。采集或诊断完成不等于可靠性门禁通过，不开始 R4-002、SA/SB 或撤销历史恢复。
