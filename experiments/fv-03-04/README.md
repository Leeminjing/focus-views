# FV-03/FV-04：恢复分支持续交互与预览独立性

独立补充实验，不是 R5-005，不关闭历史 R5-004 可靠性门禁。不增加恢复资源或输入 API。

复用 R5-004c 受限恢复客户端：先真实保存 S0/SA/SB，确认源、A/B 和保存管理器退出；新管理器从磁盘恢复 RA/RB/C，完成原始 CPU/CET 返回、原生 Undo、文件及成果验证。只有全部恢复并发布后才进入有界持续交互阶段，没有另造冷启动演示应用。

## 构建与自动验收

PowerShell 7，Windows 11 x64，MSVC＋Windows SDK：

    ./experiments/fv-03-04/build.ps1 -Source C:/fv-c/cm/candidate -OutDir C:/fv-c/live/new-build
    ./experiments/fv-03-04/test.ps1 -OutDir C:/fv-c/live/new-build -ResultsDir C:/fv-c/live/new-result -Suite full

Source 必须是已有验证过、组件匹配的 R5-004c 执行/清理包，含 manager/controller、两款应用、kernel/store/native 对象、输入 helper/runtime304、abort-helper/abort-runtime 和 diagnose。脚本保留复用来源、各组件哈希及新客户端/预览身份；不混用旧资源 profile。源与输出目录必须不同，已完成构建和证据目录拒绝覆盖。

新编译两个客户端 DLL 和 auxiliary.exe。恢复内核、应用、输入桥及清理运行时按字节复用。FOCUS_VIEWS_RESTORED_INTERACTION_EXPERIMENT 仅在新客户端启用；旧默认构建不增加交互阶段。原采集路径只在此宏下返回已有真实采集回执，不改变其原生调用。

自动流程：

1. 三个恢复 View 交错进行原生 EDIT 选择/输入、原窗口字符处理和真实 BUTTON 文件操作。每次核对另两个分支的非时钟状态不变，三者实际计算计数继续增长。
2. 不创建预览时先执行；再创建只读预览，显示 RA/RB/C。
3. 正常关闭预览，继续操作并取新帧。
4. 重新创建预览，明确强杀这个自有预览作为故障；不重建任何 View，继续操作。
5. 第三次打开预览，核对新采集时序、序号和 SHA256，最后关闭预览，分支仍继续工作。
6. 退出管理器时执行原有清理；独立监督器确认进程实例、Job、自有句柄和 Desktop，不能留后台进程冒充独立性。

full 对两款应用执行上述自动场景，再执行两款不启用持续阶段的默认恢复流程；另验收真实输入桥退出和监督器在 live-ready 原生同步点强杀管理器。预期失败单独记录，不算持续交互成功。minimal 仅运行 fixture 自动场景。

## 只读预览边界

预览在宿主 Desktop 上显示，用 WS_EX_NOACTIVATE、SW_SHOWNOACTIVATE，不切换 Desktop 或注入宿主输入。GUI 是 Win32/GDI，不含框架。窗口只消费 manager 的 Frame；无目标进程控制句柄或输入能力。

Frame 由已有目标 GUI 线程采集路径生成 PNG，包含 View、PID＋原生创建时间、TID、独立序号、采集区间、真实绘制计数、SHA256。预览以 FILE_MAP_READ 映射帧源并核对 PAGE_READONLY，以 WIC 解码原图后 GDI 绘制；显示确认来自实际预览 WM_PAINT。无预览也继续采集。关闭后重开必须读取关闭后新采集的帧。

预览有独立的控制/回执 mapping，不能写 Frame feed。帧或生产者时序超过三秒时清除旧画面并显示 unavailable；不把缓存图叫作新帧。这个时限是本实验显示策略，不是通用实时保证。生产者 PID＋创建时间固定；预览不是 View 心跳来源。

## 真人宿主验收

    ./experiments/fv-03-04/test.ps1 -OutDir C:/fv-c/live/new-build -ResultsDir C:/fv-c/live/human-new -Suite manual

真实恢复完成后，会出现原生专用宿主窗口并维持约 60 秒的后台分支操作（当前有界操作完成后退出）。在该窗口移动鼠标、点击、输入窗口显示的挑战文字；至少观察 20 秒后，如确实没有意外焦点切换、光标跳转或输入串入，再勾选声明并点击确认。可切换预览查看各分支，不需操作无关程序。

只记录专用窗口的按键/字符/鼠标事件计数、挑战是否匹配及明确的人类确认，不记录输入正文。manager 仅按操作边界采样宿主前台 HWND、光标、输入 Desktop、最后输入时间，不采集其他窗口标题或内容。静止采样只报告有限观测；有外部活动或变化记 inconclusive，不推断谁导致，也不能替代人工验收。

无人参与或未确认时 manualHostTest=not_executed；有确认却未满足条件为 not_verified。禁止脚本模拟真人输入并记为人工通过。人工界面代码存在不等于已经完成真人验收。

## 支持范围

现有 ABI v1 三帧 fixture 和 ABI v2 四帧 recorder、显式协作、已登记映像引用、单 GUI 线程、既有 EDIT/BUTTON/GDI/文件子集。多次实时编辑可以继续运行，但不尝试保存此时不支持的多操作 Undo 快照。没有增加持键/拖拽中快照、未知控件、GPU、任意第三方应用、稳定 SDK、管理器服务或宿主重启能力。

历史关闭等待、输入钩子 /126、创建 /5 的因果问题不因本次自动结果而解决。
