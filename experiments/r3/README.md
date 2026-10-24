# R3-001：原生后台图形与输入诊断

**诊断已实际编译、运行；R3 整体未通过。** 后台 GDI 窗口与若干定向消息路径有效，但修饰键和鼠标按键原生状态不一致，越界捕获路由未验证。详见 [报告](results/20261024-r3/report.md)、[逐项汇总](results/20261024-r3/suite-final/summary.json) 和 [真实图像](results/20261024-r3/suite-final/run/A-after-input.png)。这是冷启动 GUI 基线，不是 GUI 快照恢复。

## 构建和复现

使用 MSVC x64、Windows SDK 和 PowerShell，静态链接 C++ 运行库，不安装第三方包：

```powershell
.\experiments\r3\build.ps1
.\experiments\r3\test.ps1
```

非标准工具链可传 `-MsvcRoot`、`-SdkRoot`、`-SdkVersion`；构建和测试均支持 `-OutDir`，测试另支持 `-ResultsDir`。测试脚本用 Windows 上已有的 System.Drawing 独立读取 PNG，而不是只相信采集器返回值。

`r3.exe` 包含 controller、fixture、bridge 三种进程角色。控制器通过 CreateDesktopW 和 STARTUPINFO.lpDesktop，把 A/B 及每次请求的桥接进程放到相应后台桌面；使用共享内存和就绪事件传递诊断数据。每次采集有单独的辅助进程和期限。创建的进程加入本实验自己的 Job，退出时只清理所拥有的对象。

单独运行完整诊断或最小复现，输出目录必须不存在：

```powershell
.\experiments\r3\out\r3.exe --output "$PWD\experiments\r3\out\fresh-all"
.\experiments\r3\out\r3.exe --output "$PWD\experiments\r3\out\fresh-modifier" --scenario modifier
.\experiments\r3\out\r3.exe --output "$PWD\experiments\r3\out\fresh-capture" --scenario capture
.\experiments\r3\out\r3.exe --output "$PWD\experiments\r3\out\fresh-recovery" --scenario capture-recovery
```

modifier 最小复现直接比较 A 的 WM_KEYDOWN 与 GUI 线程内 GetKeyState/GetKeyboardState，并保留 B 对照。capture-recovery 复现暂停 A、让 PrintWindow 助手超时、恢复 A 后的再次采集。诊断完成的退出码 0 不等于其中所有能力通过；检查 JSON 中的逐项 status。

## 证据约定

- `A-events.jsonl` / `B-events.jsonl`：目标 GUI 线程记录的原生消息、按键查询、焦点、捕获、文字、选区和绘制。queryThread 必须等于实际 GUI tid。
- `controller.jsonl`：投递接受、目标观测、PrintWindow 返回、图像接收或拒绝、进程/桌面生命周期。投递成功不代表操作完成。
- PNG 只编码 PrintWindow 返回的真实 GDI RGB 像素。目标自己绘制可见 Tick 和对应黑白位标记；桥接进程及测试脚本分别解码，核对目标绘制日志、更新范围和图像哈希。
- 本机 PrintWindow 实测触发 WM_PAINT，未触发预设的 WM_PRINT。按真实绘制路径验证，不将某种消息名当作必经事实。没有使用控制器绘图、合成目标内容、旧帧缓存或 GPU/DWM 采集。
- 每个采集文件使用新路径；失败不发布 PNG，超时会结束该次助手并确认退出。目标已退出时检查 HWND 与 PID/TID 身份，拒绝继续使用旧 HWND。
- `allHarnessChecksPassed` 只表示证据及清理检查符合规则；修饰键、鼠标状态等能力仍保留 fail。not_verified、not_executed、inconclusive 不得改写为支持。

## 人工宿主交互步骤（本次未执行）

```powershell
.\experiments\r3\test.ps1 -HoldSeconds 30
```

在保持阶段，程序继续向 B 的原生编辑框投递字符，并向 A 交错投递拖拽消息。操作者在自己的真实桌面打开记事本，连续输入、切换宿主应用、拖动宿主窗口并移动鼠标，记录是否丢焦点、串入字符、光标被移动或桌面切换。不要把自己的移动或前台切换视为运行时串扰。

检查 host.jsonl 的输入桌面和前台/光标观测，并结合人的实际体验记录结论。程序不会读取宿主文本或自动认证人工结论；仅设置 HoldSeconds 会标为 not_verified，默认自动运行标为 not_executed。

未调用 SwitchDesktop、SendInput、SetCursorPos、SetForegroundWindow、AttachThreadInput 或 SetKeyboardState；未虚拟化或替换测试程序的原生输入查询。只复用了未修改的 common/native.hpp 摘要工具，R1/R2 源码和共享工具未改动，因此未重复运行既有实验。

平台依据：[PrintWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow)、[GetKeyboardState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeyboardstate)、[GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate)、[SetCapture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture)。平台说明与本机实际观测分别记录，不以文档推断代替实测。
