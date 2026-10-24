# R3-003 实际实验报告

2026-10-24，CAP-12，基于 `38857dd447e2f132539f9c080c98c18e7fa05e5e`。**已编写、已编译、已运行；已验证主 EXE 指定 IAT 路径上的私有异步按下高位，以及它与队列键态不同的时间语义。** 不宣布完整键鼠隔离、任意应用兼容、R3 完成或 GUI 快照恢复。

## 环境、身份与复现

Windows 11 家庭版中文版 10.0.26200 x64，Intel i9-13900HX，非提升权限。MSVC 目录 14.44.35207、编译器 19.44.35229.0、Windows SDK 10.0.18362.0；C++20、`/W4 /WX /O2 /MT`，链接 `/DYNAMICBASE /NXCOMPAT /CETCOMPAT`。未引入驱动、虚拟机、第三方运行库或修改系统安全设置。

精确身份见 [源码/二进制哈希](full-verified/identity.json)、[构建元数据](full-verified/build.json)、[构建输出](build.log)、[系统](system.json)、[依赖](dependencies.txt) 和 [EXE 导入表](exe-imports.txt)。基线提交本身不包含新增源码，复核应同时使用哈希清单。所有模式运行同一个 `r303.exe --role fixture`。

```powershell
.\experiments\r3-003\build.ps1 `
  -MsvcRoot 'D:\focus-distinct-browser\.tools\msvc\VC\Tools\MSVC\14.44.35207' `
  -SdkRoot 'D:\Windows Kits\10' -SdkVersion 10.0.18362.0
.\experiments\r3-003\test.ps1 -Scenario minimal -ResultsDir '<新的最小结果目录>'
.\experiments\r3-002\build.ps1 `
  -MsvcRoot 'D:\focus-distinct-browser\.tools\msvc\VC\Tools\MSVC\14.44.35207' `
  -SdkRoot 'D:\Windows Kits\10' -SdkVersion 10.0.18362.0
.\experiments\r3-003\test.ps1 -ResultsDir '<新的完整结果目录>'
.\experiments\r3-003\test.ps1 -Scenario exit -ResultsDir '<新的退出结果目录>'
```

最终源码对应 [最小实验](minimal-final/summary.json)、[完整实验](full-verified/summary.json) 和 [退出实验](exit-verified/summary.json)。完整脚本自动执行未修改的 R3-002 全套回归，见 [本次回归结果](full-verified/r302-regression/summary.json) 和 [重新构建输出](r302-regression-build.log)。

## 机制与观察路径

应用原有 Win32 消息循环、原生 EDIT 和 `GetAsyncKeyState` 调用未改。`observer.cpp` 仅补充只读查询，包括左右修饰键、X、重复查询和显式动态原生入口；无介入/介入两组共用这个新二进制，没有“通过版”业务逻辑。

每 View 的常驻桥和受管队列继续独立。桥接受事件时发布一个完整异步状态版本；偶数 seqlock epoch 的发布是线性化点，查询读取单个一致版本。目标 GUI 线程出队时仍通过 SetKeyboardState 更新真实队列表。两者不共用同一个生效点。

最小时间实验有意先把输入接受到运行时待投递队列，不立即 PostMessage。`staged_not_posted` 明确披露这一边界；随后只读定向消息在 GUI 线程执行真实应用查询。没有把这个阶段称为 Windows 物理键盘输入。之后再把待投递事件按原序送入 Windows 消息队列。

IAT 解析只接受已加载、受本运行时拥有的 x64 主 EXE 的普通命名导入。唯一 USER32.GetAsyncKeyState 槽的原值必须等于原生导出地址；数据页临时 PAGE_READWRITE，比较交换入口后恢复原页保护。系统 DLL、磁盘文件、其他模块及动态解析入口不改。安装和移除由同 GUI 线程的定向控制钩子执行，没有全局钩子。

完整运行 native PID/TID `42644/47472`，A `19900/39748`，B `48516/38280`。A 的实际地址如下，来自 [输入桥日志](full-verified/run/A-input.jsonl) 和控制器独立读回：

| 对象 | 实际值 |
| --- | --- |
| EXE 基址 | `140701615063040` |
| EXE IAT 槽 | `140701615379528` |
| 原始入口 / 模块 | `140736106907648` / `C:\WINDOWS\System32\USER32.dll` |
| 替换入口 / 模块 | `140734672686032` / 本次 `runtime303.dll` |
| IAT 页权限前/后 | `2 / 2`，PAGE_READONLY |

控制器分别读取 A、B 的 11 个实际调用点，核对指令为 `FF 15` 的 RIP 相对间接调用，并计算其引用槽，均等于记录的 EXE IAT。调用者返回地址均属于主 EXE，线程均为实际 GUI TID。它们同时覆盖只读扩展和未修改 fixture 的原调用；不是运行时直接填写观测结果。见 [callsite / iat_readback](full-verified/run/controller.jsonl)。

## 实测结果

**R3-003 完整实验：44 项控制器断言、62 项独立证据检查符合预期。** 最小实验为 15 项断言/36 项检查，退出场景为 3 项/16 项。这些数字不是能力数量；原生异步键态对照和故障注入的健康卸载仍记为能力 fail。

| 场景 | 实际证据与判定 |
| --- | --- |
| 无 IAT 介入 | 相同队列机制处理 Ctrl 后，GetKeyState 已按下，EXE 原始 GetAsyncKeyState 仍为 0；能力 **fail** 保持 |
| A 接受 down、未消费 | 输入序号 `1`、版本 `1`、消费序号 `0`；应用 API 返回 `-32768`（仅高位置位），队列 Ctrl 为 0；**通过** |
| B 同时读取 | B 自身版本/序号仍为 0，Ctrl 异步高位为 0；**通过** |
| 重复查询 | 同一版本的重复 API 读取结果相同，不消耗历史低位；虚拟低位固定为 0；**通过所声明高位子集** |
| A 在消费前又接受 up | 输入序号 `3`、版本 `2`、消费序号仍 `0`；应用立即读到松开；**通过** |
| 随后消费旧 down | 未修改 fixture 的 `control_before` 同时记录队列 Ctrl=`-128`、异步 Ctrl=`0`；二者分别反映当前出队消息和最新已生效事件；**通过** |
| 连续 down/up | 四次事件形成单调版本，API 读取按所记录版本匹配接受日志；队列仍逐消息推进，不提前应用未来 up 到队列表；**通过** |
| 左右 Ctrl | 右侧保持按下时释放左侧，异步通用 Ctrl 仍按下、左松开、右按下；**通过** |
| Shift 与 X | 版本 `14` 时，右 Shift 和 X 异步按下，左 Shift 和 Ctrl 已松开；**通过** |
| A/B 交错 | B 持自己的 Ctrl 时，A 继续持自己的 Shift/X，互不改变；**通过** |
| 取消 | 版本 `15` 的取消先使异步 Ctrl/Shift/X 清除，而队列 Shift 仍按下；消费释放后队列才清除；**通过** |
| 原生入口对照 | A 私有 Ctrl 按下时，同线程经 GetProcAddress 解析 USER32 原始函数仍返回 0；该路径**未被虚拟化** |
| 新运行时里的 EDIT 回归 | B 通过正常输入、Home、Shift+End，原生 EDIT 实际选区为 `0..5`；没有设置选区命令；**通过** |
| 正常移除 | 先释放键态，再恢复原始槽与页权限、释放自持 DLL 引用、解除钩子；控制器读回 A 的槽等于原始地址，应用后续继续原生查询；**通过** |

主要原始证据：[A API/版本日志](full-verified/run/A-input.jsonl)、[A 原生消息与查询](full-verified/run/A-events.jsonl)、[B 日志](full-verified/run/B-input.jsonl)、[原生对照](full-verified/run/native-input.jsonl) 和 [控制器观测](full-verified/run/controller.jsonl)。独立脚本按接受事件重建版本台账，再逐条核对实际查询，不把 DLL 自报的结果当作唯一证据。

## 失败、拒绝与清理

| 反例 | 实际结果 |
| --- | --- |
| 安装时缺少指定导入 | 故意要求解析不存在的导入名；运行时错误 `127`，`instance_rejected`，无可用输入通道、无已接受输入、无残留入口替换 |
| 入口已交换后的安装失败 | 明确的故障注入 `injected.IAT.after_exchange`，错误 `1223`；真实 IAT 已先被交换，随后回滚。控制器读回原始地址，原页权限恢复、自持引用释放；不发布健康通道 |
| 移除被拒绝 | 故障注入 `injected.IAT.restore_refused`，错误 `5`；健康卸载能力 **fail**。槽仍指向 runtime，DLL 保留自持引用，通道 health=`-1` 并拒绝新输入；之后只结束本实验拥有的目标进程 |
| 事件待投递时目标退出 | 控制器确认目标终止；桥记录 `owned_target.exit`、错误 `1067`，清空私有异步状态并标记地址空间已消失。不是把目标死亡叫做 IAT 恢复成功 |

这些注入错误不是系统安全策略阻止 IAT 的证据。本机正常 IAT 路径确实成功，没有关闭保护或换成业务状态导入。

研发中发现并修复了清理错误覆盖主错误的问题：[最小红色复现](exit-error-red/controller.jsonl) 最终错误被后续解除钩子的 `0` 覆盖，退出断言真实失败；[修正后的同场景](exit-error-green/controller.jsonl) 保留 `1067`。修复为原子保留首个错误，后续清理返回值另记。最终构建的 [退出回归](exit-verified/summary.json) 再次通过。目标终止后，本机解除线程钩子可能返回失败且 GetLastError 为 0；日志保留这个实际次要结果，同时保留主错误，不伪造错误码或健康退出。

完整 R3-003 创建的 15 个子进程均确认退出，7 个 Desktop 均释放，无 cleanup_error。失败卸载中的 DLL 自持引用先保持到目标终止，避免可调用入口悬空；不宣称 Unhook 会让 Windows 立即从进程卸载所有模块。

## 回归、宿主与结论范围

原 R3-002 本轮重新构建并完整执行：**32 项断言、39 项证据检查符合预期**，原生失败/故障失败仍为 fail，实际 GDI PNG 与原生 EDIT 结果保留在 [回归目录](full-verified/r302-regression/summary.json)。R3-001、R3-002、R1/R2 和 common 的源码及历史结果都未修改；**R1/R2 本轮未重跑**。

R3-003 本次宿主采样 42 次，10 个外部输入间隔，0 次静止阶段变化、0 个采样错误；状态为 **inconclusive**。回归运行也有用户输入，宿主采样仍为 inconclusive。不能由此宣称人工使用宿主时已经无干扰。没有使用 SendInput、SwitchDesktop、SetCursorPos、SetForegroundWindow 或 AttachThreadInput；宿主监测调用未被虚拟化。人工交互 **未执行**，现有 30 秒步骤保留。R3-001 采集取消后的未确认结论不变。

只更新 CAP-12 的已验证子集：**已核对主 EXE 普通导入路径、单 GUI 线程、Ctrl/Shift 左右键和 X 的当前按下高位**。历史低位、动态解析、其他模块/系统控件内部调用、延迟导入、多线程和其他按键并未验证。未覆盖查询返回 0 并有 `supported=0` 标志，不能把该 0 称为鼠标按钮隔离。IAT 替换是模块级操作，线程钩子的安装范围不等于已经支持整个进程的任意调用线程。

本轮 GUI 实例全部冷启动；没有保存/恢复窗口、普通 GUI 调用栈或图形资源，也没有把独立原型拼起来称为 GUI 分叉。下一步可以单独研究鼠标状态与路由，不需要把本次高位子集扩大成完整键鼠兼容层。

机制依据：[PE 导入表与 IAT](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)、[VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect)、[GetAsyncKeyState 原生语义](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate)。通过结论来自本机实际记录，未把文档机制说明当成实测结果。
