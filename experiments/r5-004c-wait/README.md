# R5-004c-wait：从 recorder 裁剪文本服务等待

本实验没有提供清理修复。普通 WM_CLOSE 与停止点清理均有自然失败；WCT COM 回调把旧的等待边延伸到了 COM 对象及仅有 PID 的 RPCSS 终点。服务端线程和未完成操作仍未知，门禁保持 false。

新增 [完全不安装清理运行时的正常关闭对照](results/no-cleanup001/report.md)。test-no-cleanup.ps1 串行对比 profile-normal / normal-no-cleanup-runtime；后者根本不构造 AbortRegistry、不调用 prepare_cleanup，且记录清理助手为零与目标 DLL 未加载。ready、resource-ready、安装完成/跳过后分别记录实际线程实例和已知模块。本次两组均正常关闭，但系统在本轮前已重启，历史等待未复现，不能宣布因果修复。预留 -DirectWindow 后续裁剪本轮未执行。

~~~powershell
./experiments/r5-004c-wait/test-no-cleanup.ps1 -OutDir <同一构建目录> -Profile <固定resource.bin> -ResultsDir <全新目录>
~~~

沿用同一个四帧 recorder EXE、后台 Desktop、EDIT/BUTTON、受限窗口资源适配器与原 inline 清理 DLL；不再改变异常清理入口。裁剪分两步：先只消费 S0 的窗口资源描述，去掉执行状态恢复和成果编排；再导出独立资源 profile，测量管理器不读取任何 Session/Snapshot/Artifact，只建立新的原生窗口。它是清理诊断，不是执行恢复证明，也不宣称已经找到全局最小程序。

~~~powershell
# 先按既有说明准备 R5-004b 的构建输出和 MSVC/SDK。
./experiments/r5-004c-wait/build.ps1 -OutDir C:/fv-c/new-build
# 资源提取是单独的准备操作，不把它记为执行恢复。
C:/fv-c/new-build/profile.exe --snapshot <已验证的S0.fvs> --fixture C:/fv-c/new-build/second.exe --output <新的resource.bin>
./experiments/r5-004c-wait/test.ps1 -OutDir C:/fv-c/new-build -Profile <resource.bin> -ResultsDir C:/fv-c/new-pair
~~~

脚本顺序执行 normal/checkpoint，使用同一构建和 profile，每组独立 Desktop/Job/所有权记录。不随机睡眠制造故障，不重试失败操作，不改系统策略。原生窗口关闭仍是 5 秒判据；正常关闭探针位于目标 Desktop，5500ms 是该探针签退期限，不增加原生关闭期限。超时后诊断助手最多 1500ms 加 500ms 签退，不改变已经固定的失败。

原监督器的 save 协议入口在 profile 模式中仅用于创建受监督进程，不发布 Session 或 Snapshot。脚本仍保留 nativeCleanupPassed=false 并退出非零；diagnosticSequenceCompleted=true 只表示记录和独立物理收尾完成。不能把最终强杀或 owner.fvc 的物理清理证明写成原生清理成功。

diagnose.exe 只暂停/取栈自有进程线程；WCT 按微软公开示例注册 OLE32 COM 回调，在同一自有进程中继续查询被等待的工作线程。跨进程节点只读取 WCT 返回值，没有暂停、注入、终止系统服务或提升权限。OLE32 引用保持到诊断进程结束；所有符号类型、原始错误和超时分别保存。

实际结果见 [报告](results/wait001/report.md)。历史 PID 误判与首次正常关闭探针错误分开保留；旧清理候选不升级为修复。不进入 R5-005。
