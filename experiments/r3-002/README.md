# R3-002：受管队列键态与原生 EDIT

**已编译、已运行，受限键盘链路通过。** 同一个原生 fixture 在关闭状态更新时 Shift+End 仍为 `5..5`；目标 GUI 线程通过 `WH_GETMESSAGE` 调用 `SetKeyboardState` 后，A 的原生 EDIT 实际选择 `0..5`。B 独立处理输入。详见 [实际报告](results/20261024-r3002/report.md)、[完整证据检查](results/20261024-r3002/verified-full/summary.json) 和 [实际窗口图像](results/20261024-r3002/verified-full/run/A-selection.png)。

仅验证受管入口、单 GUI 线程、指定按键组合及当前 fixture。仍是冷启动 GUI 实例，不是 GUI 快照恢复；不承诺后台异步键态、完整键盘隔离、鼠标路由或 R3 整体完成。

## 构建与运行

```powershell
.\experiments\r3-002\build.ps1
# 必须先跑最小修饰键实验，再验证 EDIT 与失败路径。
.\experiments\r3-002\test.ps1 -Scenario modifier
.\experiments\r3-002\test.ps1
```

构建支持 `-MsvcRoot`、`-SdkRoot`、`-SdkVersion` 和 `-OutDir`；测试支持 `-OutDir`、`-ResultsDir`、`-Scenario modifier|all`。结果目录必须不存在，避免覆盖原始证据。本机复现参数见报告。输出是 `out/r302.exe` 与 `out/runtime.dll`，静态链接 C++ 运行库；不需要第三方包。

`fixture.cpp`、截图助手 `bridge.cpp` 和原生查询协议直接编译自未修改的 `../r3/`。对照与介入实例均运行同一个 `r302.exe --role fixture`；应用不自行加载 DLL、不导入运行时 API。跨进程线程钩子由同桌面的常驻输入桥安装。旧 R3-001 的源码、脚本禁止项与历史结果保持不变。

## 实验入口与证据阶段

每个 View 有独立共享队列和常驻输入桥。桥核对目标 HWND/PID/TID/桌面后，以显式线程 ID 安装 `WH_GETMESSAGE`，不使用全局钩子。带 token/序号的私有消息是受管输入入口；DLL 仅处理这些消息。在 `PM_REMOVE` 回调里按当前事件更新真实线程键盘表，再把消息交还原来的 `TranslateMessage/DispatchMessage`。其他消息沿原路径处理。

`baseline` 保留相同消息封装/展开和只读观测，但禁止键态写入；此外，在完全卸载钩子、输入桥退出后，另用旧 R3-001 助手直接投递 Ctrl，保留完全无钩子的失败对照。`skip_state` 是单事件故障注入，不是备用实现。

- `*-input.jsonl`：投递接受、目标线程钩子出队/查看、实际查询和处理屏障。查看事件 `removed=0, applied=0`，不能推进未来按键。
- `*-events.jsonl`：未修改 fixture 中的原生控件处理前后、原版键态查询、文本、选区、绘制。序号钩子回执不替代控件完成证据。
- `controller.jsonl`：输入批次、只读观测、独立断言、实际能力结论、进程/桌面清理。`assertion.passed` 与 `capability.status` 分开；预期反例依然是能力 `fail`。
- `identity.json` / `build.json`：源码与二进制 SHA-256、基线提交、系统、权限和工具链。
- PNG 与目标绘制日志、像素 Tick 共同核对；控制器不绘制目标内容。

屏障保证前面的按键消息已经返回控件过程。`TranslateMessage` 后续生成的字符可能排在屏障之后，所以普通 X 按键还独立等待原生 EDIT 的实际文本结果，不把屏障误作所有派生消息完成。

取消/正常卸载在目标线程清除运行时所持按键高位，确认原生查询清除后再解除钩子。若无法确认释放，通道失效并停止接受命令；不会报告健康退出。该实验没有验证任意应用内部快捷键/手势状态的撤销。

## 人工宿主交互（本次未执行）

```powershell
.\experiments\r3-002\test.ps1 -HoldSeconds 30
```

看到 `MANUAL_HOLD` 时，在真实桌面的记事本连续输入、切换应用、选择文字和移动鼠标。此时 A 保持受管 Ctrl，B 持续用正常键消息导航。记录是否出现宿主 Ctrl 串入、焦点被抢、丢字或光标跳动；把用户自己的前台/光标变化与运行时干扰分开。结合 `host.jsonl` 的前台、输入桌面、光标及 Ctrl/Shift 高位采样记录人工结论。

保持模式最多 30 秒，受有界诊断日志容量约束。仅执行保持模式仍为 `not_verified`，不会自动生成“人工通过”。本次仅做 1 秒自动保持冒烟，未进行人的交互验收。R3-001 的人工未执行与采集取消后再次取图未确认结论继续保留。

未覆盖：多 GUI 线程、消息过滤/模态循环、IME/死键/切换布局、Alt/系统快捷键、自动重复时序、Raw Input、异步键态隔离、完整焦点/鼠标路由、任意第三方应用及 GUI 保存恢复。入口允许的 A–Z 中本轮只实测 X，其余不能据此宣称已验证。
