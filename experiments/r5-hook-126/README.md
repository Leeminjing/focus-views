# 旧线程输入钩子 /126 的独立安装探针

最初的独立探针只诊断文件定位、实际加载模块、导出归属和 SetWindowsHookExW 安装阶段。新增显式真实路径诊断见 [real001](results/real001/report.md)：通过构建开关在旧 input.cpp 的实际 API 调用点取证，不改变输入/恢复机制。两种实验分别记录二进制身份，不把诊断构建称为旧二进制原样运行；均不证明历史关闭等待或 /126 已修复。

最新 [失败路径端到端验收](results/failure001/report.md) 复用已有 blocked:，通过 -BlockedCleanup 运行原失败处理、证明核对、一次独立关闭对照及缺少证明的实际入口拒绝。原失败不改绿，诊断链单独 assessment。新增 -QueryFailureOutDir 指向用 -InstallDiagnostics -DiagnosticQueryFailure 编译的独立 b001：只令附加 GetProcessTimes 查询实际失败，不改安装 API。默认构建不启用该测试；另一个真实负例在私有目录有意缺少 DLL，不能称为历史钩子 /126 复现。

~~~powershell
./experiments/r5-hook-126/test-real.ps1 -InputOutDir <正常b001诊断构建> -CetOutDir <CET诊断构建> -ControlOutDir <完整wait构建> -Profile <resource.bin> -BlockedCleanup -QueryFailureOutDir <显式取证失败构建> -ResultsDir <全新短目录>
~~~

test-input-failures.ps1 检验原始主错、真实助手退出和仅取证失败；test-missing-proof.ps1 仅删私有副本证书，再调用实际主入口 -FollowupOnlyEvidence，禁止发布或重试原 Snapshot。等待分类改用关联查询及实例创建时间，材料不足不触发 direct-window。最终入口已实跑通过，历史根因、完整 R5-004 和可靠性门禁未关闭。

## 真实路径诊断（独立探针无需重复运行）

~~~powershell
# 两款原程序全部构建完成后才开始串行测试，不覆盖原 out。
./experiments/r3-005b-001/build.ps1 -InstallDiagnostics -MsvcRoot <MSVC> -SdkRoot <SDK> -SdkVersion <版本> -OutDir <新b001目录> *> <新b001目录外构建日志>
./experiments/r3-005a-cet-002/build.ps1 -InstallDiagnostics -MsvcRoot <MSVC> -SdkRoot <SDK> -SdkVersion <版本> -OutDir <新CET目录> *> <新CET目录外构建日志>
# 将各自完整构建日志复制为构建目录/build.log，旧套件会保留它。
./experiments/r5-hook-126/test-real.ps1 -InputOutDir <b001目录> -CetOutDir <CET目录> -ControlOutDir <既有完整wait构建> -Profile <resource.bin> -ResultsDir <全新短目录>
~~~

FOCUS_VIEWS_INPUT_INSTALL_DIAGNOSTICS 默认不定义，仅诊断构建的 input 翻译单元启用。原 LoadLibraryW/GetProcAddress/两个线程 SetWindowsHookExW 的返回和错误先保存，再做观测，原生错误按保存值传播。附加取证失败只记录 diagnostic_failure，不替换主错误；不预加载 DLL、改变搜索路径、安装全局钩子或重试。助手/目标创建时间、Desktop、模块路径、磁盘哈希和导出归属来自现场；模块哈希不冒充内存全镜像证明。卸钩/FreeLibrary 的原始结果也记录，保留原清理语义。

test-real.ps1 先运行原 recorder relocation-bad-offset 路径，再运行真实旧程序正例及两组预构建套件。只有原清理失败已固定、监督器确认原自有资源全部清理、启动周期未变时，才运行一次独立的安装/完全无清理运行时关闭对照；两组都保留相同等待关系且物理收尾确认后，才允许一次 direct-window 诊断。它们不是 Snapshot 重试，后续成功不覆盖原失败。安全判据见 followup.ps1 与纯人工 test-followup.ps1。

诊断套件显式传 -InstallDiagnostics，先核对构建开关、实际 input/header 哈希，再仅为这处已审查源码增加基线白名单；旧恢复、控件、图像和生命周期断言保留。b001 的嵌套回归需 -SkipRegression 后独立执行已预构建的 CET 套件，禁止测试期间覆盖依赖。test-suites.ps1 可单独执行这两套，不能把它称为完整 R5 嵌套回归。audit-real.py 只读检查真实旧输入日志，不启动新实验。

~~~powershell
./experiments/r5-hook-126/build.ps1 -OutDir C:/fv-c/new-hook-build
./experiments/r5-hook-126/test.ps1 -OutDir C:/fv-c/new-hook-build -ResultsDir <全新目录>
~~~

默认各运行一次旧 R3-005b-001 和 CET-002 的 runtime304.dll。可用 -Dll 指定明确的路径列表；每个操作只尝试一次，输出必须是新目录。构建默认读取 wait001/build.json 的 MSVC/SDK 路径，可用 -Config 指定其他相同字段的本地配置。C++20、Windows SDK、无第三方运行依赖，保留 /GS /guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT。

单个探针 EXE 分监督器、目标窗口、安装助手三角色。目标和助手在独立 Desktop、同一自有 kill-on-close Job 中，挂起创建后先归 Job 再 ResumeThread。安装助手核对目标 PID/TID/创建时间、窗口归属、存活及 Desktop；记录 DLL 参数路径及磁盘 SHA256、实际 GetModuleFileNameW、HMODULE、GetProcAddress 地址、PE 信息及导出所属模块，随后仅安装到该线程的 WH_GETMESSAGE。

WM_NULL 只用于确认目标消息循环仍能处理消息，不提交业务输入、IAT 替换或自定义状态。卸钩失败不主动释放安装助手的 DLL 引用，留到助手进程结束；原生失败、卸钩和资源签退各自保留。监督器确认两实例退出、Job 为空、自有句柄关闭以及 Desktop 不存在；强制收尾不计为正常安装路径通过。异常早退没有独立清理证明时 summary 保持 cleanupVerified=false。

实际结果见 [hook126001/report.md](results/hook126001/report.md)。历史 /126 没有在本轮复现，不能因本次成功推断旧失败时缺的是哪个模块，也不宣称旧完整回归全绿。
