# R3-004：后台虚拟鼠标与原生窗口路由

独立诊断实验，C++20 + Windows SDK，使用同一份原版 `../r3/fixture.cpp`、原生 EDIT / BUTTON 和 SetCapture / ReleaseCapture 拖拽程序。A/B 是冷启动实例，不是从 S0 恢复的 GUI 分支。

```powershell
.\experiments\r3-004\build.ps1 -MsvcRoot '<MSVC 目录>' -SdkRoot '<Windows Kits\10>' -SdkVersion '<SDK 版本>'
.\experiments\r3-004\test.ps1 -Scenario minimal
.\experiments\r3-004\test.ps1
```

完整测试还要求已有 `experiments/r3-003/out` 与 `experiments/r3-002/out` 的构建；可通过 `-R303OutDir`、`-R302OutDir` 指定。完整测试实际重跑这两个原封不动的实验，不重跑 R1/R2。结果目录必须不存在，失败记录不覆盖。

实际执行与范围见 [报告](results/20261024-r3004/report.md)。

## 机制与时序

- 每个 View 独立后台 Desktop、GUI 进程、常驻输入桥、映射和线程钩子。普通鼠标输入只有 `action + top-client x/y`，不接受目标 HWND。
- 桥接进程将输入事件作为一个版本发布：左键高位和屏幕坐标立即生效；屏幕坐标通过所属 top 窗口的 ClientToScreen 取得，不读取物理光标。
- 目标 GUI 线程在 `PM_REMOVE` 才更新真实 SetKeyboardState 队列表、转换消息。`PM_NOREMOVE` 不推进。事件自身坐标用于路由；GetCursorPos 返回最近接受版本的坐标，二者不必相同。
- GUI 线程调用真实 GetCapture。无捕获时递归 ChildWindowFromPointEx；有捕获时优先投递到实际捕获窗口，并做 ScreenToClient。每一候选窗口都须属于目标 PID/TID 和受管 top 窗口树。最后一个已观察到的捕获窗口被销毁后，拒绝后续输入。
- EXE 的 GetAsyncKeyState 与 GetCursorPos 两个普通命名 IAT 槽经 PE 边界、原入口和页权限核对后事务性替换。原生 EDIT/BUTTON 窗口过程未修改。GetCapture/GetFocus/GetKeyState/GetKeyboardState 仍是原生接口；异步高位和光标查询明确属于运行时虚拟化。实际调用指令由控制器读回核验。
- 程序本来的消息循环、WM_LBUTTONDOWN/UP/MOVE、SetCapture/ReleaseCapture、按钮 BN_CLICKED 和 GDI 绘制产生业务变化。observer.cpp 仅增加只读 API 观测，同一二进制用于 IAT 开启/关闭对照。

## 失败与清理

持键期间每次鼠标输入刷新默认 5 秒租约；遗漏抬键并超时会使通道失效，而不是无限保留左键。正常关闭或可响应的失败清理先清队列表，再向原生捕获窗口发送释放，必要时调用 ReleaseCapture，恢复两个 IAT 槽和权限后解除钩子。

GUI 线程的定时器监测输入桥进程。桥意外退出时，确认其死亡后才接管异步状态写入、清除按钮和捕获、恢复 IAT；异常回调中保留 DLL 自持引用，直到该受管 GUI 进程结束。GUI 挂起时清理可以无法确认，此时保留 DLL 引用、拒绝通道；控制器平衡本实验的 SuspendThread 后终止自己拥有的测试进程。卸载拒绝同样不能报告健康。日志保留首个错误，后续清理错误另记。

负向用例包括：外部 View 的路由候选、实际捕获窗口销毁、漏发抬键、持键关闭、桥退出、GUI 挂起、卸载拒绝、两槽安装中途失败。故障注入使用单独字段或运行时控制点，不是正常鼠标输入 API。负向断言通过表示准确拒绝，不表示相应输入能力可用。

## 人工宿主交互

```powershell
.\experiments\r3-004\test.ps1 -HoldSeconds 30 -ResultsDir '<全新结果目录>'
```

看到 `MANUAL_WINDOW_STARTED` 后，在宿主正常移动物理鼠标、点击自己的其他窗口并输入文字。实验同时让 A 保持拖拽、B 反复点击。人工记录实际操作、是否出现焦点抢占、光标跳转、意外按键，以及开始/结束时间；保留单独人工结论。计时窗口本身不生成通过结论，脚本标记 `not_verified`。未执行该流程则为 `not_executed`。

宿主每 50ms 记录原生光标、前台、输入桌面、Ctrl/Shift/左键和 LastInputInfo。有人输入的区间不归因于运行时；静止采样仅是有限证据，不能替代人工验证。

## 支持范围

已测 Win11 x64、单 GUI 线程、单受控 top 窗口树、左键和移动、客户区、普通 GDI、主 EXE 两个已确认的普通 IAT 导入路径。递归实现存在，但本次原版 fixture 仅实测一层子窗口。GetCursorPos 仅对有效 POINT 指针、已建立逻辑位置的查询验证；异步键态只返回指定键和左键的高位，低位历史不支持。

不覆盖右键、滚轮、非客户区、系统菜单、Raw Input、GetMessagePos、双击、其他模块/动态 API 解析、多 GUI 线程、捕获 HWND 重用、DPI/多显示器矩阵、任意第三方应用、GPU、GUI 快照恢复。未拦截 USER32 内部查询路径。后台 Windows 自己仍未提供两个物理鼠标；这里提供的是明确的受管路由。原始背景 API 对照和既有采集取消后再取图未确认的结论保持不变。
