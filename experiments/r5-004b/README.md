# R5-004b：同构建、异映像基址的协作续接

独立实验，保留 R5-004/004a 的失败与原始地址。Windows 11 x64、MSVC C++20、CET/CFG/GS/ASLR 保持开启。只改变主 EXE 的真实加载基址；受管数据、栈地址和资源 profile 不变。没有指针猜测、影子栈写入或源 SSP 写回。

## 首先复现

`build-probe.ps1` / `test-load.ps1` 复制现有 EXE 的原始字节。两个原生 CreateProcessW 的 CREATE_PROCESS_DEBUG_EVENT 给出不同实际基址，均在执行入口前终止并排空退出事件。复制不是执行恢复证据，也不保证任意 Windows 版本必然采用不同地址；如果基址相同，该次测试失败，不重试选择布局。

`test-baseline.ps1` 让旧内核正常保存 Session，再用相同 SHA256 的另一文件对象恢复。首次实跑得到 restore.build_or_image_identity / 1306；监督器确认旧管理器、应用、Job 和 Desktop 已清理。

## 实验 ABI 与重关联

旧 Metadata v1/v2 字节布局不变。新应用额外导出独立版本的 `R5004ImageReferences`。没有该声明的应用仍只有同基址路径，不自动获得异基址能力。仅 `FOCUS_VIEWS_IMAGE_RELOCATION_EXPERIMENT` 构建启用新 Snapshot 扩展；磁盘封装版本为 2，旧构建仍使用版本 1，尚无稳定协议声明。

| 位置 | 来源和核验 | 准备方式 |
|---|---|---|
| RIP、停点、函数入口、调用者边界、cookie 地址 | 已有执行 ABI、实际目标 PE、RVA 边界与目标声明 | 目标基址 + 源 RVA |
| 普通返回槽 | 源真实 PE unwind 顺序及保存的返回向量 | 只修改已核实的槽；与目标原生 CALL 建立的普通/影子链逐项匹配 |
| 栈中的 Metadata 指针、间接调用函数指针 | 活跃应用帧显式登记地址与 RVA，源/目标槽原值核对 | 仅目标私有栈副本变换 |
| 数据页中的函数地址 | 显式登记；实际执行会核对并使用它 | 仅目标私有数据副本变换 |
| R8 中的函数地址 | 本实验显式寄存器约定，目标检查点真实装载 | 仅普通 CONTEXT 的该寄存器重关联 |
| R10 与栈局部计算值 | 原始真实计算现场 | 不重算、不重定位 |
| R11/SSP | 目标自身正常 CALL 建立的链 | 沿用已验证约定，保留目标值，只读核对 CET_U |
| HWND | 原有七处登记与原生资源重建 | 原有独立资源映射 |

两个应用仍使用 /Od /Ob0、三/四个受控 C++ 帧。`prepare-apps.py` 的生成源可逐行审查：只添加独立引用 ABI、显式可寻址指针与调用验证，不替运行时保存/导入业务状态。不是未改造应用支持。其他寄存器、DLL 引用、优化器隐藏指针、异常展开、多线程和数据/栈搬迁不在支持范围。

准备阶段只读取目标，在管理器副本中处理。身份、RVA、原值、执行结构或 CET 关系失败时不写源现场。真正开始内存、栈、cookie 或 CPU 写入前仍推进 R5-004a dirty 标记；写入后禁止目标侧停止点清理回调。

## 构建与测试

先准备已验证的 R5-003/R5-004a 构建依赖，然后执行 `build.ps1`。复用的目标资源库、输入 DLL 与助手必须记录 SHA256。新清理助手与 DLL 从当前共享通道头同时构建，不能混用不同通道布局的开发包。

`test-one.ps1 -Application fixture -ResultsDir D:/fv/b/new1` 先运行单条三帧正例；`-Application recorder` 为四帧。`-KillRestore view-published:RA` 在确定同步点真实强杀 Manager 2，再由新 Manager 读取原 Session。输出目录必须全新，失败日志保留；新操作具有独立 PID 与归属凭证，不重试失败操作。

实际结论见 [正式报告](results/r5004b/report.md) 与 [分层汇总](results/r5004b/summary.json)。最终同目录矩阵 16 场景、1,958 项主检查通过，三/四帧均验证；最终构建的原 R5-004 63 场景、3,174 项检查通过，完整旧嵌套回归已在开发轮实跑，最终轮未重复。

前轮同目录矩阵和独立 R5-004a 回归再次出现停止点清理超时，原始 false summary 保留。当前启动周期两个清理入口都成功，不能据此宣称因果修复，因此整体 `fullR5004Passed=false`、`gateClosed=false`。历史 CreateProcessW/5、人工宿主验证、宿主重启与断电能力不因此升级。

`verify-evidence.py --deep` 只读核验所有归档/分块及逐文件长度和 SHA256；不把解包验证计作原生实验。`diagnostic/` 与 `build-cleanup-diagnostic.ps1` 是隔离的清理研究，`-Deferred` 为未集成的候选入口；不得与正式构建混用通道尺寸。本地转储不随证据发布。测试输出目录须用新的短路径；完整嵌套回归有原有路径长度限制。
