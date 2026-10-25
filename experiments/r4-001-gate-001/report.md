# R4-001-GATE-001：回归修复与原始失败

**2026-10-25，已构建并实际运行。最终功能回归矩阵全部通过，但创建拒绝诊断仍红，因此整个 gate 尚未闭环。** [最终功能汇总](results/20261025-gate001-owner-draw/summary.json) 返回成功；随后更小的纯创建诊断再次复现真实错误 5。两者分别记录在 [分层 assessment](assessment.json)，不以某次回归通过宣称根因已修复。SA/SB 与完整 R4 均未实现，宿主人工测试未执行。

## 已确认的两类测试问题

1. 原 PID 全局唯一匹配在真实历史 ABC-1 中失败：76 次创建、76 次退出，PID 32644 退出后复用。新审计按顺序维护活跃实例，历史轨迹通过；重叠、缺失退出、重复退出、逆序时间、Desktop 泄漏或错误关闭的反例均拒绝。见 [生命周期检查](lifetime-tests.json)。
2. 原脚本在创建失败后继续索引 S0/目标记录，导致越界中止。现在各场景保存主要 API 失败与依赖证据错误，继续其他场景，最终生成 false 的 summary 并返回非零。故意以目录作为 image 的真实 CreateProcessW 返回 5 用例验证这一点；没有模拟 API 返回或修改 ACL。

原始历史记录在 [R4-001 常驻桥结果](../r4-001/results/20261025-r4001-resident/report.md)，本轮没有改写。

## 调试句柄的独立错误

旧 b001 和 CET-002 手动关闭 CREATE_PROCESS/CREATE_THREAD 调试事件句柄。公开接口要求这些 process/thread 句柄由系统在继续相应 EXIT 事件时关闭；hFile 仍由调试器关闭。见 [WaitForDebugEvent 官方说明](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-waitfordebugevent)。

[最小错误对照](handle-early-close.jsonl) 在提前关闭后分配 Event，核对它复用了同一槽；3 个 Event 都在 ContinueDebugEvent 后变成 ERROR_INVALID_HANDLE。正确所有权的 [原始对照](handle-correct-owner.jsonl) 中 3 个 Event 均存活。修复保留系统句柄，不关闭 CET、/GS、CFG，不修改影子栈或系统 DLL。

该错误已得到直接机制证据，但它不是所有 CreateProcessW/5 的充分解释：修正后 c001 仍曾失败。

## 创建拒绝的实际复现

- [第一轮矩阵](results/20261025-gate001/summary.json)：独立 b001/CET-002 完成，c001 的 fail-B-2 在 C 收尾 post 助手创建时返回 5；R4 嵌套也复现 post 创建失败，更深的 CET-002 又在新承载进程创建时失败。
- 三个旧控制器现均记录完整 image/command/Desktop/flags/时间/调用者 SID 与完整性、句柄及 GUI 数量、文件/桌面访问探测。新增诊断先保留原错误，之后调用不会覆盖它。
- [常驻桥矩阵](results/20261025-gate001-resident/summary.json)：c001 全量与 R4 本体/完整嵌套均通过，但更早独立 b001 的 omit-focus-1 新承载进程仍被拒绝；这份 gate 汇总保持 false。
- 已实际查询的 [Windows 原生日志与文件 ACL](results/20261025-gate001-resident/system-after.json) 没有找到可归因事件；查询无事件与权限/日志不可用保留原错误文本，不推导安全软件原因。

常驻 courier 是有序 IPC 与采集进程复用，移除了逐命令创建，不是失败重试。所有 GUI 仍由原统一事件泵管理；首次创建、命令、目标退出和清理失败继续拒绝。诊断记录证明减少创建次数不能被当成错误 5 已被根除。

## 按钮绘制红项

[确认矩阵](results/20261025-gate001-confirmation/summary.json) 的 b001 positive-1 报告 EDIT 区域像素 100% 相同，但 BUTTON caption ink=0，原断言正确失败。已直接查看 [source-S0](results/20261025-gate001-confirmation/b001-standalone/positive-1/source-S0.png) 和 [restored-S0](results/20261025-gate001-confirmation/b001-standalone/positive-1/restored-S0.png)：恢复后的按钮标题确实空白。

这与执行续接及原生控件文本查询是不同层的结果，不能删除按钮像素断言或用 GetWindowText 冒充画面成功。b001 后续复用 c001 的目标 GUI 线程原生 WM_PRINT/DIB 采集：fixture 仅增加只负责采集的运行时消息，桥只编码返回像素；未更改控件业务逻辑或虚构内容。原像素断言保留。

最终两次独立 positive 的 EDIT 区域均 12,648 / 12,648 相同，BUTTON caption ink 均为 81。见 [positive-1 像素核对](results/20261025-gate001-owner-draw/b001-standalone/positive-1/native-control-pixels.json)、[positive-2](results/20261025-gate001-owner-draw/b001-standalone/positive-2/native-control-pixels.json) 和已直接查看的 [恢复画面](results/20261025-gate001-owner-draw/b001-standalone/positive-1/restored-S0.png)。旧空白帧仍保留。

## 最终功能矩阵

各套件均使用本轮最终构建，源身份已按实际文件重新核对。构建、loadconfig、源码及 EXE/DLL 哈希分别位于各子套件。

| 套件 | 实际场景与检查 | 结果 |
| --- | --- | --- |
| b001 独立 | 18 次 / 280 项，并含 CET-002 14 次 / 285 项 | 通过 |
| c001 全量 | 6 次 / 670 项，再实际执行 b001 与 CET-002 全量 | 通过 |
| R4 本体及完整回归 | 本体 8 次 / 2,860 项（含回归汇总），其 c001 6 次 / 670 项、b001 18 次 / 280 项、CET-002 14 次 / 285 项 | 全部通过 |
| 创建拒绝协议反例 | 9 个场景中第一项故意触发实际 API / 5，余项继续，完整失败 summary 保留 | 子脚本退出 1，summary=false；拒绝/汇总/清理检查符合预期，不算恢复成功 |
| gate 功能脚本 | 10 项分层汇总检查，全部通过 | 退出 0 |

见 [各次执行命令与退出码](results/20261025-gate001-owner-draw/summary.json)、[源码/二进制独立核对](results/20261025-gate001-owner-draw/source-binary-verification.json)。包含上述故障协议反例共 125 个场景，原始流中 704 个创建实例/704 个退出、271 个 Desktop 关闭；[独立顺序审计](results/20261025-gate001-owner-draw/cleanup-verification.json) 全部配对。R4 自身仍保留按实例身份的原断言。

旧“目录完全不变”检查改为精确变更清单，保存于每个 baseline-changes.json；仅本次审核的修复源文件获准变化，历史结果和其他实现仍须一致。原 [R4-001 历史报告](../r4-001/results/20261025-r4001-resident/report.md) 与证据不改写，R4 原生实现也未修改。

## 尚未解决：纯原生创建也复现错误 5

`launch-probe.cpp` 不使用 Snapshot、SetThreadContext、输入钩子、GDI 或业务计算。它在单个后台 Desktop 上逐次创建/等待/关闭；调试模式只使用正常调试事件泵、正确句柄所有权。目标为 b001 时传入故意缺失映射的 bridge 参数，因此目标退出 1 是正常诊断结果；它不运行 fixture 业务。

最终同一构建四组各计划 500 次，前三组均完成：最小 EXE 普通创建、现有 EXE 普通创建、最小 EXE 调试创建。第四组“现有 EXE + DEBUG_ONLY_THIS_PROCESS”在第 **55** 次 CreateProcessW 返回 **ERROR_ACCESS_DENIED (5)**，立即停止，先前 54 个进程均确认退出，Desktop 随后关闭。没有再试第 55 次。

见 [实际失败行及清理](results/20261025-launch-probes/fixture-debug.jsonl)、[四组诊断 summary=false](results/20261025-launch-probes/summary.json) 与 [代码/EXE 哈希](results/20261025-launch-probes/identity.json)。该 summary 是从已保留的实际日志生成，assessedExistingLogs=true，未把过程退出码缺失填成 0。

随后实际运行了可复现包装脚本 test-launch-probes.ps1，使用相同二进制另建新结果目录；[四组共 2,000 次创建](results/20261025-launch-script/summary.json) 全部完成，脚本退出 0，实际退出码与源码/二进制身份均记录。这是另一次诊断，不重试先前被拒绝的那次启动；它不改变前一 summary=false，也不能证明偶发拒绝已经修复。

拒绝现场的只读探测：EXE 读取和 GENERIC_EXECUTE 均成功，Desktop 打开/DACL 读取成功；调用者未提升，完整性 RID=8192，childProcessPolicy=0；当前 Job 查询成功、ActiveProcessLimit=0、UIRestrictions=0、活动进程 3；调用者句柄 108、GDI 对象 0，约 2.16 GB 可用物理内存。这些数字只描述观测点，不证明所有系统约束均已排除。

围绕实际拒绝时间再次查询 [原生日志及文件 ACL](results/20261025-launch-probes/system-after-rejection.json)，仍未取得可归因记录。原错误/查询状态完整保留，没有将无日志解释成没有系统保护介入。

**结论是阻塞点已缩到不需要恢复机制的原生创建链路；不是已确定 Windows、安全产品或某项权限的根因。** 本轮公开用户态探测没有给出错误 5 的具体拒绝来源，不关闭保护、不加入重试，也不为此改写失败。后续应围绕该最小程序取得创建拒绝的系统层诊断；在此之前不开始 R4-002/SA/SB。功能矩阵全绿与这个未解释的原生拒绝必须同时披露。

## 保持的边界

运行环境是 Windows 11 x64（NT 10.0.26200）、非管理员、原 MSVC 19.44 / SDK 10.0.18362 工具链，具体 build.json、identity.json 和实际 loadconfig 在每个子套件中。未修改 common/native.hpp 或 R1/R2 实现，因此没有宣称重跑它们。

Artifact、控件种类、输入 API、撤销历史均未扩展。原失败套件保持失败；一次新矩阵通过也不证明所有运行环境中 CreateProcess 永远成功。没有自动重试、切换宿主桌面、关闭安全保护或静默冷启动。
