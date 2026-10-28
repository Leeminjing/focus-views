# 完全不安装清理运行时的正常关闭对照

2026-10-28；源码基线 e57d2c6d2b48f07e5a354f3b840e06f3102fe36d，加本目录 build.json 列出的实验修改。**本次两组正常关闭，历史等待没有复现；清理因果修复未验证，完整 R5-004 仍未通过。**

## 实际运行

Windows 11 x64 10.0.26200，普通用户；MSVC 19.44.35229.0、Windows SDK 10.0.18362.0。本轮观察到系统启动时间为 **2026-10-28 19:42:46.5 +08:00**；历史 wait001 自然失败属于 08:51:50.5 启动周期。实验没有重启系统、改变服务、IME、CET/CFG/GS 或宿主输入。两轮不能合并成同一启动周期的修复对照。

同一构建串行运行一组 profile-normal 与一组 normal-no-cleanup-runtime，保持 recorder、Desktop 创建机制、固定资源 profile、资源重建及正常 WM_CLOSE 探针不变。无钩子模式根本不构造 AbortRegistry，核对新管理器的回调为空，且不调用 prepare_cleanup；不是仅切换 inline/deferred。正式停止点清理实现、PID 修复和恢复内核没有修改。

| 实际观测 | 安装清理运行时 | 完全不安装 |
|---|---:|---:|
| AbortRegistry / prepare_cleanup | true / true | false / false |
| 自有清理助手创建记录 | 1 | 0 |
| ready 时已观察线程数 | 4 | 4 |
| resource-ready 时已观察线程数 | 6 | 6 |
| 安装完成／跳过后线程数 | 9 | 6 |
| 最后一次目标模块枚举中的 abort-runtime.dll | 1 | 0 |
| WM_CLOSE 后探针确认 HWND 消失 | 通过 | 通过 |
| 原监督器进程、Job、自有句柄、Desktop 证明 | 通过 | 通过 |

原始数据见 [pair-summary.json](pair-summary.json)、[逐阶段线程和模块](stages.json)。FILETIME 保留整数精度。resource-ready 时，两组都已经观察到新增线程以及 msctfmonitor.dll、ole32.dll；无钩子组跳过安装后线程实例和已知模块集合未变化。安装组随后多出三条被观察线程以及 abort-runtime.dll。**这些阶段差异不是线程用途、初始化开始/完成或触发因果的证明。** 模块 SHA256 是所报告路径的磁盘文件哈希，不冒充加载内存逐字节身份。

正常关闭探针在所属 Desktop 核对 PID/TID/创建时间后投递 WM_CLOSE，原来的 5000ms 判据未放宽。原始 normal-close.jsonl 记录 windowAbsent=true、processStillAlive=true、destroyWindowReturnNotObservedDirectly=true：证明窗口在期限内消失，**没有直接观测 DestroyWindow 的返回值，也没有把这一时刻称为进程退出**。之后原监督器分别完成所有实例和物理资源签退确认。没有用最终终止来覆盖先前关闭超时；这两组没有该超时。

## 判定和没有执行的部分

这是补齐“完全不安装”的诊断对照，不是修复。两组本次都成功，未取得相同等待关系的新反例，不能选择“安装后才失败”或“两组均失败”的后续分支。预留的 normal-new-window-direct 模式本轮**未执行**，不据此推断资源重建必要性；未继续修改清理入口或服务端，也没有扩跑清理/异基址/崩溃组合矩阵、完整旧回归或人工宿主操作。

[summary.json](summary.json) 将本轮局部正常关闭成功与历史清理门禁分开。nativeCleanupPassed、cleanupCausalFixVerified、fullR5004Passed、gateClosed 在历史故障关口上均保持 false；pair-summary 内的 nativeCleanupPassed=true 仅指本次两组正常关闭。历史八个自然失败、原探针错误及旧回归红项不覆盖。

## 构建、复现与原始字节

~~~powershell
./experiments/r5-004c-wait/build.ps1 -OutDir C:/fv-c/new-control-build -ReuseCandidate <既有同版本候选构建>
./experiments/r5-004c-wait/test-no-cleanup.ps1 -OutDir C:/fv-c/new-control-build -Profile <固定resource.bin> -ResultsDir <全新目录>
~~~

本次实际根目录 C:/fv-c/nh，构建 build，串行测试 pair1；来源 profile 为 C:/fv-c/wait/profile-normal/store/resource.bin。profile SHA256 为 ad3dff2b35009257a3298355a1ef54b85d9b1f9a21ac59b24c9393c51b6b4dbb。构建继续使用 /GS、/guard:cf、/CETCOMPAT、/DYNAMICBASE、/NXCOMPAT。源码身份及复用内核/应用二进制身份见 [build.json](build.json)。

[raw.tar.xz](raw.tar.xz) 保存两组原始 JSONL、监督器所有权与清理记录、resource.bin、构建/测试日志和阶段导出；[清单](raw.tar.xz.manifest.json) 逐文件长度与 SHA256 均从 TAR 解压流核对。[tested-package.tar.xz](tested-package.tar.xz) 保存实际运行的二进制/PDB、精确实验源码及构建材料，[清单](tested-package.tar.xz.manifest.json) 同样核对。未改依赖由源码基线和原构建材料绑定，不把事后文档编辑写成已测试源码。二进制保护规则沿用 results/** -text。
