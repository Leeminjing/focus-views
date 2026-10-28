# 跨生命周期 PID 实例身份修复

2026-10-28；基于 b1560bfd2。Windows 11 x64，MSVC 19.44.35229.0、SDK 10.0.18362.0，普通用户。未修改系统设置、CET/CFG/GS、活动窗口或线程归属检查。

## 确定性反例与修复

identity_test.exe 直接调用真实 Restart::restore_phase，而非复制判断公式。私有测试 Session 将旧 manager 声明为当前 PID、较早创建时间，并设置错误 Snapshot ABI；其他旧实例使用明确的人工身份声明。修复前先得到 snapshot.previous_lifetime_alive，未到达 ABI 校验；修复后准确得到 snapshot.runtime_abi，且没有创建进程或发布 View。另一个用例使用当前真实 PID/创建时间，仍准确拒绝 snapshot.previous_lifetime_alive。

这些是**人工声明的确定性对照**，不声称系统自然复用了 PID。历史自然复用导致的原始失败保留在 R5-004c，没有覆盖或重跑挑选 PID。

删除 restore_phase 的附加裸 PID 自比较；gone() 仍核对 PID+GetProcessTimes 创建时间及退出状态，对未知身份、查询或等待失败明确拒绝。应用恢复断言、管理器/应用 PowerShell 检查同步改为完整实例身份；新增原生创建时间日志和共享只读脚本判据。活动进程、窗口、线程、Job 的归属要求未删除。

[绿色结果](identity-summary.json) 包含 8 项原生检查、4 项脚本检查，全部通过：同 PID 不同创建时间、真实存活实例、相同实例、未知身份、真实查询错误，以及恢复路径的精确错误。读取 GetProcessTimes 的空句柄实际返回 ERROR_INVALID_HANDLE/6，原 API 和错误保留，未当作旧实例已经退出。

初轮 [red.jsonl](red.jsonl) 有两项 false：reused-pid 是产品错误；query-error-is-not-gone 是测试夹具错误，因为 -1 在 Windows 上是当前进程伪句柄。后者改用真正无效的空句柄后验证错误传播，不能把它写成第二个产品修复。原始红项均保留。

共享证据入口增加实例判据导入后，使用原 R1 二进制重跑 9 个用例，全部符合既有正向/拒绝预期，见 [R1 summary](r1-summary.json)；没有重编译 R1 或重跑 R2。

构建身份见 [build.json](build.json)。复现：先用 build.ps1 指定现有 MSVC/SDK；以一份独立完成的 fixture 保存目录为 BaselineStore，运行 test-identity.ps1 -OutDir <构建目录> -BaselineStore <store> -ResultsDir <新目录>。脚本只在新目录修改人工 Session/错误 ABI，不改变来源。

## 完整回归的实际结果

完整执行 R5-003 及 R5-002/R5-001b/R5-001a/R4-003 两条嵌套旧链，没有跳过或重试。R5-003 本体 22 场景全部通过，1,051 项本体证据检查通过，fixture/recorder 的错误 ABI 均准确拒绝；第 1,052 项旧回归汇总为 false，因此整套仍失败。见 [原始完整 summary](full-regression-summary.json)。

最终红项来自两条旧 R3-005b/CET-002 链路的输入钩子：40 个场景有原始 SetWindowsHookExW.thread_only / 126（ERROR_MOD_NOT_FOUND），随后控制器报 validate / 13，并缺少助手退出日志。旧链按原脚本的构建及依赖配置运行；这些旧输入 DLL 未重建或替换，旧业务源码和断言未修改。运行后两个 runtime304.dll 路径仍有文件，文件 SHA256 与原始错误见 [钩子失败观测](legacy-hook-failure.json)；这不能证明加载时的依赖或缺失模块，根因尚未定位，不归因于 PID 修复、内存压力或安全软件。

原清理审计的“缺少退出记录”保持失败。之后只读 CIM 没有发现命令行仍指向本次结果树的 r305a.exe，见 [后验进程观测](post-regression-process-observation.json)；这不是原时点的退出证书，不能改写旧审计为通过。没有重试选择有利 PID/环境，没有关闭保护或强杀非自有服务。完整原始测试树通过逐文件 SHA256 归档，索引见 [等待诊断报告](../../../r5-004c-wait/results/wait001/report.md)。

PID 确定性修复和 R5-003 本体得到验证；完整旧回归、清理因果修复、完整 R5-004、历史 CreateProcessW/5 门禁继续分别保留。
