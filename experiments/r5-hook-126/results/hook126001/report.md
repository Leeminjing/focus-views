# 旧输入钩子 /126：分阶段安装诊断

2026-10-28，源码基线 e57d2c6d2b48f07e5a354f3b840e06f3102fe36d，加独立探针。**本次未复现 /126，根因未知，没有产品修复或完整旧回归通过声明。**

Windows 11 x64 10.0.26200，普通用户；MSVC 19.44.35229.0、SDK 10.0.18362.0。全部本轮测试属于 19:42:46.5 +08:00 启动周期；旧 40 次 /126 发生于另一启动周期。未由本实验重启、改保护、换 DLL、重建旧程序或借用宿主输入。

## 真实安装现场

从旧 r3-004/input.cpp 裁剪 LoadLibraryW → GetProcAddress("ManagedGetMessage") → SetWindowsHookExW(WH_GETMESSAGE, ..., module, tid) 的安装顺序。探针目标是独立的 Win32 窗口/EDIT 和消息循环，不运行原恢复/业务流程。安装助手处在目标 Desktop，使用已核对的目标实例；不是全局钩子。

最终构建对两份原有 DLL 各运行一次，见 [matrix-summary.json](matrix-summary.json)：

| 旧文件来源 | LoadLibraryW | GetProcAddress | 导出实际所属模块 | SetWindowsHookExW | 卸钩/释放/独立资源签退 |
|---|---|---|---|---|---|
| r3-005b-001/out/runtime304.dll | 成功 | 成功 | 与所传 HMODULE 相同 | 成功 | 通过 |
| r3-005a-cet-002/out/runtime304.dll | 成功 | 成功 | 与所传 HMODULE 相同 | 成功 | 通过 |

日志保存调用时的 DLL 参数路径、磁盘 SHA256、实际加载路径及基址、导出地址及 RVA、PE 机器/映像大小、目标 PID/TID/创建时间与 Desktop、真实 API 结果/错误。两份导出的 RVA 均为 31280，实际地址随加载基址记录，未用预设地址填写。WM_NULL 被原目标消息循环实际分发；这不能证明受管键盘、IAT 虚拟化或原业务输入已通过。加载路径与导出归属是**安装助手**现场，不冒充目标进程加载内存完整身份或历史失败时的现场。

[dll-comparison.json](dll-comparison.json) 表明两份 DLL 当前磁盘 SHA256 与历史失败后观测相同，分别为 2379ba82f7c1f55c1d1e9fd6dc079c0308142062882aaf8795760f041032ae7b 和 b47c20bc158f8e0c03b042f8ef3ab7937eab293babe023bf53c3eb6c5bd2654a。没有替换 DLL；但哈希相同不能倒推出旧调用那一刻的依赖、加载器或钩子安装状态。

## 开发红项和构建对应关系

初版探针 b001-1 中，加载/导出/安装本身成功；监督器从另一 Desktop 投递退出消息却得到 **PostThreadMessageW.probe_quit / 1444**。这是新增诊断胶水的错误，原 summary 的 cleanupVerified=false 原样保留。RAII 尝试回收自有资源不能补写成当时已获得独立清理证明；它更不是旧 /126 的复现。

随后由同 Desktop 的安装助手在卸钩后投递 WM_QUIT，build2 对两份 DLL 各一次成功并独立确认退出。最终 build3 又补严卸钩失败时不主动释放模块引用、强制收尾不得算正常通过的判定，再对两份 DLL 各一次确认；不是失败恢复的自动重试或为重现自然故障增加压力次数。原始目录分别是 b001-1、b001-final、cet002-final、matrix-final；**共五次探针尝试，只有最后两次属于最终构建矩阵**。初版源码未单独冻结，保留其 build.json 中的源码哈希及实际二进制/PDB，不声称最终源码就是初版失败源码。

## 旧原程序的单场景对照

另仅执行一次现存、未重建的 r3-005b-001/out/r305a.exe --role controller --scenario positive。退出 0；experiment_complete 的 failedAssertions=0、执行续接、原生控件重建和恢复后输入均为 true；[旧生命周期只读审计](original-b001-lifetime-audit.json) 通过。见 [原始调用结果](original-b001-outcome.json)。

这是单个旧正例，不是完整 R3-005b/CET-002 或嵌套旧回归重跑。原代码没有本探针新增的安装现场字段，因此不把探针现场数据拼成旧程序调用时的证据。历史 40 个 /126 和缺少退出记录的原审计仍为失败，不覆盖、不改写。

## 清理、证据与结论

最终两例均确认助手和目标正常退出，Job 为空，自有句柄关闭，CloseDesktop 后实际 OpenDesktop 返回不存在；没有强杀掩盖超时。最终安装失败/卸钩失败分支本轮未触发，不能据此宣称这些自然故障已验证修复。初版胶水失败单独保留。

[build.json](build.json) 记录最终源码/EXE 哈希和安全编译参数；[raw.tar.xz](raw.tar.xz) 包含所有尝试、原程序单正例、安装现场、监督日志、初版/中间构建身份及二进制，[清单](raw.tar.xz.manifest.json) 逐文件长度和 SHA256 均从解压流验证。[tested-package.tar.xz](tested-package.tar.xz) 与[清单](tested-package.tar.xz.manifest.json) 保存最终 EXE/PDB 和精确源码/构建脚本；没有打包系统 DLL 或声称依赖路径在另一机器上必然有效。

公开接口依据：[SetWindowsHookExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw)、[GetModuleHandleExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw)。它们用于选择观测位置，不代替本机实际结果。

[summary.json](summary.json) 保持 rootCauseConfirmed=false、causalFixVerified=false、fullLegacyRegressionRerun=false、fullR5004Passed=false、gateClosed=false。结论仅是本轮安装现场与一个旧正例成功，历史拒绝原因仍未知；不进入 R5-005。
