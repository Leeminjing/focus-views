# 失败路径的端到端验收

2026-10-28，基线 ee6a6326b，加本轮源码修改。Windows 11 x64 10.0.26200、普通用户、MSVC 19.44.35229.0、SDK 10.0.18362.0。全部原生构建在测试前完成；保留 C++20、/GS、CFG、CET、ASLR 和既有协作执行范围。**本轮验证错误处理，不证明历史文本服务等待或 SetWindowsHookExW /126 的根因已找到。**

## 最终实际运行

最终入口为 C:/fv-c/fp/final3，同一启动周期 2026-10-28 19:42:46.5000000 +08:00，同一批应用/内核二进制。test-real.ps1 增加显式 -BlockedCleanup 和 -QueryFailureOutDir；没有再造阻塞窗口程序，也没有新增清理候选或恢复资源。

| 项目 | 结果与边界 |
|---|---|
| 原 r5-004c/test-diagnostic.ps1 的 blocked: | 自有窗口过程真实阻塞；原操作 summary.passed=false 保留，诊断期限、停止线程/栈、其他 View 存活、独立物理清理和新恢复均符合原断言 |
| 失败后联动 | 确认全部原操作的监督 summary 与 owner.fvc 后，仅运行一次新的安装/完全无清理运行时关闭对照；两组正常关闭，原失败和原日志哈希不变 |
| 缺少清理证明 | 复制实际失败材料到私有目录、重关联副本路径，仅删除副本的一份 owner.fvc，再调用真实 test-real.ps1 -FollowupOnlyEvidence；明确拒绝，没有创建后续对照窗口 |
| 真实输入加载失败 | 私有应用目录仅复制实际 r305a.exe，故意不放 runtime304.dll；真正 LoadLibraryW 返回 0/126，通道及控制器保留 LoadLibraryW.runtime/126，线程钩子未尝试，助手退出得到确认 |
| 仅附加取证失败 | 单独显式测试构建在诊断代码中实际调用 GetProcessTimes(nullptr)，返回错误 6；原加载/安装 API 成功、通道首错为空，原应用完成执行恢复、原生控件输入和绘制 |
| 旧受影响套件 | b001 18 场景、279 检查，CET-002 14 场景、285 检查符合预期；包含既有拒绝反例，不是 32 个恢复正例 |

七个阶段均符合各自验收，见 [最终原始驱动 summary](final-driver-summary.json) 和 [本轮汇总](summary.json)。**原操作仍失败；独立 diagnosticChainPassed=true 只表示故障处理和后续诊断链完成。** [原 summary](original-failure-summary.json)、[原处理器 assessment](original-handler-assessment.json)、[联动 assessment](followup-assessment.json)、[缺失证明实际入口](missing-proof-assessment.json) 均分别保存。没有修改原 summary 为绿色，也没有伪造清理证书。

缺失证明用例是明确人工材料反例：只修改完整私有副本，原 summary 哈希不变；入口在读取证书时拒绝，newControlPairRuns=0。资源未清理、启动周期变化和重复尝试的安全门仍由判据拒绝；本轮没有为这些场景制造额外系统故障或实际重启宿主。

## 等待链判据

原来“出现三类节点”的统计现在必须建立关系：已确认的目标进程/GUI 实例 → GUI 查询中的 ThreadWait → 同进程工作线程 → 该工作线程的另一条查询 → COM → 所报告的 RPCSS PID 终点。核对查询线程、顺序、完整 node count、原生结果和创建时间；工作线程在 GUI 查询之前的 pinned thread_context 也须与其后查询的创建时间一致，以排除相同 TID 的另一实例。任何必要材料缺失、查询失败、节点截断或无法关联都返回 insufficient_evidence，不触发 direct-window。

[人工红项](wait-pattern-red.json) 复现了旧类型统计对不相干查询和错误 GUI 身份的误分类；首个测试夹具曾误用了 PowerShell 的 R 别名，未产生分类结论，其控制台错误单独保留。[最终七例](wait-pattern-green.json) 覆盖真实结构的人工正例、跨查询不相连、三类节点分属三条查询、截断、错误 GUI 实例、查询失败及工作线程实例替换。

原生诊断仅补记 GetThreadWaitChain 返回的 count，没有新增采样资源或更换清理实现。NodeCount 是实际返回数量，WCT 信息会动态变化，不能把两次查询当作原子快照，更不能据此声称完整死锁/根因成立。[Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/wct/nf-wct-getthreadwaitchain)

人工 blocked: 证据没有被标成历史 COM/RPC 等待。[只读分类](blocked-classification.json) 为“证据不足”；后续两组普通关闭也没有同类等待证据，因此 **direct-window 未执行**。不因需要覆盖分支而填写虚假的等待类型。

## 验收实际抓到的代码问题

1. 初次真实缺失 DLL 用例，助手已保存 LoadLibraryW/126，但控制器以 validate/13 覆盖，并缺少助手退出记录，见 [原退出审计红项](load-lifetime-red.json)。b001/CET 两处安装失败处理现在先保留原错误，等待自有助手实际退出并记录，然后传播原错误；独立收尾错误不能盖过主错。没有重试加载或安装。[最终输入负例](input-negative-assessment.json) 验证修正及单独的取证查询失败。加载失败的 /126 **不是**历史线程钩子 /126 的复现。
2. 首轮组合入口因 JSON 将 ISO 启动时间物化为 DateTime，再转文化字符串丢掉格式/小数而误报 boot-changed。原失败见 [first-driver-summary.json](first-driver-summary.json)，同一启动周期的 [只读证据](boot-typing.json) 显示两个 UTC ticks 完全相同。现在按完整 UTC ticks 比较，保留亚秒精度；[九项判据](followup-tests.json) 包含 JSON 日期对象、相同 UTC 时刻和仅一 tick 不同的拒绝。

开发轮 final1 保持失败；final2 的中间通过也保留，此后补严工作线程实例关联，最终用同一二进制、最终脚本在 final3 完整复验。每轮均为新的显式操作和新快照来源，不重试同一次失败 Snapshot，不覆盖红项，不以压力次数挑选自然故障是否出现。原 40 次钩子 /126 和历史自然清理失败全部保持原结论。

## 构建、复现和材料

先分别运行 b001/CET 的 build.ps1 -InstallDiagnostics；另建 b001 的 -InstallDiagnostics -DiagnosticQueryFailure 测试变体。后者仅 input 翻译单元定义 FOCUS_VIEWS_INPUT_DIAGNOSTIC_TEST_FAILURE，原安装 API 返回值不改写。控制构建复用原清理运行时/内核，只重建诊断助手等实验单元。实际源材料按原 build-id 算法重新计算，三份程序全部匹配，见 [native-build-verification.json](native-build-verification.json)。原有构建目录未覆盖。

~~~powershell
./experiments/r5-hook-126/test-real.ps1 -InputOutDir <正常b001诊断构建> -CetOutDir <CET诊断构建> -ControlOutDir <完整wait控制构建> -Profile <resource.bin> -QueryFailureOutDir <显式取证失败构建> -BlockedCleanup -ResultsDir <全新短目录>
~~~

[raw.tar.xz](raw.tar.xz) 与[逐文件清单](raw.tar.xz.manifest.json) 保存实际失败/通过日志、CPU/栈、监督证书、输入、图像、快照、人工材料副本和开发轮红项；[tested-package.tar.xz](tested-package.tar.xz) 与[清单](tested-package.tar.xz.manifest.json) 保存实际二进制/PDB、工具链身份、源码及脚本。所有文件长度和 SHA256 从归档解压流验证，原文件不修改。

本轮最终串行入口通过，原人工原生清理仍为失败。完整 R5 嵌套回归、宿主重启、断电及人工宿主交互未执行。nativeCleanupPassed、cleanupCausalFixVerified、fullR5004Passed、gateClosed 在历史关口继续 false；历史 CreateProcessW/5 可靠性问题独立保留，不进入 R5-005。
