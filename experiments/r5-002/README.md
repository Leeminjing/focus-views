# R5-002：有限可变深度协作式执行恢复

本实验在 R5-001b 的完整成果流程上替换执行内核，支持两个明确的调用形状：ABI v1 三协作帧/五返回关系，以及 ABI v2 四协作帧/六返回关系。不是任意深度或未改造程序恢复。

## 模块与 ABI

- `runtime/abi.hpp` 包含未改动的 v1 `Metadata`；v2 在完整 v1 前缀之后增加带版本/长度的执行描述。旧应用不用重编译。
- 描述给出有限帧数量、返回数量、从内到外的函数入口、栈局部量、业务停点和外部调用者边界。
- `runtime/processes.hpp` 在同一调试线程中验证版本、数量、地址和事件归属；v1 自动转换为三帧描述。
- `runtime/execution.cpp` 从目标 PE 使用 `RtlVirtualUnwind`，验证每帧顺序、局部量所处帧、调用者边界，以及每项普通/影子返回关系。恢复前源/目标关系及 RSP 必须相同。
- `recorder/main.cpp` 的 `record → emit → fold → sample → CheckpointTrap` 真正执行四层 C++ CALL/RET。新增 `record` 有独立的随机派生局部量、业务停点和结果依赖；协作引导只重建调用结构，跳过业务前缀。
- 复用旧资源、Undo、图像、输入与文件实现。`target.lib`、`helper.exe`、`runtime304.dll` 保持原件。新版内核链接到显式实验客户端；业务 oracle 不进入内核。

`execution_capacity=8` 是编码/校验上限，**不是支持八帧的声明**。只接受并验证三帧和四帧；同机、同构建、固定布局、`/Od /Ob0`、单 GUI 线程、已有 EDIT/BUTTON/Undo/关闭同步工作文件限制保持不变。

## 构建与运行

先构建 R5-001a 和 R5-001b，保留各自 `out/fixture.exe` 与 `out/second.exe` 原件。

```powershell
./experiments/r5-002/build.ps1 -MsvcRoot <MSVC目录> -SdkRoot <Windows SDK目录> -SdkVersion <版本>
./experiments/r5-002/test.ps1 -ResultsDir <新的结果目录>
```

新内核用同一个 manager 分别管理未重编译的旧 fixture、未重编译的旧 recorder（`legacy.exe`）、新四帧 recorder。每款都实际执行 S0/A/B/SA/SB/RA/RB/C、真实 Undo、Artifact 和私有文件链路。测试另完整运行旧 R5-001b 及其 R5-001a/R4-003 嵌套回归。

故障包括：未知 ABI、真实四帧却声明三帧、遗漏帧入口、声明另一个有效函数作为 unwind 帧、错误合法 CALL 位置、遗漏第四帧局部量、遗漏 R10，以及已有资源/文件拒绝。失败不发布 View，已有分支继续，固定 Snapshot/Artifact 保持。

输入会话仍先完成既有 key-up/release 握手；不支持持有按键/拖拽的检查点。CET、/GS、CFG 不关闭，影子栈只读。未执行的宿主人工交互继续标记未执行，历史 CreateProcessW/5 门禁保持独立。

## 原理依据

使用目标应用自己的 [x64 unwind 元数据](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170) 与 [RtlVirtualUnwind](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlvirtualunwind) 逐帧检查；正常 CALL 建立新进程自己的 CET 链，不复制源 SSP，也不将 CONTEXT 写回成功等同于执行恢复成功。

实际测试结果见本目录 `results/`；开发期结果与正式结果分别保留。

## GUI 安全点排空

首轮正式矩阵有一次源 S0 的 `ERROR_BUSY`，尚未进入寄存器反例；原记录缺少具体队列消息，不能确认历史触发消息。确定性 `queued-paint` 对照复现：冻结/资源就绪/无捕获时，迟到的所属窗口 WM_PAINT 会使原 guard 拒绝。

新版仅在 1 秒期限内通过正常 Dispatch 处理所属窗口 WM_PAINT，然后再次验证空队列、关闭输入、无捕获/绘制及资源就绪；不重启进程或重试 Snapshot。其他消息明确拒绝；实际 PM_REMOVE 后也重新核对消息与 HWND，防止 Peek 的 WM_QUIT 例外被吞掉。`test-guard.ps1` 覆盖排空成功、未知消息和 WM_QUIT 插入对照。此修正不将首轮历史原因或 CreateProcessW/5 门禁标为已解决。

交付使用 `package.ps1` 保存已测试二进制，用 `archive-results.ps1` 将逐字节原始实验与完整嵌套回归归档并逐项验证 SHA256；原始文件仍保留在本地，仓库公开归档、清单、汇总及代表性真实帧。解压 raw ZIP 到一个新的结果目录可重建相同相对路径。

最终运行见 [20261026-r5002-verified/report.md](results/20261026-r5002-verified/report.md)。主矩阵 70 次、三个安全点对照、14,154 项本轮证据检查及完整旧回归通过。前两轮红项分目录保留。公开原始材料使用 Windows 原生 `archive-tar.ps1` 生成 tar.xz 并逐项解压/哈希校验；`archive-results.ps1` 提供 ZIP 形式的同类归档。运行包由 `package.ps1` 单独校验。
