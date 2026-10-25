# R5-001a：独立 manager 与显式协作应用

本实验从 R4-003 拆出恢复内核，分别构建 `manager.exe`、`fixture.exe`、`helper.exe`。manager 不再具有 fixture 角色，不链接 `app/fixture.cpp`、业务文件逻辑或检查点汇编；fixture 不链接管理器和调试事件泵。旧 R4-003 源码及证据保持不变。

本轮仍只验证原来的单线程、固定布局、编译期协作调用链、标准单行 EDIT/BUTTON、单次 Undo 和关闭普通文件范围。没有第二个应用、公开 SDK、长期 Snapshot 格式或管理器重启恢复声明。

## 模块边界

| 模块 | 职责 | 业务依赖 |
| --- | --- | --- |
| `runtime/abi.hpp` | 版本/架构/区域、停点、资源种类、显式引用和原生完成证据 | 无 Work、seed 或 receipt 算法 |
| `runtime/processes.hpp` | 自有进程/句柄/Desktop、单调试线程的 PID/TID 分发、退出清理 | 无业务结构 |
| `kernel.lib`：`execution.cpp`、`image.hpp` | 原始区域/栈/CONTEXT、目标 PE 及 CET 返回关系、引用重关联 | 仅解释应用显式声明的执行 ABI |
| `target.lib` | 目标 GUI 线程中的标准资源/Undo、真实 GDI 采集 | 明确受限的原生资源适配器 |
| `runtime/files.hpp`、Action/Artifact | 注册的已关闭文件版本、固定成果及私有副本 | 不生成业务正文，文件名由调用者声明 |
| `scenario/fixture_contract.hpp` | 本 fixture 的 Work、命令邮箱、版本和大小声明、只读观测编码 | 实验客户端协议，不进入 kernel |
| `scenario/scenario.cpp` | 原 R4 流程及 fixture 的测试 oracle | seed/receipt 预期只在这里核对 |
| `app/` | 真正的 GUI、业务初始化、计算、原生文件生成/处理、协作入口 | 只编入 fixture.exe |
| `helper.exe` + 既有输入 DLL | 同 Desktop 的输入/取图/命令桥 | 不具有应用或管理器角色 |

manager.exe 在本实验链接了验收驱动；因此它仍包含该 fixture 的客户端 schema 和测试预期。恢复内核没有这些业务知识，manager 没有链接应用实现。不是宣称整个实验驱动已成为通用管理服务。实际编译/链接分组见每次结果中的 `link-boundaries.json`，测试另扫描内核依赖。

`runtime/native.cpp` 在一个翻译单元中复用旧 common/native.hpp 的既有实现，通过私有声明头供多模块调用；没有修改旧共享工具或靠重复定义/忽略链接错误完成拆分。

## 独立映像 unwind

管理器从所选应用 PE 的导出表取得元数据 RVA，核对文件 SHA256、真实加载基址、x64 PE 头、异常目录和实际函数表字节。源与载体仍要求同一应用构建/布局；管理器与应用的 EXE 和加载基址必须不同。

`image.hpp` 从已停止的目标进程读取可读映像页，建立同目标虚拟地址的**不执行、只读诊断副本**，并在复制栈上显式查询目标 `.pdata`/unwind 信息。RtlVirtualUnwind 使用目标映像和该表，不使用管理器的 RtlLookupFunctionEntry，不加载执行目标代码，不更换管理器执行上下文。局部映像/栈地址占用、未知 unwind 版本或无法分类的必要资源明确失败，不回退到同 EXE。

每一步普通返回地址与真实目标 CET_U/SSP 处的影子返回地址比较；新载体必须先正常 CALL 建立自己的链，才写回原始字节和上下文。不会复制源 SSP、修改影子栈、关闭 CET/GS/CFG 或跳过返回关系。

运行时 ABI 与 fixture 客户端契约分开：前者检查版本、大小、架构、区域及必需资源；后者由测试客户端检查 Work/邮箱大小。区域地址与引用由应用显式声明，内核不认识 Work 布局，不扫描整数推测 HWND。

## 构建与运行

```powershell
& ./experiments/r5-001a/build.ps1 -MsvcRoot <MSVC-root> -SdkRoot <Windows-SDK-root> -SdkVersion <version> *> ./experiments/r5-001a/out/build.log
& ./experiments/r5-001a/test.ps1 -ResultsDir D:/new-results -Repeats 2
# 独立 manager 启动独立编译的应用；输出目录必须不存在。
& ./experiments/r5-001a/out/manager.exe --fixture D:/focus-views/experiments/r5-001a/out/fixture.exe --output D:/new-case --scenario positive
```

正式矩阵包含原 R4 七个场景与 `wrong-image`、`abi-mismatch`、`unknown-resource`，各两轮；默认另完整执行旧 R4-003 及其嵌套回归。`-SkipRegression` 只用于开发诊断，必须报告未运行。失败不重试、不静默冷启动；源码/二进制哈希和真实运行结果分开记录。

错误映像用自有 fixture 副本的实际 PE 时间戳字节变更生成，运行时在创建目标前核对哈希拒绝；不是关闭保护后运行。ABI 负例在独立目标声明中提供不匹配版本；未知资源例创建额外子窗口并声明不支持的必要资源种类。RB 失败时其他分支继续，文件/快照/Artifact 保持。

输入模块仍使用 R4-001 版本。拆分暴露了原同 EXE 中由 bridge 带入的 GetCursorPos 导入依赖；fixture 新增只读真实查询，记录原生/介入后的返回及错误，业务逻辑不变。缺失导入的初始失败留在本地 out/，不能把模块安装失败当成功。

原始 Snapshot 含目标只读诊断映像材料，用于核对 unwind，并非把 PE 全局数据恢复到目标。业务数据仍只在声明区域中原始捕获和恢复，运行时资源由明确入口重建。本轮不复制原启动栈、整个 TEB/PEB/TLS、任意优化帧或未知对象。

实际结果见 [正式报告](results/20261025-r5001a/report.md)。GATE-002 的历史创建拒绝继续作为独立可靠性门禁，不新增 ETW/压力循环。原始证据通过本实验 `.gitattributes` 固定字节；人工宿主交互仍未执行。
