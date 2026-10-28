# Recorder 的文本服务等待：受限裁剪与 COM 后继

2026-10-28，基于 PID 身份修复 126eda430。**清理未修复，R5-004 仍未通过。** 本轮不继续改变 inline/deferred 入口；实际裁剪后的正常 WM_CLOSE 和 checkpoint 清理都失败。新证据回答“被等待的工作线程还在等谁”，但没有解释它为什么不能完成。

## 实测与裁剪顺序

| 步骤 | 结果与限制 |
|---|---|
| recorder relocation-bad-offset 原路径 | 一次真实自然清理失败；仍在首次源状态写入前拒绝 RB，RA 继续绘制、固定版本不变，独立新操作完整恢复 |
| 裁剪 1：只读 S0 的原生资源描述 | 不写原始内存/栈/CPU，不处理 SA/SB、文件成果或其他 View；checkpoint 仍复现同类等待 |
| 正常关闭探针首版 | 管理器跨 Desktop 检查 HWND 所有者失败，未执行 WM_CLOSE；这是探针错误，不能计为自然清理失败 |
| 修正探针后同构建 normal/checkpoint | 同 Desktop 探针核对实际 PID/TID 后投递 WM_CLOSE；两组都出现同类等待 |
| 裁剪 2：独立 resource profile | 测量管理器不读取 Session、Snapshot、Artifact；两组仍出现相同 COM/RPCSS 等待 |
| 最终构建观测补齐后的一组串行对照 | normal/checkpoint 两组均失败；同构建、同启动周期、profile 不变，监督器均确认全部物理收尾 |

共保留 8 个自然失败及 1 个探针错误；不是 8 项恢复能力通过，也不是用重复次数证明可靠性。不同裁剪阶段和探针修正对应不同构建，不混成一个最终构建。最终两组单独见 [原始 summary](final-pair-summary.json)，测试退出非零；[只读汇总](summary.json) 中的诊断完成不代表清理成功。

只保留目前定位所需的原 recorder 二进制及资源配置；没有证明每个剩余部件都是必要条件，因此不声称全局最小复现。profile 是从既有 S0 提取的固定原生窗口资源描述，具有 EXE 身份、精确长度和 SHA256，属于诊断准备数据。测量期间没有旧进程、CPU/栈恢复、业务初始化重放或业务成果导入。profile 控制组使用新进程引导和真实窗口创建，不能当作新增快照恢复证明。

## 比上一轮新增的等待证据

最终 normal 例：目标 PID 3256、GUI TID 61320；最终 checkpoint 例：目标 PID 38520、GUI TID 67000。均有进程和线程创建时间核对。WCT COM 回调实际注册成功，分别返回：

~~~text
normal:
GUI 61320 → ThreadWait → 工作线程 2936
工作线程 2936 → Com {81d4e9c9-1d3b-41bc-9e6c-4b40bf79e35e}
             → PID 2292 / TID 0 / WctStatusPidOnlyRpcss

checkpoint:
GUI 67000 → ThreadWait → 工作线程 64428
工作线程 64428 → 同一 Com 名称
              → PID 2292 / TID 0 / WctStatusPidOnlyRpcss
~~~

这来自分别查询 GUI 和被等待工作线程的真实 GetThreadWaitChain 返回，不是用邻近符号推测 COM 对象。节点名称只称为 WCT 报告的 COM 名称，**不把 GUID 当作已经识别的 CLSID、接口或方法**。cycle=0；不能据此写成已证实死锁。WCT 结果动态变化，逐线程采样也不是原子快照。

随后只读 CIM 观测 PID 2292 的服务映射包含 RpcSs、RpcEptMapper，进程为 svchost。此旁证未固定服务器进程句柄，未获得服务端线程、RPC 方法或真实激活目标；不能把 RPCSS 的 PID 终点当作已经识别最终 COM 服务器，更不能据此解释为何阻塞。原始服务观测与 WCT 见 [服务旁证](server-observation.json)、[等待链与栈](wait-chains.json)。

GUI 栈仍落在 NtUserDestroyWindow、IMM32/MSCTF、msctfmonitor 的退出等待；工作线程仍有 COM/RPC/CoCreateInstance 及文本服务相关栈。系统模块多为导出符号，大偏移只作邻近标签。正常例没有进入清理 VEH；两组都有同类等待，因此“仅在异常回调/停止点才会阻塞”的解释不成立。不能把这一排除结论写成完整根因。

## 时间、权限与清理边界

Windows 11 x64 10.0.26200，MSVC 19.44.35229.0、SDK 10.0.18362.0，普通用户。同一启动周期自 08:51:50 开始；未重启、关闭保护、禁用 IME、改变服务或借用宿主输入。系统依赖路径、磁盘文件版本和 SHA256 的前后观测一致；它们不是完整的加载时映像身份保证。

最终 normal 的工作线程 2936 在 ready 观测中未出现，在 resource/hook prepared 之前已经存在；GetThreadTimes 创建时间为 134376421583988605，关闭前及等待时再次核对。实际创建时间、调试创建事件和清理阶段分别保留。Windows 私有文本服务初始化的精确开始/完成没有独立标记，日志明确写 initializationCompletionKnown=false；“初始化与销毁竞态”仍是待验证假设。

窗口关闭保留 5 秒判据，诊断仍最多两秒。normal 的 WM_CLOSE 是异步投递，窗口在截止时仍存在且目标存活；不虚构 DestroyWindow 返回错误。checkpoint 的 DestroyWindow 没有返回，通道 error=0 不是成功。诊断或符号失败不能掩盖首个关闭错误。第一次源状态写入前的 clean 约束没有放宽；本裁剪路径根本不写入 Snapshot 业务状态。dirty 路径及正式清理实现保持原样。

8 个自然案例的监督器均独立确认所属实例、Job、自有句柄和 Desktop 收尾；这不覆盖各自的原生清理失败。原路径还保留 RA 存活和新管理器完整恢复证据。S0/SA/SB、Artifact 数据及清单的 7 项后验哈希和 Session 发布哈希核对一致，见 summary 的 fixedStoreAudit；后验守卫不计作新恢复实验。

PID 修复后的完整旧回归已经实际执行，**整体失败**：R5-003 本体 22 场景、1,051 项本体检查通过，fixture/recorder 精确 ABI 拒绝也通过；第 1,052 项旧回归汇总失败。更深层两条旧 R3-005b/CET-002 链路的 40 个场景有 SetWindowsHookExW.thread_only / 126，控制器随后报 validate / 13，并缺少部分助手退出记录。DLL 路径在后验仍有文件，不据此确定加载时缺少哪个模块；原始错误和清理红项保留，没有重试。细节见 [身份修复报告](../../../r5-003/results/pid-identity/report.md)。

R5-003 本体与裁剪诊断前段曾并行，因此不把它们用于证明独占系统负载下的差异；最终 normal/checkpoint 在同一构建中串行执行。没有通过重启或清除系统环境选取有利结果。

没有找到有效清理改动，因此未为了追求全绿再运行完整 R5-004a/b/原崩溃组合矩阵。历史失败不覆盖，cleanupCausalFixVerified=false、fullR5004Passed=false、gateClosed=false。没有人工宿主交互、系统重启、断电或新资源兼容能力声明。

官方依据：[RegisterWaitChainCOMCallback](https://learn.microsoft.com/en-us/windows/win32/api/wct/nf-wct-registerwaitchaincomcallback)、[Using WCT](https://learn.microsoft.com/en-us/windows/win32/debug/using-wct)。本轮仅采用公开 COM 回调注册，不采用示例中的全系统线程访问或权限提升。公开接口不足以返回的服务器操作继续标记未知。

## 原始证据与复现材料

[归档索引](archives.json) 包含三份经过逐文件 SHA256 核验的归档：wait-native 保存 539 份原始案例/快照/成果/图像/日志及开发错误；wait-build 保存最终构建、符号及对应主源码共 30 份；pid-regression 保存完整旧回归 35,361 份原始文件。后者分为不超过 32 MiB 的四块，按 parts 清单连接后核对整包哈希。没有进程转储或 Windows 系统二进制。后验解包核验不计作新原生实验。

复现入口与构建说明见 [README](../../README.md)，最终构建与直接源码哈希见 [build.json](build.json)。[只读校验](transport-verification.json) 验证所有归档文件，nativeExperiments=0。原始 nativeCleanupPassed=false 和旧回归 false summary 均保留。
