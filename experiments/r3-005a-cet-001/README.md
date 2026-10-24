# R3-005a-CET-001：受保护返回链机制对照

本实验从 `d96b2e3` 的 ret-probe 分出，旧源码与证据保持不变。它区分目标原生调用链、同线程上下文恢复，以及源退出后的跨进程原始现场恢复。诊断完成不代表执行恢复通过。

实现使用 C++20、Windows SDK 和少量 x64 汇编。CET、/GS、CFG 保持开启，没有影子栈写入、源 SSP 移植、RET 替换或安全策略修改。GUI 资源和输入的代码只保留已有实验路径，不扩展功能；新实验先在无输入钩子、无采集路径检验 CET。

## 构建与运行

```powershell
$toolchain = @{ MsvcRoot='<MSVC 根目录>'; SdkRoot='<Windows Kits\10>'; SdkVersion='<版本>' }
New-Item -ItemType Directory -Path '.\experiments\r3-005a-cet-001\out' -Force | Out-Null
.\experiments\r3-005a-cet-001\build.ps1 @toolchain -OutDir '.\experiments\r3-005a-cet-001\out\legacy' 2>&1 | Tee-Object '.\experiments\r3-005a-cet-001\out\legacy-build.log'
.\experiments\r3-005a-cet-001\build.ps1 @toolchain -Crt dynamic -OutDir '.\experiments\r3-005a-cet-001\out\control-md' 2>&1 | Tee-Object '.\experiments\r3-005a-cet-001\out\control-md-build.log'
.\experiments\r3-005a-cet-001\build.ps1 @toolchain -Crt dynamic -Ehcont -OutDir '.\experiments\r3-005a-cet-001\out\ehcont-md' 2>&1 | Tee-Object '.\experiments\r3-005a-cet-001\out\ehcont-md-build.log'
.\experiments\r3-005a-cet-001\test.ps1 -ResultsDir '<全新目录>'
```

默认测试每个变体重复两轮，并重跑未修改的原始 R3-005a ret-probe。先构建原 R3-005a 是该回归的前提。测试不自动下载工具或安装 CRT；动态变体依赖已安装的微软 MSVC/UCRT，报告记录实际版本。原静态 CRT＋EHCONT 构建若失败应保留原错误，不能加 `/force:guardehcont`。

单独的最小运行：

```powershell
.\experiments\r3-005a-cet-001\out\legacy\r305a.exe --output '<全新目录>' --scenario rtl
```

| scenario | 实际路径 |
| --- | --- |
| native-chain | 新进程在原生启动栈建立并返回三层诊断 C++ 函数；不进入源业务入口 |
| native-seh | 同样的原生诊断链，通过真正 RaiseException/SEH handler 后返回 |
| rtl-local | 活跃诊断栈内 RtlCaptureContext，再由更深一层调用 RtlRestoreContext |
| legacy | 源退出后，SetThreadContext 写回原始栈、内存和基本上下文，尝试返回 |
| xstate | 先将目标自身 CET_U 原值写回，再恢复源基本字段并保留目标 CET_U |
| rtl | 恢复源原始字节；在受管栈下半部的私有运行时转移帧调用 RtlRestoreContext |
| rtl-native | 原生引导栈直接请求 RtlRestoreContext 转向源受管栈，记录栈边界拒绝对照 |

`rtl` 沿用旧实验显式 NT_TIB 栈边界及固定栈 ABI，转移帧低于源全部活跃栈帧；实际业务帧字节未被重建函数或业务初始化替代。它没有解决 GetCurrentThreadStackLimits、TLS、系统原生栈与完整线程资源的恢复问题。`rtl-native` 单独保留，避免把跨栈拒绝误判成首个 RET 的 CET 故障。

`InitializeContext2` 和 `SetXStateFeaturesMask` 配置缓冲区；实际状态来自停止线程的 GetThreadContext。CET_U 写入只限当前目标在同一停点读出的原值。原值写回成功不能推导任意 SSP 可写，也不能推导影子栈能克隆。RtlRestoreContext 没有返回值，记录调用点、实际后续调试事件和异常。

实际运行、返回码、对照矩阵和官方接口依据见 [实验报告](results/20261024-cet001/report.md)。恢复未通过时 EXE 与总测试均返回 2，不发布 View；R3-005b/R4 保持未开始。
