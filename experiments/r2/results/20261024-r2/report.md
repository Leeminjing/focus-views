# R2-001 / R2-002 首轮实验报告

日期：2026-10-24（Asia/Shanghai）。基于 `893b8c8` 开发，源码与二进制 SHA256 见 [最终 R2 汇总](suite-final/summary.json)。实际先完成内存实验，再加入文件；最终套件在当前构建上分别重跑两条正向链路。

**结论：本机受控单线程片段可以从同一 S0 派生并保留 A/B，分别修改内存和通过受管入口访问的普通文件；A/B 存活时派生的 C 保持原始起点，三者均可继续运行。** 仅声明这个 R2 子集，不代表完整 R2 或通用 Windows 应用恢复。

## 环境和构建

Windows 11 家庭版 x64，10.0.26200；Intel Core i9-13900HX；非管理员。MSVC 19.44.35229、MASM x64、Windows SDK 10.0.18362.0。C++20，`/O2 /MT /W4 /WX`，链接 `/DYNAMICBASE /NXCOMPAT /CETCOMPAT`。未修改系统安全策略。

最终构建标识 `609e59815281d106aad265caef79a0708760d52c048ce1a5b0b731356753b1b1`。实际构建输出见 [build.log](build.log)，静态链接运行库；[原生依赖](dependencies.txt) 只有 KERNEL32 和 bcrypt。[反汇编](workload-disassembly.txt) 显示受管片段没有调用/返回或导入引用；文件请求通过明确 INT3 服务点交给运行时。

各分支查询到影子栈策略原始标志 `0x105`、动态代码策略 `0x0`。没有关闭保护；实验仍不覆盖影子栈恢复。旧 SDK 的策略索引查询方式沿用 R1。各程序及共享源码的哈希、权限与工具链路径均在汇总中。

## R2-001：持续运行和内存关系

见 [memory.log](suite-final/memory.log)。源 PID 20924 在派生前确认终止，S0 固定内存值为 `0x52d9433b86b4802d`。两个指针都指向受管数据对象；汇编通过第一个指针写入、第二个指针读取。

| 时刻 | A（PID 39788） | B（PID 43444） | C（PID 40800） |
| --- | --- | --- | --- |
| A/B 分别操作两次后 | 初值 +18 | 初值 +118 | 尚未创建 |
| 从 S0 创建 C 后 | 保持 +18 | 保持 +118 | 原始值，步数 0 |
| 三者再次运行后 | 初值 +19 | 初值 +120 | 初值 +3 |

创建 B 时 A 已在执行。统一事件泵将 A 的结果分发给 A，并在 B 发布前确认该事件；随后 A/B 的操作同时在途，C 创建后也再次向三者投递输入。各进程在最终观察时同时存活。

源初始化/检查点各 1 次；A/B/C 均为 0 次，最终结果事件分别为 4/4/2 次。寄存器和栈继续参与初始续接计算；后续状态由程序执行改变。S0 原始区域与上下文指纹在 C 创建前后均为 `2f45c873678ac0844beb5f8fa9d738a44c3380199ec2e90e9e3d7da09da69652`。

## R2-002：真实文件版本和打开实例

见 [files.log](suite-final/files.log)。源 PID 13788 通过受管入口调用真实 CreateFileW、DuplicateHandle 和 SetFilePointerEx，形成以下关系，然后在无进行中服务的检查点捕获：

```text
固定文件内容：0123456789
h1=1 ─┐
      ├─ 打开实例 1，位置 2 ─┐
h2=2 ─┘                     ├─ 文件 0
h3=3 ─── 打开实例 2，位置 6 ─┘
```

恢复给每个分支建立私有文件副本，再按实例重开并定位，最后复制实例内的句柄引用。真实原生句柄由运行时持有，程序中的逻辑 ID 原样恢复；没有扫描或改写疑似 HANDLE 的整数。

实际 A/B/C PID 分别为 **33476 / 15264 / 31472**：

1. A.h1 读出 `23`，A.h2 查询位置为 4；A.h3 保持 6，B.h2 保持 2。
2. A.h2 写入 `AA` 后，A 为 `0123AA6789`，B 仍为原内容。
3. B.h2 移动到 7 并写入 `BB`，B 为 `0123456BB9`，A 不变。
4. A 关闭 h1 后，h2 继续读出 `6`，位置到 7；B 的位置仍为 9。
5. A/B 均保留且已修改内存和文件，再创建 C。C 的内存为原始 `0xa5cafa8f27cfadbb`，文件为 `0123456789`，h1/h2 位置 2、h3 位置 6。
6. C.h1 也读出 `23`，C.h2 随之前进到 4，说明原别名关系仍成立；C 写入 `CC` 后，A/B 保持各自内容。三个原生文件 ID 不同。
7. A/B/C 再次执行内存命令，最终值分别为原始值 +19/+120/+3，最终结果事件分别为 15/13/8 次；三者仍同时存活，然后才进入测试清理。

文件字节被保留为 [source](suite-final/files-files/source/file-0.bin)、[A](suite-final/files-files/A/file-0.bin)、[B](suite-final/files-files/B/file-0.bin)、[C](suite-final/files-files/C/file-0.bin)，测试脚本在控制器退出后独立读取并核对内容。最终内容依次为 `0123456789`、`0123AA6789`、`0123456BB9`、`0123CC6789`。

S0 内存/上下文指纹在操作前后相同；固定文件内容和资源图指纹一直为 `1b5626aaca40ae3beaabee52a816665f173dda550a8d28171cc5de7965074c30`。所有恢复分支的初始化和原始检查点事件均为 0。

## 失败与拒绝证据

| case / 原始日志 | 退出码 | 实际检测 |
| --- | --- | --- |
| [shared-open](suite-final/shared-open.log) | 2 | 故意给 B 复制 A 的打开实例，B.h2 位置为 4，违反预期 2 |
| [split-alias](suite-final/split-alias.log) | 2 | 故意独立重开 A.h1/h2，A.h2 仍为 2，违反预期 4 |
| [omit-position](suite-final/omit-position.log) | 2 | 未定位时为 0，违反预期 2 |
| [omit-content](suite-final/omit-content.log) | 2 | 故意用零字节恢复内容，无法读出 `23` |
| [partial-create](suite-final/partial-create.log) | 3 | B 已复制文件并建立一个原生打开后失败，未发布，文件与句柄回收 |
| [unsupported-resource](suite-final/unsupported-resource.log) | 3 | 给 B 的资源清单注入不支持标记，资源写入前拒绝 |
| [异步](suite-final/reject-async.log) / [映射](suite-final/reject-map.log) / [锁](suite-final/reject-lock.log) / [设备](suite-final/reject-device.log) / [未知](suite-final/reject-unknown.log) | 各 3 | 受管入口拒绝相应请求，ERROR_NOT_SUPPORTED / 50 |

B 创建失败的两条路径都有独立断言：没有 `VIEW_PUBLISHED role=B`；确认 B 的退出事件和进程句柄已终止；DeleteFileW 成功删除本次创建的精确文件，部分目录为空；A 继续进行多轮内存计算及真实文件读取，C 从未变的 S0 恢复出原位置与关系。拒绝后再确认 A/C 同时存活。删除不使用递归或全局进程清理。

最终 13 项检查：2 条正向实验成功，11 条错误或不支持情形按预期失败/拒绝。负向项在 summary 中始终为 `experimentSucceeded=false`。所有创建 PID 均有对应终止确认，套件后残留 fixture 进程为 0。

## R1 回归和代码范围

只把 R1 的底层工具移到实验内部 `common/native.hpp`，保留 R1 单目标恢复链路；构建脚本增加源码目录参数供 R2 复用，并记录共享源码哈希。没有公共 SDK 或稳定快照协议。

R1 重新构建并运行原有 9 项检查，全部符合预期，见 [R1 构建日志](r1-build.log) 和 [R1 回归汇总](r1-regression/summary.json)。早期 memory-first/files-first/suite-1 日志保留开发过程的实际运行；本报告的最终断言以 suite-final 为准。

## 未覆盖

固定地址、同机同构建、每分支一个受管线程；原始数据和私有执行栈，不是普通应用堆或通用对象图。文件仅为受管服务入口内的普通同步磁盘文件，最大 4096 字节、每次请求最多 16 字节；打开参数和共享模式也限定为实测组合。

文件权限、时间戳、锁、映射、异步 I/O、任意路径重定向、外部修改者、未改造应用的直接 Win32 调用、第三方进程资源发现、TLS、SIMD、普通调用栈和影子栈恢复均未验证。S0 在控制器内存中，不支持跨控制器退出或宿主重启。拒绝保证仅覆盖已进入受管资源入口的请求和资源清单。

C 用于证明固定起点，不涉及 SA/SB 的持久化、Artifact 导入或合并。没有推进 R3/R4，也不将整个 R2 标为完成。
