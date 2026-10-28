# 旧线程输入钩子 /126 的独立安装探针

只诊断文件定位、实际加载模块、导出归属和 SetWindowsHookExW 安装阶段；不修改旧输入实现、不替换旧 DLL，不证明完整输入虚拟化或关闭等待已修复。

~~~powershell
./experiments/r5-hook-126/build.ps1 -OutDir C:/fv-c/new-hook-build
./experiments/r5-hook-126/test.ps1 -OutDir C:/fv-c/new-hook-build -ResultsDir <全新目录>
~~~

默认各运行一次旧 R3-005b-001 和 CET-002 的 runtime304.dll。可用 -Dll 指定明确的路径列表；每个操作只尝试一次，输出必须是新目录。构建默认读取 wait001/build.json 的 MSVC/SDK 路径，可用 -Config 指定其他相同字段的本地配置。C++20、Windows SDK、无第三方运行依赖，保留 /GS /guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT。

单个探针 EXE 分监督器、目标窗口、安装助手三角色。目标和助手在独立 Desktop、同一自有 kill-on-close Job 中，挂起创建后先归 Job 再 ResumeThread。安装助手核对目标 PID/TID/创建时间、窗口归属、存活及 Desktop；记录 DLL 参数路径及磁盘 SHA256、实际 GetModuleFileNameW、HMODULE、GetProcAddress 地址、PE 信息及导出所属模块，随后仅安装到该线程的 WH_GETMESSAGE。

WM_NULL 只用于确认目标消息循环仍能处理消息，不提交业务输入、IAT 替换或自定义状态。卸钩失败不主动释放安装助手的 DLL 引用，留到助手进程结束；原生失败、卸钩和资源签退各自保留。监督器确认两实例退出、Job 为空、自有句柄关闭以及 Desktop 不存在；强制收尾不计为正常安装路径通过。异常早退没有独立清理证明时 summary 保持 cleanupVerified=false。

实际结果见 [hook126001/report.md](results/hook126001/report.md)。历史 /126 没有在本轮复现，不能因本次成功推断旧失败时缺的是哪个模块，也不宣称旧完整回归全绿。
