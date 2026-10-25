# R3-005b-001：原生 EDIT 资源与父子关系恢复

本实验基于未修改的 CET-002，新增一个实验私有的 Win32 控件资源适配器。范围是单 GUI 线程、一个顶层自绘窗口、一个标准 EDIT、一个标准 BUTTON，继续使用 CET、/GS、CFG 和编译期协作续接。不是任意控件堆内存复制或应用文档导入。

## 运行

```powershell
New-Item -ItemType Directory -Path '.\experiments\r3-005b-001\out' -Force | Out-Null
.\experiments\r3-005b-001\build.ps1 -MsvcRoot '<MSVC 根目录>' -SdkRoot '<Windows Kits\10>' -SdkVersion '<版本>' 2>&1 | Tee-Object '.\experiments\r3-005b-001\out\build.log'
.\experiments\r3-005b-001\out\r305a.exe --output '<全新目录>' --scenario positive
.\experiments\r3-005b-001\test.ps1 -ResultsDir '<全新结果目录>'
```

默认每个场景重复两轮，再运行原 CET-002 的完整回归；该回归需要先构建原 CET-002 的 out。`-SkipRegression` 仅供诊断，未运行时报告明确标记。不得覆盖旧结果目录。正向进程返回 0，预期拒绝的进程返回 2，负向被准确检出不算恢复成功。

## 状态与时序

源程序业务初始化生成随机文本，用正常 WM_SETTEXT 初始化自己的 EDIT，并标记未保存修改。初始化自然建立空撤销历史；之后只用原有受管 Home＋左 Shift＋End 选择文字。快照器没有清空撤销缓冲区，也没有替源程序修改选区来制造前置条件。

源输入桥确认已消费操作、释放键态并退出后，运行时冻结受管计算，向仍响应消息的 GUI 线程提交只读资源采集请求。`resources.cpp` 通过原生消息查询窗口树、文本、选区、修改标志、EM_CANUNDO、焦点及光标；采集完成后才进入调试检查点并固定 S0。

新进程先创建真实窗口和子控件，在私有准备消息循环中由同线程运行时恢复原生资源、读回核验；随后才进入 CET 合法 CALL 链的结构检查点、恢复原始受管内存/栈/寄存器，并正常 RET。没有向已停止的 GUI 线程同步发送采集或恢复消息。资源请求是独立运行时 IPC，不是测试程序的业务导入函数。

三个窗口以逻辑 ID 1/2/3 建模，记录类、控件 ID、所属线程、父子关系、样式和几何。恢复映射七个显式活跃 HWND 槽：数据页三个，叶函数两个，中间函数两个。已知活跃槽表和注册表必须完全一致；不扫描整数猜句柄。这项保证只覆盖声明的编译协作 ABI，不声称能发现任意未改造程序的隐藏引用。

## 场景

| 场景 | 验证 |
| --- | --- |
| positive | 随机 EDIT 文本、选区、修改标志、焦点及窗口关系恢复；真实计算续接；继续原生选择/输入/光标操作 |
| undo | 正常字符输入生成真实撤销历史，拒绝快照；源文本、选区及撤销状态保持 |
| unknown-child | 实际额外 STATIC 子窗口导致捕获拒绝 |
| bad-parent | 目标 BUTTON 实际属于 EDIT，父子关系不一致，拒绝重建 |
| unregistered-ref | 合作 ABI 声明七个活跃 HWND，目标漏登记一个，CPU 写回前拒绝 |
| omit-text / omit-selection / omit-modify / omit-focus | 分别遗漏字段恢复，由实际原生读回拒绝，不发布 View |

## 边界

只支持固定布局、默认配置、单行 ASCII EDIT、已知字体、无水平滚动及可判定为向前的选区。文本末尾的 EM_POSFROMCHAR 位置由最后字符位置与原生字体度量求出；单行消息 y 与 GetCaretPos 不直接等同，使用 EM_GETRECT 核验实际 y，并在重建后比较完整光标坐标。反向/未知光标、非空撤销历史、可检测的 IME 组合态、未知窗口类型/后代、字体或关系均拒绝。IME 实际交互未测试；没有支持 RichEdit、完整撤销、任意 EDIT 配置或任意控件内部状态。

EDIT 使用只读观察子类记录键鼠和原生查询，仍调用原始控件过程；没有替换控件键态查询或填写业务结果。输入模块完全复用原 R3-004；没有增加方向键等 API。图像助手是本实验私有副本，使用实际 PrintWindow 加原生 WM_PRINT/PRF_CHILDREN，明确不是 DWM/GPU 合成帧。脚本核对真实绘制、计数像素，以及排除闪烁光标列后的 EDIT 区域；不根据文本值拼图。

沿用 CET-002 的 `/Od /Ob0`、固定地址、已知调用帧和 CRT cookie ABI；原启动栈、TLS、完整 XSTATE 及 GetCurrentThreadStackLimits 的边界问题未解决。旧失败、宿主人工未执行和旧采集限制保持；不开始 R4。

实际源码身份、原始资源快照、图像、回归和分层判定见 [报告](results/20261025-r3005b001/report.md)。
