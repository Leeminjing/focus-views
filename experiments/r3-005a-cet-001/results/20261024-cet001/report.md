# R3-005a-CET-001：官方上下文机制未恢复跨进程返回链

**2026-10-24：已编译、已运行；源执行现场恢复未通过。** 3 个变体、7 条路径、各重复两轮，共 42 次；532 项证据核对符合各自预期，`sourceExecutionRestorePassed=false`，总脚本退出 2。该数字包含逐次身份、原始字节、引用与清理检查，不代表能力数量。其中 24 次跨进程恢复均失败；诊断对照成功没有计为业务恢复。R3-005a、R3-005b/R4 的原状态保持。

## 环境、源码和构建

基线 `d96b2e3bb70466340fbf4d1d69bd5d7b2be83ef4`，Windows NT 10.0.26200.0 / x64，非管理员，MSVC 19.44.35229.0、SDK 10.0.18362.0。源码 SHA256、权限与设置声明见 [identity.json](identity.json)。实际程序、DLL、构建配置的身份分别在三个变体目录的 `binary-identity.json`，构建命令见 [README](../../README.md)。

| 变体 | CRT | /guard:ehcont | 共同条件 |
| --- | --- | --- | --- |
| legacy | /MT | 无 | C++20、/GS、/CETCOMPAT、/guard:cf、/Gy、相同源码 |
| control-md | /MD | 无 | 同上 |
| ehcont-md | /MD | 有 | 同上，编译/链接同时启用 EHCONT |

最后两组只改变 EHCONT；没有把 CRT、CFG 差异归因于 EHCONT。`ehcont-md/loadconfig.txt` 实际有 EH Continuation table present 和 15 个条目；无 EHCONT 的两组没有该标记。脚本把实际加载地址归一到 PE 首选基址，对比真实表条目，确认保存 IP 与 rtl-local 手工捕获 IP 均不在表中。所有目标策略原始值 261（0x105）：影子栈及上下文 IP 校验开启；CET_U 读取的控制值为 1。没有 SetProcessMitigationPolicy、安全设置修改、关闭 CET/GS/CFG、影子栈写入、源 SSP 移植、RET 改写或内部系统调用。

本机旧静态 UCRT 与 EHCONT 无法链接，保留 [实际构建失败](ehcont-static-build-failure.log)：`libucrt.lib(fputc.obj)` 报 LNK2046，随后 LNK1386；没有使用 `/force:guardehcont`。动态配对变体使用机器已安装的微软运行库，未安装或下载依赖，[版本与 SHA256](installed-ms-runtime.json) 单独记录。这项工具链失败不等于 CET 机制失败。

## 实测矩阵

每个格子均运行两次，原始 JSONL、标准输出和二进制字节位于对应变体/路径目录；总判定与全部检查见 [summary.json](summary.json)，总输出见 [console.log](console.log)。

| 路径 | legacy /MT | control-md /MD | ehcont-md /MD | 实际含义 |
| --- | --- | --- | --- | --- |
| native-chain | 普通三层返回成功 | 同左 | 同左 | 新进程自身正常 CALL/RET，非源业务 |
| native-seh | 真正 SEH 后返回成功 | 同左 | 同左 | 系统合法异常延续对照 |
| rtl-local | 同线程恢复并返回成功 | 同左 | 0xC0000409 / 48 | 手动捕获的 IP 不自动成为 EHCONT 延续目标 |
| legacy | 首个 RET，子码 57 | 同左 | 同左 | 基本 SetThreadContext 无法重建影子返回链 |
| xstate | 原值写回成功，RET 57 | 同左 | 同左 | 目标 CET_U 原值不代表源返回链 |
| rtl | 首个 RET，子码 57 | 同左 | ntdll 中子码 48，未到 RET | 官方调用不自动克隆源受保护返回链 |
| rtl-native | ntdll 中子码 13 | 同左 | 同左 | 从目标原生栈切到受管栈的独立拒绝对照 |

SDK [原始定义](sdk-symbols.txt)：13=`FAST_FAIL_INVALID_SET_OF_CONTEXT`，48=`FAST_FAIL_SET_CONTEXT_DENIED`，57=`FAST_FAIL_CONTROL_INVALID_RETURN_ADDRESS`。不将 13 或 48 说成首个 RET 的 57；也不从子码推断未经验证的内核检查细节。

新进程自身诊断链不进入 `application()`；有独立随机数据、volatile 栈数组、三层普通 C++ 返回和 /GS，检查真实返回次数为 3、计算结果及 SSP 前后平衡。`native-seh` 把真实首次机会异常交给应用原生 handler，不由调试器吞掉；EHCONT 构建下仍可返回。`rtl-local` 在仍活跃的叶函数内调用 RtlCaptureContext，再从更深层调用 RtlRestoreContext，记录一次实际调用及返回；其成功只证明现存本线程链的延续。

## 跨进程恢复的具体证据

每条跨进程路径都独立运行真实源自绘 GUI：一次业务初始化 → 受管安全点 → 捕获页/栈/上下文 → 源完成普通返回、改变文本和计数 → 源退出确认 → 不同 PID 承载进程。新进程不进入业务初始化或原检查点；恢复器只写原始字节、声明的三个 HWND 槽和既有 CRT ABI cookie，不直接填写业务结果。

控制器核对整个受管栈/数据读回；脚本另比对原始活跃栈、数据页和三处引用，并从原始栈局部值与 CONTEXT.R10 独立重算源实际完成的 receipt。源已死、新 PID、零初始化重放、失败不发布 View、进程退出和 Desktop 关闭均逐次记录。最初 [原始 ret-probe 回归](original-ret-probe/summary.json) 也再次复现旧故障，旧实验文件未修改。

有代表性的原始记录：

- [control-md/rtl-1](control-md/rtl-1/controller.jsonl)：保存 RIP `140697042620753`、RSP `1859723001256`。目标运行时在栈下半部 RSP `1859721953192` 调用 RtlRestoreContext，TIB 范围 `1859720908800..1859723001856` 包含保存 RSP。API 随后真正到达 RIP `140697042620756` 的 RET（`c3`），异常 0xC0000409 / 57。普通返回地址 `140697042848885` 与目标影子返回不一致；没有到达 leaf/middle/outer 完成断点。
- [ehcont-md/rtl-1](ehcont-md/rtl-1/controller.jsonl)：同样的布局与实际调用，被 ntdll+1197126 处的 fast-fail 48 拒绝，尚未到保存的 RET。
- [control-md/rtl-native-1](control-md/rtl-native-1/controller.jsonl)：原生调用栈/TIB 范围不包含保存 RSP，ntdll 内拒绝 13；单列这条路径，没有让它遮蔽受管布局中可到 RET 的 CET 失败。
- [control-md/xstate-1](control-md/xstate-1/controller.jsonl)：GetThreadContext/LocateXStateFeature 真实读取；SetThreadContext 对目标 CET_U 原值返回成功；源基本字段＋目标原值 CET_U 也成功。读回 SSP 保留目标值，随后 RET 57。没有把源 SSP 写入目标 CET_U。

`rtl` 的转移帧低于保存的全部活跃源栈帧，只运行引导和系统 API，不执行源任务；沿用 R3-005a 原有的固定栈及显式 TIB 边界 ABI。这是为了把跨栈拒绝与返回链失败分开，不是修复 GetCurrentThreadStackLimits：OS 原始栈、完整 TEB/PEB/TLS、自动扩栈及完整 XSTATE 仍未恢复。`rtl-native` 仅是针对调用边界的反例，不能从它单独断言某一内部范围检查就是全部原因。

## 官方接口支持什么，本轮证明什么

| 接口/机制 | 文档作用与本轮边界 |
| --- | --- |
| GetThreadContext + LocateXStateFeature | 停止线程上读取基本状态及 CET_U；不创建返回链 |
| InitializeContext2 | 初始化满足大小、对齐和特征要求的缓冲区；CONTEXT_XSTATE 必须显式请求 |
| SetXStateFeaturesMask | 设置缓冲区有效特征掩码，实际读写另由线程上下文 API 完成 |
| SetThreadContext | 本轮基本字段写回和目标 CET_U 原值请求成功；没有影子栈内容复制语义 |
| RtlRestoreContext | 恢复调用者上下文，支持本线程延续；本轮活跃链对照成功，跨进程未成功 |
| /guard:ehcont | 合法延续 IP 元数据，影响 IP 校验；不是影子返回地址链的分配/克隆接口 |

上述缓冲区语义来自微软 [InitializeContext2](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-initializecontext2) 与 [SetXStateFeaturesMask](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setxstatefeaturesmask)。[SetThreadContext](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadcontext) 明确要求停止线程，也保留由 OS 管理的状态；不能由其成功返回推导全部状态可设置。

[RtlRestoreContext](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlrestorecontext) 无返回值，实验以调用前陷阱、后续执行/异常和上下文为证；未把“调用过”当成成功。[EHCONT 文档](https://learn.microsoft.com/en-us/cpp/build/reference/guard-enable-eh-continuation-metadata?view=msvc-170) 说明延续 IP 的验证及表生成；本轮实际表和 SEH 对照与此相符。硬件返回和上下文 IP 策略是不同检查，见 [PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-process_mitigation_user_shadow_stack_policy)。

**本轮没有证明 CET_U 绝对只读，也没有证明任意 SSP 可以写。** 仅证明指定 API 接受当前目标的原值；没有执行非平凡 SSP 修改，没有跨进程复制受保护对象。也未穷尽所有公开用户态机制，因此结论是“本轮测试的机制没有恢复源返回链”，不是“Windows 用户态恢复不可能”。

## 结论与下一项架构问题

固定受管内存、普通栈和已声明 GUI 引用可以捕获/写回；CET 返回资源仍未重建。RtlRestoreContext 能管理当前线程已有链的延续，并未在本实验中为新线程生成源调用链。EHCONT 给合法控制转移提供校验信息，不替代该资源。

下一种可研究的架构是**由受管运行时协同建立检查点续接帧**：把可保存点约束在已知运行时边界，研究在新线程通过正常 CALL 建立与恢复帧匹配的受保护返回链，而不执行源业务初始化或检查点前的任务计算；内存、栈局部量、寄存器和 GUI 引用仍由运行时恢复。这需要编译期/运行时配合的独立机制实验，尚未实现，不能称为应用状态导入或任意原生程序兼容。若无法在不重放业务的情况下维持原执行语义，该候选也应拒绝；不能把它作为本轮成功的替代。

没有恢复后业务帧或输入，本轮没有新增图像作为恢复证明。没有开始 R3-005b/R4，没有扩展输入 API；R1/R2、R3-001～004 未修改也未重跑，旧采集偶发失败和宿主不确定结论保持，人工交互未执行。
