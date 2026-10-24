# R2 首轮：并存分支、内存别名及普通文件

R2-001（内存）先实际通过，再加入 R2-002（受管文件）。最终验证见 [报告](results/20261024-r2/report.md)、[R2 汇总](results/20261024-r2/suite-final/summary.json) 和 [R1 回归汇总](results/20261024-r2/r1-regression/summary.json)。这是 R2 的受限子集，R3/R4 未开展。

## 构建与测试

沿用 R1 的 MSVC x64、MASM 和 Windows SDK 工具链：

```powershell
.\experiments\r2\build.ps1
.\experiments\r2\test.ps1
```

非标准安装可指定 `-MsvcRoot`（包含 bin/include/lib 的 MSVC 版本目录）、`-SdkRoot` 和 `-SdkVersion`。可用 `-OutDir` 改产物目录；测试脚本另接受 `-ResultsDir`。文件 case 必须使用新目录，已存在的资源目录会拒绝覆盖。

单独运行：

```powershell
.\experiments\r2\out\controller.exe --fixture "$PWD\experiments\r2\out\fixture.exe" --case memory
.\experiments\r2\out\controller.exe --fixture "$PWD\experiments\r2\out\fixture.exe" --case files --workdir "$PWD\experiments\r2\out\fresh-files"
```

R1 回归仍用 `experiments/r1/build.ps1`、`experiments/r1/test.ps1`。两个实验仅共用 `experiments/common/native.hpp` 的错误、句柄、PE 导出定位、摘要和原始内存访问工具；R2 独立实现统一事件泵，没有复用 R1 的单目标事件等待器。

## 行为与边界

- 控制器捕获原始代码、数据、私有栈和整数/控制上下文，源进程终止后派生 A/B。程序以双指针别名读写同一内存对象，多次接收输入并继续执行。
- 单个创建/调试线程按 PID/TID 分发事件，各分支保留自己的停点、预期事件、期限、发布和退出状态。A 的结果在 B 创建期间被分发；操作和清理没有跨 PID 消费事件。
- 输入区仅传递命令、参数与写入字节；业务值、步数和计算结果由汇编执行产生。恢复直接续接 S0，不重跑源初始化。
- 文件入口是明确的服务断点及请求区。运行时在控制器中持有原生文件对象，按分支解释逻辑句柄，使用真实 Win32 文件 API 返回结果，由受管程序继续处理。
- 普通文件按 `逻辑句柄 → 打开实例 → 分支私有文件版本` 保存。S0 含一个文件、两个打开实例、三个逻辑句柄：h1/h2 共享位置 2，h3 独立位于 6。
- 在受管服务均已完成的安全点捕获文件字节、打开参数、位置和关系。恢复完整复制文件，按打开实例调用 CreateFileW，再用 DuplicateHandle 建立实例内别名。
- 文件大小上限 4096 字节，单次传输上限 16 字节，仅支持受管目录中的普通同步磁盘文件；Open/Read/Write/Seek/Tell/Duplicate/Close。不支持异步、映射、锁、设备和未知资源/标志。
- S0 只在控制器内存中。固定地址、同机同构建、每分支单受管线程；不恢复普通启动栈、TLS、SIMD、影子栈或任意原生 HANDLE。不扫描内存修补句柄。
- 这是协作式受管入口实验，不代表能透明恢复未改造应用，也不声称发现了入口之外的所有资源。
- 正向实验在最终观察点保留 A/B/C 同时存活，再进行额外操作验证；测试结束时统一清理进程和原生句柄，磁盘文件作为证据保留。C 仅检验 S0 固定性，没有成果导入或合并。

## 预期退出码

| case | 退出码 | 判定 |
| --- | --- | --- |
| memory / files | 0 | 正向实验成功 |
| shared-open | 2 | B 错误共享 A 打开实例，B 位置串扰至 4 |
| split-alias | 2 | A.h1/h2 分别重开，h2 未随读取前进 |
| omit-position / omit-content | 2 | 恢复位置或内容不匹配 |
| partial-create | 3 | B 中途失败；未发布、资源回收，A/C 继续正常工作 |
| unsupported-resource | 3 | 拒绝标为不支持的快照资源；A/S0 不受影响 |
| reject-async/map/lock/device/unknown | 3 | 受管服务入口明确拒绝对应请求 |

`harnessPassed=true` 表示符合预期；失败或拒绝的 case 始终保留非零退出码及 `experimentSucceeded=false`，不能当作恢复成功。
