# R5-004c 固定版本完整组合验收

2026-10-29，最终入口 **C:/fv-c/c3，退出 0，八阶段通过**。代码冻结提交为 **6415104b8f8df92c15351a197b15314169a181c8**；起始版本为 1ef786b。Windows 11 x64 10.0.26200、普通用户、MSVC 19.44.35229.0、SDK 10.0.18362.0。全部运行处于启动周期 **2026-10-29T08:27:21.5000000+08:00**，本轮没有重启系统或修改系统保护。

**这证明本轮组合功能和错误处理符合预期，不证明历史文本服务关闭等待、SetWindowsHookExW /126 或 CreateProcessW /5 已找到根因或得到修复。** cleanupCausalFixVerified、fullR5004Passed、gateClosed 继续 false；不进入 R5-005。

## 防重复与冻结输入

原先真实入口将 AlreadyAttempted 写死为 false。现在在原失败材料之外，使用同一 Windows 用户 LocalAppData/focus-views/failure-followups 目录；身份由原管理器 PID＋原生创建时间集合及完整启动 UTC ticks 生成。执行窗口对照之前以 FileMode.CreateNew 原子取得执行权并 flush。记录包含原 summary、日志、清理证明的哈希，成功、失败或中断均不删除，残缺记录也不能自动释放执行权。这里没有新增调度框架。

[实际入口六项验收](once-final.json)：首次允许一个对照；同来源换结果目录仍在窗口创建前拒绝；两个由事件同时放行的独立调用只有一个取得执行权；失败的对照仍消耗记录，之后不能再次启动。见 [首次联动](first-followup.json)、[二次拒绝](second-refusal.json)、[竞争一](race-1.json)、[竞争二](race-2.json)、[失败后拒绝](failed-attempt-refusal.json)。原人工阻塞 [summary](original-failure.json) 继续 false，不能由后续成功覆盖。人工阻塞不是历史自然等待的复现。

原代码的红项由真实入口复现，见 [once-red](once-red.json)。没有伪造 API 返回或仅测试内存布尔值。原有缺失清理证明的实际入口拒绝和真实输入错误传播也在最终组合中重跑，见 [missing-proof](missing-proof.json)、[input-negatives](input-negatives.json)。加载不存在 DLL 的 /126 不是历史钩子安装 /126 的复现；另一个显式测试构建只让附加取证调用失败，真实安装返回和通道首错不变。

prepare-combination.ps1 在测试前准备十套旧默认构建、候选执行/清理/关闭对照包和三份单独输入诊断包，并完成既有 acknowledged-ring 依赖 profile。旧包使用各自正确的依赖，不将候选磁盘版本 2 DLL 强塞入旧版本 1 资源协议。测试阶段没有编译、覆盖依赖或更换源码。

最终冻结 **17 个构建目录、825 个文件**（含额外 R1 既有构建），包括实际源码/脚本、EXE/DLL、库/对象/PDB、构建材料和资源 profile。每阶段前后以实际文件长度与 SHA256 核对。原生二进制在三轮中保持相同，后续只修正准备/审计脚本和资源 profile。[确定性反例](frozen-byte-negative.json) 保持 build.json 不变，只修改私有测试 DLL 一个字节，仍正确拒绝；这是文件判据测试，不是原生恢复实验。

## 最终组合结果

[完整入口 summary](final-entry.json) 和 [本轮汇总](summary.json) 分别保留。下表为各套件自己的统计，不跨行累计为能力数量。旧嵌套链包含重复的底层 GUI/CET 套件，它们确实执行，但不按新增能力重复计算。

| 阶段 | 实际结果 |
|---|---|
| 防重复、缺失证明拒绝、输入错误传播 | 三阶段通过；包含正确拒绝，不代表原失败变成功 |
| inline/deferred 聚焦对照 | 4 组通过 |
| R5-004a 独立清理矩阵 | 12 场景、1,538 检查符合预期 |
| R5-004b 真实异基址矩阵 | 16 场景、1,958 检查符合预期 |
| 原 R5-004 崩溃矩阵 | 63 场景、3,174 检查符合预期 |
| R5-003 本体与完整下游 | 22 场景、1,052 本层检查；完整下游通过 |
| R5-002 | 70 主场景；主矩阵、安全点与套件检查合计 14,154，通过 |
| R5-001b | 42 场景、8,028 检查通过 |
| R5-001a | 20 场景、3,710 检查通过 |
| R4-003-001 与两个完整旧分支 | 14 场景、2,519 本层检查；R4-001、R4-002a 及各自 c001/b001/CET-002 下游通过 |
| R1 受影响工具回归 | 9 用例符合预期；复用未修改的既有二进制，没有重新编译 R1 |

拒绝场景只说明故障被准确检测，不算执行恢复成功。恢复仍限显式协作、已验证三/四帧、固定受管数据/栈地址、显式映像引用重定位及已知 Win32 资源。没有扩大控件、Undo、输入 API 或第三方应用范围。

## 两轮失败及修正

**C1 保持失败。** 本轮首次重建 recorder 后错误沿用了旧 EXE 的资源 profile。两组关闭对照均在 wait_profile.identity /1306 拒绝，管理器退出 2，没有 trim_complete；这是准备步骤漏检，不能解释为历史关闭等待。[绑定反例](profile-binding-red.json) 表明 profile 自身哈希有效，但其中映像 SHA256 与新 EXE 不同。[首轮 summary](first-entry-failed.json) 保留；聚焦及独立清理原生断言完成，源码修正后冻结检查阻止尚未开始的异基址、崩溃和旧回归阶段。没有覆盖失败或重新使用已消耗的来源记录。

准备步骤现在先以当前构建生成原生 recorder S0，独立监督确认源进程与资源清理后，经已有 profile.exe 提取配置；入口还在创建任何窗口前复核 profile 长度、自身 SHA256 和所绑定的 EXE SHA256。见 [当前绑定](profile-binding-green.json)。没有删除原生身份检查或手工篡改 profile 的映像值。

**C2 完整执行，仍保持失败。** [原 summary](audit-entry-failed.json) 中原生矩阵完成，但旧默认审计把先前已提交的门控输入诊断 input.cpp 列为未批准差异，使末端和上层回归失败。没有原生故障证据指向新的恢复或输入机制问题。默认构建未启用诊断宏，诊断变体保持单独包。

修正只额外认可 input.cpp 在 **1ef786b92d808fe543edbbd80e40c47ae9bb160a** 的已核对版本，而不是将其永久加入无条件白名单。原历史材料及其他实现仍受审计；未来改变该文件也不能自动通过。[三项私有仓库对照](approved-source-tests.json) 验证精确版本接受、后续 input.cpp 修改拒绝、其他旧实现修改拒绝；没有启动原生应用。这是审计基线补齐，不是历史系统故障的因果修复。

**C3 是该代码提交后的完整新操作。** 所有原生包不变，使用当前 profile 和新冻结清单，从新的源实例/快照运行整个入口；不是重试 C1/C2 的失败 Snapshot。两个末端 CET 审计及上层旧回归均实际通过，sameFinalBuild=true、sameBoot=true。之前的 false summary 和日志均保留。

## 构建与复现

先按既有 R5-004b/R5-003 说明准备 MSVC、SDK 和自研依赖。旧构建可执行、PDB、身份等材料先备份，历史结果目录不修改。

~~~powershell
./experiments/r5-004c/prepare-combination.ps1 -Root C:/fv-c/new-builds
./experiments/r5-004c/test.ps1 -OutDir C:/fv-c/new-builds/candidate -ControlOutDir C:/fv-c/new-builds/control -InputOutDir C:/fv-c/new-builds/b001 -CetOutDir C:/fv-c/new-builds/cet -QueryFailureOutDir C:/fv-c/new-builds/query -FrozenInputs C:/fv-c/new-builds/frozen-inputs.json -Profile C:/fv-c/new-builds/resource.bin -ResultsDir C:/fv-c/newrun
~~~

本轮实际准备目录为 C:/fv-c/cm。C1/C2 后没有更改 C++ 或重编译；仅修正脚本并提交代码，确认编译输入不包含这些审计脚本后，以相同原生包封存 frozen-gate-final.json，实际运行 C3。包中分别保留初次源码、C2 源码、最终提交源码及各轮清单，不能把中间版本写成最终版本验证通过。

[归档清单](archives.json) 列出实际二进制/PDB、源码/脚本、CPU/栈、原始 Snapshot、Action/Artifact、私有文件、真实 GDI Frame、输入记录、所有管理器/监督器生命周期、原始拒绝与首错、持久尝试记录及哈希。每个归档均以解压流逐文件验证长度与 SHA256；大归档按 32 MiB 分块并核对连接哈希，未改写原始材料。传输/后验核验不计作新的 Windows 实验。

本轮完整固定版本组合通过；自然文本服务关闭等待和历史 SetWindowsHookExW /126 未复现，未发现因果修复。CreateProcessW /5 独立可靠性门禁保持；人工宿主交互、宿主重启、断电恢复未执行，通用 Windows 应用兼容性未证明。
