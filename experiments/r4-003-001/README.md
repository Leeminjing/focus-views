# R4-003-001：SA/SB 与 Artifact 的受限端到端整合

将 R4-002a 的执行/Undo 终态恢复与 R4-001 的 Action、原生应用文件操作、固定 Artifact 和 C 私有导入放进同一次运行。旧实验源码与历史证据保持不变；没有增加创建压力测试、键鼠 API、驱动或公共 SDK。

## 支持范围

沿用同构建固定地址、/Od /Ob0、单 GUI 线程、编译期协作 CALL 位置与已声明受管栈 ABI。CET、/GS、CFG 保持开启，逐目标读取实际 CET/CFG 策略并核对其自己建立的影子返回链。不是未经改造应用的透明恢复，也不覆盖完整堆/TLS/异常展开。

GUI 仍为固定顶层、单行标准 EDIT 和 BUTTON。仅支持从空 Undo 开始、全选后一次 ASCII 字符替换；BUTTON 保存不改变 EDIT 文本或已记录的 Undo。恢复后的 Undo/输入另行验证，不声称 Undo 后还能再次保存新的终态。

文件范围仅为应用在指定私有工作区通过同步 Win32 API 生成、已关闭的 `document.txt` 小文件。捕获记录内容、SHA256、来源 View/PID/Action 和原文件身份；不保存打开的 HANDLE、当前位置、锁、映射、异步 I/O、任意目录、ACL、ADS 或其他文件系统元数据，不声称通用文件系统隔离。

## 实际链路

1. 源程序随机生成未保存数据和原生 EDIT 文本；捕获 S0 后源继续变化并退出。
2. A/B 从同一 S0 真正续接，各编辑一次。通过受管鼠标点击真实 BUTTON，应用自行 CreateFileW/WriteFile/FlushFileBuffers/CloseHandle 产生文件。
3. 运行时依据实际文件完成回执和字节校验发布 A-v1/B-v1；随后关闭输入，在响应的 GUI 线程采集 Undo/控件，再捕获真实 SA/SB 执行现场和工作文件版本。
4. 原 A/B 确认退出。RA/RB 分别重建窗口、原生 Undo 和私有文件，通过正常 CALL 建立自己的 CET 返回链，恢复原始内存/栈/R10 并实际 RET。
5. C 从原 S0 续接，先核对原始文本、Undo、内存和空工作区，再导入两个固定版本的私有副本。应用实际 ReadFile，修改私有副本并生成依赖两个输入的结果。
6. RA/RB 原生 Undo 两次后继续输入，并通过 BUTTON 重新写各自私有文件；C 正常全选替换。三者继续运行/绘制，原工作文件、Artifact、S0/SA/SB 指纹不变。

`files.hpp` 只新增必要的实验私有文件版本层。捕获时要求实际完成回执、零应用文档句柄，以及对真实文件成功独占打开；只接受一个普通非 reparse、单硬链接文件。恢复在新 staging 目录创建新文件、写入/刷新/关闭、校验版本，再重命名到目标工作区；不是直接共享原目录。目标 GUI 或 CET 恢复失败则销毁未发布目标并回收本次创建的工作区。原始 Snapshot 字节只读，只有目标 HWND 引用使用显式重关联。

本实验复用 R4-001 已验证的常驻输入模块，支持拒绝快照后关闭、释放并重新建立通道；输入日志按通道 epoch 独立命名。检查点时无活跃按键/捕获，恢复后建立新输入通道，不保存活跃拖拽或悬挂键鼠状态。Action 的输入投递与业务完成分开：文件 effect_confirmed 必须有目标 GUI 线程的真实文件回执、关闭数和哈希，不能仅凭鼠标消息已投递。

## 构建和复现

```powershell
& ./experiments/r4-003-001/build.ps1 -MsvcRoot <MSVC-root> -SdkRoot <Windows-SDK-root> -SdkVersion <version> *> ./experiments/r4-003-001/out/build.log
& ./experiments/r4-003-001/test.ps1 -ResultsDir D:/new-results -Repeats 2
# 单项；目录必须不存在，无自动重试。
& ./experiments/r4-003-001/out/r305a.exe --output D:/new-case --scenario positive
```

脚本默认完整执行 R4-001 和 R4-002a 各自的旧回归链。`-SkipRegression` 只用于开发诊断，必须标记未运行。每次记录实际源码/二进制身份，不覆盖历史目录；失败保留原 API/错误及 summary，再跳过依赖材料。

七个场景：`positive`、`undo-unsupported`、`file-open`、`file-version`、`file-write`、`branch-failure`、`artifact-hash`。前者才是完整受限链路成功；其余准确检测预期失败，不计为成功恢复。file-open 是运行时测试侧持有的真实 GENERIC_WRITE 句柄，独占捕获须得到实际 sharing violation；它不证明能枚举任意第三方进程的隐藏句柄。版本错误只损坏恢复 staging 副本，写入错误用实际只读句柄触发 WriteFile 拒绝。RB 失败后不重试同一恢复，RA 和 C 继续工作。坏导入仍记 failed，之后是不同 Action 的独立正常导入。

原始材料包括 S0/SA/SB 数据页、完整/活跃栈、CONTEXT、返回链、元数据、资源/Undo 图、cookie ABI、程序身份、文件内容与来源清单，各自建立指纹；Action 账本、应用文件回执、固定版本及 C 私有文件另行保存。它们是实验材料，不是已发布的长期稳定 Snapshot 格式或跨崩溃持久性协议。

实际结果见 [正式报告](results/20261025-r4003001/report.md)。GATE-002 的偶发原生创建拒绝仍为独立可靠性门禁；本轮没有解决根因、没有新 ETW/压力循环。人工宿主交互仍未执行。即使端到端通过，也只声明本实验资源范围内的 R4 整合，不宣布通用应用恢复或项目完成。
