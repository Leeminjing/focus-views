# GATE-002：最后一次有界管理员取证

2026-10-25，基于 `0f5028a4` 的原有 trace/probe 二进制，未修改创建探针或系统策略。此次只运行一个独立采集会话和一组 `fixture-debug --count 64`，无失败重试、无追加压力循环。

采集器通过 `Start-Process -Verb RunAs -WindowStyle Hidden` 单独启动，token 的 elevated=1、integrity RID=12288。普通创建探针的 elevated=0、integrity RID=8192。采集器不创建测试进程。会话硬期限 45 秒，文件上限仍为 64 MiB；普通父进程在探针完成后写 stop 文件。

StartTraceW 和 Kernel-Process（0x470）、Kernel-Memory（0x20）、关联标记的 EnableTraceEx2 均返回 0。64/64 次调试创建成功，未复现历史 CreateProcessW / 5。停止后 ControlTraceW 返回 0、丢失事件/缓冲数均为 0；查询返回 4201，确认自有会话不存在。

普通权限 decoder 成功读取 ETL。根据本次进程实例的 PID/起止时间过滤后，导出 3,289 条进程/线程/映像事件、192 条关联标记和 4 条全局内存事件，元数据及属性解码错误均为 0。全局内存与成功创建事件不提供历史拒绝的因果解释。

原始 ETL 留在忽略目录 `out/admin-final/raw.etl`（具体大小和 SHA256 见 [summary.json](summary.json)），没有上传无关系统原始事件。公共证据为 [collector.jsonl](collector.jsonl)、[fixture-debug.jsonl](fixture-debug.jsonl)、[过滤后的系统事件](related-system-events.jsonl) 和 [进程生命周期范围](pid-lifetimes.txt)。过滤包含生命周期前后各 2ms 的容差，事件路径仅保留 decoder 既有的受限字段。

三个结论分别是：功能回归仍以 GATE-001 的历史完整成功为准，此次没有重跑；创建拒绝此次未复现；根因仍未确认，`issueResolved=false`、`gateClosed=false`。本轮系统诊断结束，不再增加创建次数。后续独立开展 R4-002a 的恢复机制研究，不能据其功能结果关闭可靠性或发布门禁。

实际调用：

```powershell
# 仅此 collector 提升；parent 是保持存活的普通执行终端 PID。
trace.exe --mode record --parent <ordinary-parent-pid> --seconds 45 --etl <local>/raw.etl --ready <local>/ready.json --stop <local>/stop --log <local>/collector.jsonl
# 普通权限终端，单组 64 次，无重试。
probe.exe --mode fixture-debug --fixture D:/focus-views/experiments/r3-005b-001/out/r305a.exe --count 64 --log <local>/fixture-debug.jsonl
# collector 停止后，普通权限解码。
trace.exe --mode decode --etl <local>/raw.etl --ranges <results>/pid-lifetimes.txt --log <results>/related-system-events.jsonl
```
