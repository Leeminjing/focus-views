# R3-005a-CET-002：编译期协作式调用链续接

本实验以 CET-001 的失败为起点，在新目录验证：目标通过同一组真实 C++ CALL 位置建立自己的影子返回链，再由控制器恢复源的原始内存、栈及计算寄存器，从原检查点完成正常 RET。旧实验及其失败记录保持不变。

这是受控编译期协作实验。fixture 显式提供结构入口，跳过初始化及 outer/middle/leaf 的业务前缀；它没有实现业务状态导入。恢复全部由控制器执行，应用的正常后半段计算使用写回的原始字节。不能外推为未改造应用兼容性、原生 EDIT 恢复、多分支 GUI 或完整 R3。

## 构建与运行

在仓库根目录执行；使用 C++20、MSVC 和 Windows SDK，无额外运行环境：

```powershell
New-Item -ItemType Directory -Path '.\experiments\r3-005a-cet-002\out' -Force | Out-Null
.\experiments\r3-005a-cet-002\build.ps1 -MsvcRoot '<MSVC 根目录>' -SdkRoot '<Windows Kits\10>' -SdkVersion '<版本>' 2>&1 | Tee-Object '.\experiments\r3-005a-cet-002\out\build.log'
.\experiments\r3-005a-cet-002\out\r305a.exe --output '<全新最小实验目录>' --scenario core
.\experiments\r3-005a-cet-002\test.ps1 -ResultsDir '<全新完整结果目录>'
```

先运行无窗口/输入/采集的 `core`，通过后运行 `gui`。默认完整脚本每项重复两次，并保留各进程实际返回码。正向为 0；每个负向 EXE 为 2；总脚本只有正向恢复和反例检查全部正确才成功。

| scenario | 检查 |
| --- | --- |
| core | 源退出、新 PID 合法构造匹配链，恢复原始状态并完成真实返回 |
| return-mismatch | 目标改走另一处合法 CALL；实际链与 S0 不符，在写回前拒绝 |
| stack | 故意保留目标叶函数局部量原值；三层正常返回后计算不符 |
| register | 故意保留目标 R10 哨兵值；三层正常返回后计算不符 |
| memory | 故意保留目标数据页 seed；三层正常返回后计算不符 |
| cookie | 不恢复旧 CRT cookie；单独观察 /GS 实际失败，不遮蔽其余负向 |
| gui | 同一续接机制接入原固定窗口、三个 HWND 引用和现有 GDI/输入模块 |

## 明确的编译协作

- fixture 以 `/Od /Ob0` 保持可检查的 CALL 位置，函数仍为普通 C++，含 volatile 局部数组、正常函数序言/结尾和 /GS；其他单元 `/O2`。全程启用 /GS、/guard:cf、/CETCOMPAT。
- 结构入口不执行 BCrypt 业务初始化或三个业务前缀，只建立同样的帧和零值局部量。控制器独立记录实际初始化/前缀断点，目标不能通过恢复内存中的计数器自证。
- 检查点汇编仅源路径生成 R10 中间值；目标结构路径设置不同哨兵。双方在同一 CALL 位置进入同一检查点；另一 CALL 位置仅用于反例。
- 引导汇编直接 CALL 固定 C++ 入口，未使用不受 CFG 检查的间接入口。没有 RET→JMP 替换；检查点前普通分支用于选择协作路径，检查点后仍执行 MOV、RET 以及原 C++ 返回。
- 控制器用 Windows unwind 元数据解析五个返回位置，与实际读取的 CET_U/SSP 和影子栈逐项比较；检查来源、RSP、局部量地址及构建身份后才写回。写回后再次验证返回链。CET_U 与影子栈仅被读取，R11 保留目标自己的诊断值；不移植源 SSP。
- 普通栈及 MSVC cookie/complement 沿用旧实验的显式 ABI。恢复 cookie 是为了让恢复的原 /GS 帧继续接受真实检查，未禁用检查；单独反例验证其仍执行。

栈仍为固定地址、预提交的运行时栈，保留旧实验对 NT_TIB 边界的协作设置；GetCurrentThreadStackLimits 的不一致、原生启动栈、TLS、跨引导异常、自动扩栈及完整 XSTATE 没有在本轮解决。源与目标必须同机、同构建、同映像/受管地址布局。

GUI 仅有一个自绘 GDI 顶层窗口，重新创建资源并映射三个显式 HWND 槽。输入 DLL、输入桥及 PrintWindow 助手复用未修改的 R3-004/R3-001 源码。程序先启用私有业务处理以验收重绘和输入，完成这些检查后才记录可用 View 发布；失败不交付可用 View。

实际运行、原始字节、调用链、反例、图像和边界见 [实验报告](results/20261024-cet002/report.md)。
