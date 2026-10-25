# R5-001b：同一恢复内核管理第二个协作应用

本实验验证跨应用复用，不增加控件、Undo、键鼠或持久化能力。旧 R5-001a 的源码与历史证据不变。

## 模块与不变项

| 模块 | 职责 |
| --- | --- |
| `manager.exe` | 一个薄启动器，加载显式选择的实验客户端；两款应用使用完全相同的二进制 |
| `clients/host.hpp` | 一份共享的实验编排：统一调试事件泵、S0/A/B/SA/SB/RA/RB/C、输入、Frame、Action、Artifact |
| `fixture-client.dll` | 原 fixture 的只读布局解码和计算 oracle |
| `recorder-client.dll` | 数据记录器的只读布局解码和另一套计算 oracle |
| `recorder/` → `second.exe` | 独立实现的原生数据记录器，采样数组、计算、窗口内容及 CSV 文件处理 |
| R5-001a `kernel.lib` | 原样链接，负责执行捕获/恢复、目标 PE unwind、CET 返回关系及资源引用核对 |
| R5-001a `target.lib`、helper、输入 DLL | 逐字节复用已有资源服务与输入/采集实现 |

本实验的新 manager 与 R5-001a 的 manager 不同：原静态 fixture 验收客户端被移到显式选择的 DLL 中。**不是宣称旧 manager 已无需变化即可装载任意应用**。两种新客户端均链接同一份未重编译的旧 `kernel.lib`；内核没有增加记录器分支。`reuse.json`、链接分组和实际二进制哈希用于核对这条边界。

`Observation` 仅是实验报告的只读 DTO，不是目标内存布局，从不写回进程。原 fixture 保持原 Work；记录器使用不同的 `State`、八项 `Sample`、独立数据地址和 CSV。原始数据页、栈与上下文仍由内核保存恢复，不能用 DTO 代替。

## 第二应用的协作范围

记录器自行生成随机 run ID 与八项采样，原生 EDIT 保存标签，BUTTON 经真实 Win32 文件 API 导出 `records.csv`。后续应用计算使用样本值、三个栈局部量和 R10，其公式不同于原 fixture。它还实现了读取两份私有 CSV、生成合并结果的应用逻辑。

两款应用均显式提供既有执行 ABI、七个 HWND 引用、单 GUI 线程和编译期续接入口。记录器的函数为 `emit → fold → sample → CheckpointTrap`；新载体通过相同 CALL 位置建立自己的 CET 链，跳过业务初始化和前缀后，写回 S0/SA/SB 的真实执行状态并正常 RET。不是未改造应用兼容。

资源范围仍是一个顶层窗口、一个单行 ASCII EDIT、一个 BUTTON、一次全选字符替换的 Undo，以及指定工作区内已关闭的同步普通文件。原 EDIT/BUTTON 的 ID 为 101/102，记录器为 201/202；窗口类、标题、业务内存布局和文件格式不同，但仍使用同一已支持资源子集。

四帧/六返回位置的**声明不兼容**由实验客户端在启动契约核对时拒绝；不是声称内核支持四帧，也不是运行后自动推断任意调用栈。未知必要资源另由既有运行时 ABI 拒绝。

## 构建与复现

先按 R5-001a 的构建说明生成其 `out/` 原件，然后执行：

```powershell
New-Item -ItemType Directory -Force -Path ./experiments/r5-001b/out | Out-Null
& ./experiments/r5-001b/build.ps1 -MsvcRoot <MSVC-root> -SdkRoot <Windows-SDK-root> -SdkVersion <version> *> ./experiments/r5-001b/out/build.log
& ./experiments/r5-001b/test.ps1 -ResultsDir D:/new-results -Repeats 2
& ./experiments/r5-001b/package.ps1 -ResultsDir D:/new-results

$bin = 'D:/focus-views/experiments/r5-001b/out'
& "$bin/manager.exe" --client "$bin/fixture-client.dll" --fixture "$bin/fixture.exe" --output D:/new-fixture-case --scenario positive
& "$bin/manager.exe" --client "$bin/recorder-client.dll" --fixture "$bin/second.exe" --output D:/new-recorder-case --scenario positive
```

路径按正常文件路径解析；输出目录必须不存在。客户端是自有、显式选定的实验代码，不是安全插件沙箱或稳定 SDK。helper/输入 DLL 从 manager 所在目录加载。

正式矩阵：原 fixture 十个场景、记录器十一个场景，各两轮；另有错误客户端契约拒绝。默认完整运行旧 R5-001a 测试及其 R4-003 嵌套回归。`-SkipRegression` 仅用于开发诊断，不能记为回归通过。

校验独立读取原始 Snapshot、栈局部量、CONTEXT 和文件，分别计算两款应用的结果；核对 CET、初始化/业务前缀计数、仅七处登记重关联、原生 Undo、文件对象、Frame 像素与目标绘制记录。负例成功检测与执行恢复通过分开统计，不重试失败操作，不改用宿主全局输入。

首次矩阵发现一个真实前置条件：新通道中尚未被受管事件接管的修饰键，可能在原生队列表中已有按下位。一个 C 因此把 Home 当作 Shift+Home，最终只替换了部分文本；该失败整套结果保留。共享编排现先提交只读 barrier 取得目标线程原生状态，再通过现有输入入口发送左右 Ctrl/Shift 的 key-up 和 release，等待确认并核对原生查询已松开，才允许业务输入。该握手同时用于两款应用，不改变 DLL、查询返回值或 EDIT 业务结果。检查点仍要求无未完成受管输入，不支持保存“按键仍持有”的交互现场。

结果与限制以 [最终报告](results/20261025-r5001b-neutral/report.md) 为准；[首轮失败](results/20261025-r5001b/report.md) 单独保留。可靠性 `CreateProcessW / 5` 门禁继续独立保留；人工宿主交互、任意程序、动态执行 ABI、多线程及跨管理器重启恢复不在本轮验收内。
