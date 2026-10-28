# R5-004c：候选清理入口未解决自然阻塞

报告日期：2026-10-28。**已实际实现、构建并执行；整体未通过。** deferred 在退出 VEH 后仍自然阻塞于窗口销毁，追加 inline 及最终串行对照也失败。不能把早期全绿或移出回调本身当作修复。正式 R5-004a 清理实现与 R5-004b 执行/重定位内核没有修改，候选保留在独立实验目录。

## 最终结论分层

| 项目 | 实际结果 |
|---|---|
| 同一最终 DLL 的三/四帧 inline/deferred 聚焦对照 | 4 组通过：阶段、GUI 线程身份、续接 RSP 和硬件 SSP 均核对 |
| R5-004a 独立完整清理断言矩阵 | 12 场景、1,537 项检查通过；包含 RA/RB 拒绝、RA 继续、错误线程、缺失映射和 dirty 拒绝 |
| R5-004b 异基址组合矩阵 | 16 场景、1,958 项检查，3 项原生清理确认失败；其余 1,955 项通过 |
| 原 R5-004 崩溃矩阵 | 63 场景、3,174 项检查通过 |
| R5-003 与完整旧嵌套回归 | R5-003 前体 22 场景、1,052 项检查中 1 项失败；其下 R5-002/R5-001b/R5-001a/R4-003 及两条旧链完整执行并通过，不能因此说整套通过 |
| 自然失败后的追加 inline | 同构建四帧案例失败，清理未完成；不是换构建后的对照 |
| 所有套件结束后的最终串行对照 | inline、deferred 各运行一次，各自仍出现自然清理超时 |
| 人工同步机制探针 | 普通工作线程、处理 C++ 异常、增删 VEH 共 6 个小例，两种模式均正常完成，没有支持预设的模式差异 |
| 人工真实阻塞的诊断反例 | 原始清理用例保持失败；有界采集到 84 帧，独立物理清理、RA 存活及新操作恢复有证据；不算自然故障修复 |

原始 [组合 summary](native-combination-summary.json)、[清理](cleanup-summary.json)、[异基址矩阵](relocation-summary.json)、[崩溃](crash-summary.json)、[旧回归](legacy-summary.json) 均保持原值。汇总 [summary.json](summary.json) 的 cleanupCausalFixVerified、fullR5004Passed、gateClosed 都是 false。

## 对照设计与环境

Windows 11 x64 10.0.26200，MSVC 19.44.35229.0、SDK 10.0.18362.0，普通用户；保持 GS、CFG、CET、ASLR、NX。全程未重启系统、修改安全设置、禁用 IME 或借用宿主键鼠流。观察到的 LastBootUpTime 始终为 2026-10-28 08:51:50 +08:00。

最终二进制固定在 out-final2；manager、controller、kernel、两个应用和既有输入/取图组件与 R5-004b 逐字节相同。manager SHA256 为 7c4d871fc6520747f36d7580424c21edebefeb470371a50b879173f1b5da0ee6，kernel 为 10cbf2d22d7666100396265a25aded38d67eadc35a4d5a93498b2d8cc91724db。只新增实验客户端、清理 DLL/助手与诊断助手。最终 abort-runtime.dll SHA256 为 2c08402c034e69bed1108f49639e59dd3360f600e6e8e9fafc508cb2b320ca28。完整身份与逐项复核见 [build.json](build.json)、[final-build-audit.json](final-build-audit.json)。

inline 和 deferred 由同一 DLL 的通道参数选择。前者仍在 VEH 调用栈内调用清理函数，后者由 Windows 异常续接到固定、不返回的清理函数。观测、所有权验证和模块查询在两组相同；这套带观测的 inline 控制并非声称与历史旧 DLL 字节相同。另有最初使用原 R5-004b 正式构建和旧诊断构建的自然对照，两者当时都通过，保留在 development 归档。

开发轮 v1 在 D:/fv/c/v1 完成全部套件并全绿；源码原字节单独固定在 source-v1。代码复核随后将 DestroyWindow 的失败错误码采集移动到阶段日志之前，同时补齐诊断失败时的原错误记录，再在新目录构建最终 v2。没有用 v1 成功替代最终构建复验。

v2 的本体矩阵与 v1 旧回归曾并行；进入自身旧回归前，使用 PID+创建时间固定的旧测试进程等待屏障，确保不同时重编译旧输出。最终新增的两次串行控制在两套测试都结束后运行，排除了“仅在两套测试并行时失败”的解释。没有据此推断系统全局负载或文本服务状态已被完全控制。

新证据目录 C:/fv-c/v2 避免 D 盘容量不足；源应用和依赖仍从同一 D 盘构建包启动，普通文件在各会话自己的 NTFS 私有工作区内创建。旧 D 盘证据保留；对已完成证据的 NTFS 无损压缩不改变字节。系统模块记录的是八个已知模块的路径/基址/查询结果，并非所有已加载模块的加载时内容哈希，不扩张为任意模块兼容证明。

## 自然反例：已经离开 VEH，仍在等待

最终异基址矩阵的三个失败均为 recorder：

- relocation-bad-offset；
- relocation-missing-reference；
- relocation-wrong-image。

它们仍在源 Snapshot 状态首次写入之前准确拒绝 RB，未发布 RB，RA 继续观察/绘制，S0/SA/SB/Artifact 不变。原生清理未确认的检查原样失败；独立监督器后来确认物理资源清理，随后显式的新管理器操作完成恢复，不能反向把原清理改记成功。

代表例的目标 PID 48572、GUI TID 27948，目标线程创建时间与采样记录一致。阶段为 1→2→3→4→5→6，没有 DestroyWindow 返回的 7 和进程退出前的 8。VCH 的 RSP 与原异常上下文一致，清理入口硬件 SSP 与断点 R11 都为 373801611168。native 判据是 processExited=false、exit=259、verified=false。

**error=0 不是 DestroyWindow 返回成功**：该 API 尚未返回，通道未收到它的失败错误码；控制器在既有 5 秒期限后固定清理失败。不能将其描述为该 API 返回 ERROR_TIMEOUT，也不能认定这是永久死锁。

只读采样显示 GUI 栈：

~~~text
NtWaitForSingleObject
WaitForSingleObjectEx
msctfmonitor!UninitLocalMsCtfMonitor + 0x3b
MSCTF / IMM32 teardown
win32u!NtUserDestroyWindow
abort-runtime!cleanup_entry
recorder::sample / fold / emit / record / application
~~~

没有处于 VEH 的异常分发调用帧，配合实际 VCH/清理入口阶段确认该候选已移出回调。WCT 成功返回 GUI TID 27948 → ThreadWait → 同进程 TID 41868；后者采样在 COM/RPC、CoCreateInstance、MSCTF 初始化和 msctfmonitor 工作线程相关栈。WCT 的 cycle=0，不能写成已证明的锁循环。逐线程采样及 WCT 发生于同一有界时段，不是全进程原子快照。

这里确认的是具体等待边及同类阻塞位置；RPC 目标、服务端状态和为何未完成仍未确定。符号类型明确记录：自研 DLL 有本地 PDB，系统 DLL 多为导出符号；大偏移处可能只是邻近导出标签，不能把这些名称当作准确私有函数。

追加 inline 以及最终串行 inline/deferred 都重现相同类型的超时。累计公开 6 个自然失败记录，其中 4 个 deferred、2 个 inline，详见 [natural-counterexamples.json](natural-counterexamples.json) 和 natural/ 下原始 JSONL。普通事件/异常/VEH 注册小例没有失败，并未被包装成证实根因的反例。

## 安全边界与诊断实现

源仍响应时固定目标进程、GUI 线程及创建时间，在所属 Desktop 安装线程级钩子；独立通道 magic/长度/版本防止混用旧助手。仅干净、未发布 checkpoint 可以交回原异常；实际 RSP/R11 必须匹配停止时记录且满足 x64 入口对齐。

VEH 不做窗口销毁或等待：只校验、写固定观测槽、请求续接。VCH 只观测续接上下文。cleanup_entry 由同一 GUI 线程执行，正常分配自己的 C++ 栈帧；阶段 5 的 RSP 比断点 RSP 小是该函数的普通栈帧，不是改写原异常 RSP。硬件 SSP 的关系单独验证；inline 有正常异常处理 CALL 帧，不要求其入口 SSP 等于原断点 SSP。没有写影子栈、源 SSP 或不可信 Snapshot CPU，也没有用该退出路径代替业务 RET 恢复。

首次源状态写入前仍由原内核推进 dirty 标记；之后禁止目标侧清理分发。独立 R5-004a 的所有断言正文保留，仅换用已有磁盘版本 2 oracle；不是删除检查让它绿色。未改正式清理 DLL、旧失败记录或重定位支持范围。

超时先记录失败，再用同 Job 的自有诊断进程采样，最多 1,500ms 采集、500ms 签退。核对 PID/创建时间，枚举并限制自有进程线程，暂停后取上下文/栈再恢复；从不停止其他进程的线程。原生采样 API 错误、符号类型、等待链错误及诊断被回收均记录。诊断无论成功或失败，都继续原有终止流程，不能把超时变成成功。

人工阻塞反例通过正常原生窗口过程等待自有事件，是真正的 DestroyWindow 阻塞，但明确标记 synthetic，不证明历史 TSF 根因。[原始失败](synthetic-original-failure-summary.json) 仍为 false；[诊断 assessment](synthetic-diagnostic-assessment.json) 的 true 仅表示观测和失败隔离成立，内部策略声明不计作新的原生能力检查。WCT 在该人工例中未于助手期限内完成，已明确回收；采集到的上下文/84 帧和错误日志保留，没有上传转储。

文档依据是 [VEH 回调约束](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nc-winnt-pvectored_exception_handler) 与 [VCH 生命周期](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-addvectoredcontinuehandler)。[微软示例](https://learn.microsoft.com/en-us/windows/win32/debug/using-a-vectored-exception-handler) 特别提示样例不适用于 64 位 Windows；本轮 x64/CET 结论来自实际阶段和硬件观测，不能从该样例外推。[WCT](https://learn.microsoft.com/en-us/windows/win32/api/wct/nf-wct-getthreadwaitchain) 返回信息的局限也不能当作完整系统因果图。

## 旧回归的另一项红项

最终 R5-003 的 fixture-abi 期望在没有创建 View 前得到 snapshot.runtime_abi，实际先得到 snapshot.previous_lifetime_alive / 170。manager2 记录 PID 47436，Session 中旧 manager PID 也是 47436、创建时间 134376337814723907；该旧 manager 的正常退出流程已完成，测试随后通过新的 CreateProcess 实例运行 manager2。

旧 restart.hpp 的 gone() 已按 PID+创建时间检查，但 restore_phase 仍额外用裸 PID 与当前 PID 比较，数字相同便拒绝。这个附加条件至少无法容纳合法 PID 复用，阻止本次精确 ABI 拒绝验收。原始回归及断言没有修改，未重新运行挑选有利 PID；不把安全拒绝称为 ABI 用例通过。对应材料见 legacy-final 归档、[实例观测](legacy-pid-observation.json) 及 [原始控制器日志](legacy-pid-controller.jsonl)。

该问题独立于窗口销毁。后续修复需保留“活着的原实例必须拒绝”，并按实例身份验证复用；不能删除精准错误检查掩盖它。

## 构建、证据和未覆盖范围

构建/聚焦/组合/小例命令见 [README](../../README.md)。最终组合入口显式包含独立清理套件，不再以原崩溃矩阵及 R5-003 嵌套链替代它。前一套 v1 全绿、最终 v2 的 3 个清理红项与 1 个旧回归红项、开发期探针的 Job 过早观测失败均分别保留。

共享测试入口改动后，另用已有 R1 二进制实际重跑 9 个用例，均符合原有正向及拒绝预期，见 [R1 summary](r1/summary.json)。该轮没有重新编译 R1，也没有重新执行 R2；负向用例的恢复失败没有被改记为恢复成功。

归档及逐文件 SHA256 清单见 archives.json：native-final 为最终四个本体套件；legacy-final 为最终完整旧链；earlier-combination 为 v1 全套；controls-final 为最终探针、人工阻塞和自然追加/串行控制；development 为较早对照与 v1 原源码；tested-build 为最终可执行包与对应主源码。大文件分块有连接哈希。制作时流式解包逐文件对照原字节，后验核验不计作新 Windows 实验。

只验证当前 Windows 会话和既有三/四协作帧、已知 GUI/单次 Undo/关闭文件资源。没有通用 GUI、持有键态、未知线程资源、宿主重启、断电或稳定协议声明。人工宿主操作未执行，历史 CreateProcessW/5 门禁独立保留。下一项仍应围绕已观测的文本服务初始化/退出资源与等待对象取证，不进入 R5-005，不继续增加恢复压力次数，也不把禁用 IME 当作原场景的修复。
