# R5-004b：受限异基址执行续接实测

报告日期：2026-10-28。结论：**同构建、不同实际 EXE 基址下的三/四帧协作式续接通过本轮有限验收；总体可靠性门禁仍未关闭。** 本文包含真实 Windows 构建与运行，不是仅核对历史日志。

## 分开判定

| 项目 | 实际结果与证据 |
|---|---|
| 最终异基址矩阵 m4 | 16 场景、1,958 项主检查全部通过；[原始 summary](final-summary.json) |
| 最终构建的原 R5-004 崩溃矩阵 g2 | 63 场景、3,174 项检查通过；8 个提交后恢复、32 个提交前拒绝、6 个恢复中断场景；[summary](crash-final-summary.json) |
| 默认旧构建与完整嵌套回归 | 开发轮 g1 实际完整执行并通过，顶层 3,175 项检查；[summary](complete-regressions-summary.json)。包含 R5-003 → R5-002 → R5-001b → R5-001a → R4-003 及其两条旧嵌套链。不是 m4/g2 再次执行的结果 |
| 同目录首轮 m3 | 16 场景中 6 个因停止点原生清理未确认失败；1,958 项主检查中 6 项失败；[false summary](controlled-failed-summary.json) 保留 |
| 原 R5-004a 独立回归 a1 | 12 场景、1,537 项检查中 10 项失败，涉及 6 个清理场景；[false summary](cleanup-regression-failed-summary.json) 保留 |
| 清理入口研究 | 观察到 DestroyWindow 内等待同进程 TSF/COM 工作线程；尚未证明完整根因或可靠修复。新入口一次通过后，当前启动周期的原入口也通过，不能归因于修改 |
| 总体 R5-004 / 发布可靠性 | **不宣布整体关闭**：历史清理回归仍未有因果修复；原 CreateProcessW/5 门禁也不变 |

m4 的 16 个场景为：两款应用各一个直接正例、一个未重编译旧 ABI 同基址兼容例、四个主动拒绝及其后独立恢复、两个真实强杀恢复管理器后发起新恢复的例子。拒绝准确不等于该错误现场恢复成功。g2 不包含 m3/a1 暴露的全部停止点清理反例，不能用 g2 全绿覆盖它们。

## 环境与源码身份

Windows 11 x64 10.0.26200；普通用户；MSVC 19.44.35229.0，工具目录 14.44.35207；Windows SDK 10.0.18362.0。编译与链接保留 `/GS /guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT`，协作应用仍为 `/Od /Ob0`。没有调整系统安全设置、关闭 ASLR/CET、写影子栈或写入源 SSP。

构建基线为 `e7313d7b18ed108d6b9a416a9f971268e341efa1` 加本提交的工作树；不能把基线 SHA 单独当成已编译源码身份。最终 `out-final/build.json` 的 buildId 为 `d4125fb4c3df50ffeed288c9a47184d46ddf60a0e4e3bf6d0cb65c9c0aed431e`。已逐项核对记录的主源码与二进制 SHA256，原字节位于 tested-build 归档；复用的自研输入/资源组件也有单独哈希。工具链本身不随归档分发。

最终三帧 EXE SHA256：`11926520bd4b49aff0c091c92144388a1fd20f280e41b299f69167389ecfe6ab`；四帧 EXE：`abcd481567349636501f06929fbe64afd140c4152c785f1a5be6a396f82abe56`。m4 使用同目录的逐字节副本，工作目录与 DLL 搜索目录保持相同；副本并非重新链接或改 Metadata。由实际 CREATE_PROCESS_DEBUG_EVENT 获取基址，未按有利布局重试失败操作。若系统实际给出同基址，异基址断言将失败。

## 真实地址与原始执行

| 应用 | 保存时基址 | 恢复时基址 | 每分支真实返回关系 | 额外登记映像引用 |
|---|---|---|---|---|
| fixture / ABI v1 | `0x7ff67d7e0000` | `0x7ff6fc160000` | 5，三 C++ 帧 | 4 |
| recorder / ABI v2 | `0x7ff6700c0000` | `0x7ff6f5590000` | 6，四 C++ 帧 | 4 |

以上是 m4 两个直接正例，RA/RB/C 分别核对，完整事件见 [address-examples.json](address-examples.json)。源进程和保存管理器先确认退出，再由新管理器从磁盘 S0/SA/SB 恢复；新目标正常 CALL 建立自己的 CET 返回链，恢复原始局部量/R10/计算数据及登记 R8 引用，真正 RET 返回。源初始化和业务前缀没有重放。

同时验证原生 EDIT 的一次全选 ASCII 替换与 Undo、七处已登记 HWND 重关联、独立私有工作文件、C 从原 S0 读取固定 Artifact 并处理，以及三个真实 GUI 继续输入与绘制。工作文件对象、固定版本与各分支均分别核对。示例 [fixture RA](frames/fixture-RA.png)、[recorder RA](frames/recorder-RA.png)；另外四张终帧及逐文件来源见 [frames/provenance.json](frames/provenance.json)，原始输入/绘制/控件日志在归档中。

## 机制与首次写入边界

原 Metadata v1/v2 布局不变；新应用显式导出独立 `R5004ImageReferences`，版本 2、408 字节，有限容量不是自动发现能力。四种实际依赖为栈中 Metadata 指针、栈中间接调用指针、数据页中的代码指针、R8 中代码地址。R8 在检查点汇编中与当前代码地址实际比较，不是未使用的观测字段。旧 EXE 不增加此导出，仍保留同基址兼容路径。

运行时先核对 EXE SHA256、目标自身 PE、RVA、ABI 和固定数据/栈布局，再在 Snapshot 私有副本准备变换。真实 PE unwind 确定实际返回槽；保存的返回向量与槽原值均需吻合。RIP、有限执行描述、cookie 的位置、登记引用和返回地址按映像身份/RVA 重关联，cookie 内容仍使用保存值。目标的原生 CALL 普通返回与影子返回逐项比较通过后才允许写回。没有扫描所有整数猜测指针，没有只移 RIP 后继续使用旧返回地址。

首次源内存、栈、cookie 或 CPU 写入之前，仍推进原 dirty 标记；写入后绝不进入目标侧清理异常。新代码在独立宏 `FOCUS_VIEWS_IMAGE_RELOCATION_EXPERIMENT` 下生效，磁盘封装版本 2；默认旧构建仍为版本 1。没有稳定协议或旧应用自动重定位声明。

独立 Python 审计从原始 Snapshot、登记表和事件重建预期字节：逐字节检查整段受管数据、活动栈，仅允许登记返回槽/映像引用/HWND 改动；核对 RIP、RSP、R10、R8 和目标 R11。每次三帧恢复 123 项、四帧 129 项细核验，主矩阵将其各计为一个审计检查。另在证据副本故意破坏数据、栈、RIP、R8、目标 SSP 观测，五次均被准确检测，原证据哈希不变；[6 项只读审计对照](audit-negative-summary.json) 不计作新原生实验。

## 失败没有被抹掉

旧内核与相同 EXE 副本最小链路真实得到 `restore.build_or_image_identity / 1306`，见 development 归档的 red1。开发期混合助手构建曾出现 `abort.helper_identity / 1307`；重建匹配的通道组件后另开目录运行，未删除该错误。R12 原型的 m1/g1 与最终 R8 m2/m3/m4 分开，不能将早期原型当成最终构建。

最终四个拒绝路径为遗漏真实返回槽、错误 RVA、遗漏必要登记、错误 EXE 身份。均在实际新目标 checkpoint 拒绝，未写 RB 源现场、未发布半成品；RA 仍可观察和绘制，磁盘固定包不变，随后由另一次显式管理器操作恢复全部分支。m3 中六次未完成目标侧原生清理仍导致对应检查失败，尽管监督器最后实际确认进程/Job/句柄/Desktop 清理，新操作亦成功，不能据此改记原清理成功。

原 R5-004a 的 a1 和串行 serial-probe 也复现相同超时，不能归因于并发运行实验。WCT 与本地受控转储的只读调用栈显示 GUI 在 `NtUserDestroyWindow` 经 `msctfmonitor!UninitLocalMsCtfMonitor` 等待；同进程工作线程位于 COM/文本服务初始化等待。内部函数部分只有邻近导出符号，远端等待原因未确定，不能猜测安全软件或断言 Windows 缺陷。

独立 diagnostic 构建研究把阻塞清理移出 VEH，只在干净未发布目标由 Windows 异常续接进入固定不返回入口，保持目标 RSP/SSP，不用于业务执行恢复。此构建一次通过，但随后相同启动周期旧 inline 构建也通过。实际查询当前 LastBootUpTime 为 `2026-10-28 08:51:50 +08:00`，此前失败发生在较早启动周期；环境变化混淆因果，**不将 deferred 机制宣布为修复，也不集成到正式恢复内核**。另一诊断构建曾有 `SetWindowsHookExW.abort_thread / 126`，同样保留失败。

微软提醒 VEH 避免可能同步等待或分配的调用，支持继续研究回调边界，但文档本身不证明本次根因：[VEH 回调约束](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nc-winnt-pvectored_exception_handler)、[官方上下文修改示例](https://learn.microsoft.com/en-us/windows/win32/debug/using-a-vectored-exception-handler)。地址描述依据 [PE/RVA 规范](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)。本地 `.dmp` 未发布，公开的是筛选后的 JSONL 与线程栈文本。

## 复现与原始证据

先按既有实验构建 R5-003/R5-004a 依赖，再执行以下命令；输出目录必须全新，保留失败不重试同一操作：

```powershell
./experiments/r5-004b/build.ps1 -OutDir D:/focus-views/experiments/r5-004b/out-repro
./experiments/r5-004b/test.ps1 -OutDir D:/focus-views/experiments/r5-004b/out-repro -ResultsDir D:/fv/b/repro-matrix
./experiments/r5-004b/test-gate.ps1 -OutDir D:/focus-views/experiments/r5-004b/out-repro -ResultsDir D:/fv/b/repro-gate
python experiments/r5-004b/verify-evidence.py --deep
```

`test-gate.ps1` 默认执行完整旧回归；本次 g2 显式 `-SkipRegression`，完整旧链实际来自 g1，不能将未运行写成再次通过。最小单场景用 `test-one.ps1`，代码/参数见实验 README。诊断构建与正式 out-final 分开，不能混用不同尺寸的清理通道。

| 归档名称 | 内部根目录 / 内容 |
|---|---|
| final-matrix.tar.xz | m4，最终 16 场景全部原始文件 |
| controlled-matrix.tar.xz | m3，同目录首轮及六个清理红项 |
| crash-matrix.tar.xz | g2，最终 63 场景，无旧回归重跑 |
| complete-regressions.tar.xz | g1，开发原型及完整旧链；四个 chunk 顺序连接后是原归档 |
| cleanup-regression.tar.xz | a1，默认旧构建停止点回归失败 |
| cleanup-diagnostics.tar.xz | WCT/线程栈/串行对照的公开文本 |
| cleanup-followup.tar.xz | isolated1/deferred1/direct-currentboot、诊断源码与当前启动观测，仅白名单文本 |
| tested-build.tar.xz | 最终二进制、主源码原字节、构建身份、自研依赖与实际汇编清单 |
| development.tar.xz | 旧核最小拒绝、早期正例/原型和早期审计对照 |

每份归档都有逐文件长度/SHA256 manifest；大文件分块另有 parts 清单及连接 SHA256。制作时已流式解包逐文件对照源原字节，`verify-evidence.py --deep` 可再次只读核验，绝不将校验计作新 Windows 实验。汇总结论见 [summary.json](summary.json)。所有旧实验源码、历史 false summary 与原始版本均保留。

## 尚未支持

仅同会话、同构建、固定受管数据/栈地址、三/四协作帧与登记的主 EXE 引用；不支持任意指针、DLL 重定位、优化器隐藏引用、任意调用深度、普通堆搬迁、多线程或任意 GUI 应用。当前仅既有 EDIT/Undo/关闭文件/Artifact 子集。人工宿主操作未执行；没有宿主重启、断电、稳定跨版本持久化或整体发布可靠性声明。下一项应针对清理回调的可复现阻塞验证可靠修复，不进入 R5-005。
