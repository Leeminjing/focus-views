# 真实旧输入调用点取证与有界故障后对照

2026-10-28，基线 9f8a90f，先提交正常关闭整例判定修复 d21d1e310，再构建本次诊断变体。**没有定位或修复历史 /126、文本服务等待；完整 R5-004 仍未通过。**

## 已修复的确定性漏判

正常关闭脚本改为独立 operationPassed，核对 controllerExit、managerExit、唯一 trim_complete、原生关闭、firstApi/firstError、协议与物理清理；evidenceComplete 只表示材料完整。通用监督器没有改动。原人工红/绿六例见 [verdict001](../../../r5-004c-wait/results/verdict001/report.md)；随后补充 manager 单独非零、仅首错 API、仅首错数值的独立反例，[九例均通过](../../../r5-004c-wait/results/verdict001/expanded-green.json)。不是原生故障次数，也不声称上一轮两个正常关闭例发生了漏判。

## 诊断进入真实业务路径

r3-004/input.cpp 在显式 FOCUS_VIEWS_INPUT_INSTALL_DIAGNOSTICS 下，直接观测原 LoadLibraryW、两个 GetProcAddress、WH_GETMESSAGE/WH_CALLWNDPROC 安装及原卸钩/释放调用。返回后先保存原始结果和错误码，再做取证；失败仍按保存的值传播。没有预加载、替换 API/DLL、修改搜索路径、改变输入/恢复逻辑或增加安装重试。

日志包含实际 DLL 参数、助手 EXE 和已加载模块的磁盘哈希、真实模块/导出地址与归属、助手和目标 PID/TID/创建时间、Desktop。补充查询错误只记 diagnostic_failure，不改通道首错。GetModuleHandleExW 的观察不增加引用，不把观察所得句柄用于 FreeLibrary。磁盘哈希不是加载内存全镜像证明；额外文件/实例查询会改变时序，不能将诊断变体称为旧二进制原样运行。

两份程序独立编译到 C:/fv-c/rd/b001-build 和 cet-build，原 out 未覆盖。Windows 11 x64 10.0.26200、普通用户，MSVC 19.44.35229.0、SDK 10.0.18362.0，保留 /GS /guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT。两份最终诊断构建都在原生测试前完成；metadata 记录开关、input/header 哈希和应用 buildId。恢复、CET、控件、窗口/线程归属以及生命周期断言未删。诊断测试显式核对这些哈希后，仅为已审查 input.cpp 变更增添精确基线白名单，默认旧测试不放宽。

## 固定构建串行运行的实际结果

系统启动周期保持 **2026-10-28 19:42:46.5000000 +08:00**。没有重启、关闭保护、修改服务/IME 或借用宿主输入。

| 执行项目 | 实际结果 |
|---|---|
| 原四帧 recorder relocation-bad-offset，inline 原入口 | 正确拒绝 RB，原生清理确认、RA 存活及独立新恢复通过 |
| 诊断构建的原 b001 positive 最小链 | 真实恢复、控件/输入及生命周期审计通过 |
| R3-005b 本体套件 | 18 场景、279 检查符合预期 |
| CET-002 本体套件 | 14 场景、285 检查符合预期 |

见 [清理最小路径](cleanup-minimal-summary.json)、[真实旧程序最小链](legacy-minimal-summary.json)、[两套完整本体结果](suites-summary.json)。32 场景包括既有预期拒绝，不是 32 个恢复正例。b001 的 CET 嵌套入口显式跳过，CET 在另一份已预构建目录单独完整执行；**未执行完整 R5-003/R5-002 等嵌套回归或原 R5-004 全矩阵**，不冒称这些本轮全绿。

### 保留本轮入口红项

首轮 test-real.ps1 完成两个最小原生路径后，两个旧套件都因要求构建目录/build.log，而日志实际在目录外，在进入场景循环前停止。[first-driver-summary.json](first-driver-summary.json) 的 serialVerificationPassed=false 原样保留。不是新的 /126 或清理等待。

将已有完整构建日志复制到正确位置，没有重编译或改动应用/DLL；用 test-suites.ps1 只继续此前未开始的两个套件，未重复两个已通过最小路径。执行前后构建哈希和启动周期一致。test-real.ps1 随后增加预检；[真实脚本的人工缺失文件对照](preflight-refusal.json) 验证在启动任何应用前拒绝。**最终入口未为了获取绿色总结果重跑整条链**；其完整成功执行不记为已验证，原失败不被续跑覆盖。

## 故障现场与未触发的后续分支

[只读安装审计](real-audit.json) 从真实业务流程的 26 个输入通道检查 260 项材料：每例都有真实模块/导出身份、目标实例、原始调用序列及卸钩/释放结果；本次均成功，未出现 /126 或附加查询失败。该审计不启动进程，也不是新增原生实验。旧 40 次 /126 和缺失助手退出记录的历史失败仍保留，原因未知。

清理最小路径没有失败，[followup-decision.json](followup-decision.json) 因 cleanupFailed=false 正确跳过独立关闭对照。没有重新运行独立 hook-probe、安装/无运行时正常关闭对照或 normal-new-window-direct 来追求更多成功记录。

已接入的后续流程只在原清理失败及等待证据固定、原监督器确认自有资源收尾、启动周期不变时，发起一次新的诊断窗口操作；不是 Snapshot 重试。只有安装/完全不安装两组都保留同类等待且物理收尾确认，才运行一次 direct-window。原测试的失败结果始终保留，后来成功不能改写。七项纯人工安全判据见 [followup-safety-tests.json](followup-safety-tests.json)：无自然失败、残留资源、换启动周期、已经尝试过及只有一组等待都不会继续。**本轮未在自然故障下实际触发这两个后续分支**，不把人工判据通过称为自然触发链验证。

## 原始材料与结论

[summary.json](summary.json) 分开记录两个最小路径、32 个套件场景、首轮入口红项、现场是否复现、预检修正与未执行的完整回归。nativeCleanupPassed、cleanupCausalFixVerified、fullR5004Passed、gateClosed 在历史关口仍为 false；没有因本轮成功宣称因果修复或进入 R5-005。人工宿主交互未执行，历史 CreateProcessW/5 门禁独立保留。

[raw.tar.xz](raw.tar.xz) 保存本轮原始 CPU/栈/资源、输入/调试/生命周期日志、图像、工作文件、第一轮失败及后续套件等 982 个文件；[清单](raw.tar.xz.manifest.json) 逐文件长度和 SHA256 从解压流验证。[tested-package.tar.xz](tested-package.tar.xz) 与[清单](tested-package.tar.xz.manifest.json) 保存实际诊断二进制/PDB、复用控制构建和 native 源码身份。native input/header 与构建时哈希相同；打包的最终 driver 含后加预检，identity.json 明确其仅做了预检拒绝测试，不能当作首轮执行的精确脚本版本。后续九例判定 JSON 单独公开，不声称包含在此前冻结的 raw.tar.xz 中。
