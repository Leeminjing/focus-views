# R3-003：后台异步键态的 EXE IAT 虚拟化

**已编译、已运行，限定导入路径和按键高位的实验通过。** A 的 Ctrl 事件已接受、键盘消息尚未投递时，同一个真实应用的 `GetAsyncKeyState` 返回按下，队列键态仍为松开；B 独立。原生 IAT 对照和动态解析的原生入口继续返回原值。详见 [报告](results/20261024-r3003/report.md)、[完整证据](results/20261024-r3003/full-verified/summary.json) 与 [原始应用查询](results/20261024-r3003/full-verified/run/A-events.jsonl)。

这是显式 API 虚拟化，不是 Windows 原始异步返回值，也不是 GUI 恢复。只验证已核对的主 EXE 导入项、单 GUI 线程和 Ctrl/Shift 左右键、X 的按下高位。

## 构建与复现

```powershell
.\experiments\r3-003\build.ps1
.\experiments\r3-003\test.ps1 -Scenario minimal

# 完整实验默认执行未修改的 R3-002 全套回归，先准备其构建。
.\experiments\r3-002\build.ps1
.\experiments\r3-003\test.ps1

# 单独复现“接受输入后目标退出”的清理路径。
.\experiments\r3-003\test.ps1 -Scenario exit
```

非标准安装可向构建传 `-MsvcRoot`、`-SdkRoot`、`-SdkVersion`；构建/测试支持 `-OutDir`，测试支持 `-ResultsDir`、`-R302OutDir`。每次使用新结果目录。实际工具链与本机命令见报告。

产物是 `out/r303.exe` 与 `out/runtime303.dll`，静态 C++ 运行库，不引入第三方包。代码在独立目录保留 R3-002 的受管队列、进程/桌面生命周期和每 View 常驻桥机制；不修改旧实验，也不提取公共 SDK。`../r3/fixture.cpp`、`bridge.cpp`、`protocol.hpp` 原样编译。新 `observer.cpp` 只增加只读观测，两个模式都使用同一个 EXE；原生控件、业务处理和原消息循环均未修改。

## 两个时钟

| 状态 | 生效点 | 查询 |
| --- | --- | --- |
| 受管异步高位 | 常驻桥接受有序事件，以偶数 seqlock epoch 发布完整状态时 | 主 EXE 原有 GetAsyncKeyState 调用，经已核对 IAT 入口 |
| 线程队列键态 | 目标 GUI 线程 WH_GETMESSAGE 收到 PM_REMOVE 时 | 原版 GetKeyState/GetKeyboardState，原生控件继续读取真实线程表 |

`stage` 命令将输入接受到运行时自己的待投递队列，并使异步状态生效；`deliver` 才将这些消息投递到 Windows 队列。只读诊断消息走定向线程钩子，可在待投递期间触发原生应用查询。日志明确写 `staged_not_posted`，不把运行时队列说成已经进入 Windows 输入流。

普通批次的 down/up 可以在应用处理 down 前都已生效，因此处理 down 时可以同时观察到“队列按下、异步松开”。`Probe` 和 fixture 原有查询都调用真实的 EXE API 导入项，不读取控制器的期望答案。

## IAT 与生命周期

线程级 `WH_CALLWNDPROC` 只承担受管安装、只读观测和恢复入口；`WH_GETMESSAGE` 继续更新队列表。运行时检查主 EXE 的 x64 PE 头、USER32 导入名、唯一 IAT 槽和当前原始地址，再临时把该数据页设为可写，比较交换函数指针，恢复原页权限。系统 DLL 和磁盘 EXE 不改写。

运行时保留原始入口、自持模块引用、模块路径和槽地址；控制器独立读取目标 IAT 和调用指令，核对真实的 `FF 15 + RIP 相对位移` 指向该槽。卸载先清除两套受管状态、恢复原始 IAT/权限并确认，再解除钩子和释放自持引用。恢复失败则通道失效，DLL 保持自持引用，避免留下指向已卸载代码的入口，随后只结束本实验拥有的目标。

主 EXE 的 IAT 替换是模块级操作。本轮应用只有一个 GUI 线程；没有验证其他线程的调用与并发日志语义，不得宣称线程范围外兼容。

## 证据与边界

- `effective`：每个已接受事件的输入序号、单调版本与状态；取消、失效清空有独立原因。
- `virtual_query`：实际调用 PID/TID、返回地址、调用模块、输入序号、读取版本、结果和 `supported/valid` 标志。
- `probe` / `*-events.jsonl`：只读 EXE 扩展与原 fixture 的实际 API 值、队列键态和控件结果。
- `iat_readback` / `callsite`：控制器通过 ReadProcessMemory 独立读取目标数据与代码。
- `instance_rejected`、`channel_invalid` 与错误 API/码：安装失败、卸载拒绝、目标退出都不发布健康通道。首个错误与后续清理错误分别记录。
- `identity.json`：构建源文件和 EXE/DLL 的 SHA-256；`summary.json` 区分证据断言通过与能力 fail。

低位历史按下语义没有实现，虚拟结果低位始终为 0。动态 `GetProcAddress` 的原生 GetAsyncKeyState 作为未拦截对照；其他模块内部调用、延迟导入、IME、多线程、普通键全集、鼠标按钮和 GUI 恢复均未验证。未覆盖的键查询返回 0 并记录 `supported=0`，不回读物理键盘，也不把该 0 当成鼠标隔离通过。

宿主监测仍是原生调用。人工宿主操作未执行；原 [R3-002 30 秒人工步骤](../r3-002/README.md) 保留，可单独进行。R3-001 采集取消后再次取图未确认的历史结论保持不变。R1/R2 未修改、未重跑。
