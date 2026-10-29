# R5-004c：停止点清理的同启动周期对照

本实验研究 R5-004b 仍保留的清理红项，不改变重定位内核。候选与 inline 对照在同一份新 DLL 中选择；R5-004a 的正式实现、历史结果与 ASLR 实现保留。**功能验证与因果修复分别判定**，没有复现自然故障时不能因两组均通过就宣布修复。结果见 [报告](results/r5004c/report.md) 和 [分层 summary](results/r5004c/summary.json)。

## 边界

- 原样复用 R5-004b manager、controller、kernel、两款应用、GDI/输入组件，记录 SHA256。只构建实验客户端、清理助手、候选 DLL 和有界诊断助手；不是新 SDK。
- 目标仍响应时安装线程级钩子并固定 PID/TID/创建时间；使用独立 magic、长度和版本的清理通道，不能将单个 DLL 混入旧构建。
- 只允许干净、未发布且停在所属 checkpoint 的目标。实际异常上下文 RSP/R11 须匹配停止时记录；首次源状态写入后沿用原 dirty 拒绝，不分发清理回调。
- deferred 的 VEH 只验证、记录固定槽并将 RIP 指向本 DLL 的不返回入口；VCH 只观测。窗口销毁和退出由原 GUI 线程执行，不写源 SSP、影子栈或业务数据，不以该路径代替执行恢复。
- runtime-sp.asm 只读观察调用点的 RSP/硬件 SSP，使用正常 CALL/RET。阶段 4→5 必须核对 SSP；C++ 入口自身的普通栈帧分配与异常上下文 RSP 区分记录。
- 超时先固定失败，再启动同 Job 的诊断进程，最多 1,500ms 采集及 500ms 签退。只暂停/采样该已核对进程的线程，记录实际线程创建时间、上下文、符号类型和错误；未知符号不是根因。诊断失败或被回收不能覆盖首个清理失败。无转储上传。
- 只有独立监督器核对进程实例、Job、自有句柄和 Desktop，才允许物理清理凭证；最终强杀不能把原生清理的 verified=false 改成 true。

## 阶段

| 值 | 含义 |
|---|---|
| 1 | 目标 GUI 线程准备完成 |
| 2 | 实际 VEH、原 RIP/RSP/R11 |
| 3 | deferred 请求 Windows 续接到固定清理入口 |
| 4 | 实际 VCH、续接上下文 |
| 5 | 清理函数入口、真实硬件 SSP 与该函数栈帧 |
| 6 / 7 | DestroyWindow 开始 / 返回，失败错误码先于日志采集 |
| 8 | ExitProcess 调用前；真正进程退出另由调试器及监督器确认 |
| 10 | 仅人工阻塞反例：真实 WM_DESTROY 到达自有未释放事件 |

inline 不产生 3/4；进入 5 时仍在 VEH 调用栈。阶段写入使用固定容量和发布标志，溢出拒绝，不丢弃未读记录。目标侧记录八个已知模块的实际路径/基址/查询错误；不将该列表称为任意模块兼容性。

## 复现

先按 R5-004b 的说明准备其 MSVC/SDK、自研依赖及构建输出。显式指定新短证据目录，不能覆盖原始失败。

~~~powershell
./experiments/r5-004c/prepare-combination.ps1 -Root C:/fv-c/new-builds
./experiments/r5-004c/test.ps1 -OutDir C:/fv-c/new-builds/candidate -ControlOutDir C:/fv-c/new-builds/control -InputOutDir C:/fv-c/new-builds/b001 -CetOutDir C:/fv-c/new-builds/cet -QueryFailureOutDir C:/fv-c/new-builds/query -FrozenInputs C:/fv-c/new-builds/frozen-inputs.json -Profile C:/fv-c/new-builds/resource.bin -ResultsDir C:/fv-c/newrun
~~~

test.ps1 显式执行：防重复/必要失败链验收 → 聚焦对照 → R5-004a 完整独立断言矩阵 → R5-004b 矩阵 → 原 R5-004 崩溃矩阵 → 完整旧嵌套回归。test-cleanup.ps1 保留原 R5-004a 的断言正文，仅切换为当前磁盘版本 2 的既有 oracle。prepare-combination.ps1 在测试前准备全部构建及既有 acknowledged-ring 依赖 profile，备份被替换的旧构建；测试阶段不再编译或替换依赖。默认构建、候选、输入诊断变体分开，源码/脚本、EXE/DLL/库/PDB、资源 profile 逐文件 SHA256 冻结，每阶段前后核对。旧版本 1 磁盘套件不使用候选版本 2 二进制。

失败后的关闭对照使用同一用户 LocalAppData/focus-views/failure-followups 下的不可覆盖记录，按原管理器 PID＋创建时间集合和完整启动时间生成身份。真实入口在启动窗口前以 CreateNew 原子取得执行权并 flush，原始 summary/log/proof 哈希保存在记录中；成功、失败或中断都不删除记录。改变结果目录不能重新获得执行权。原失败材料不修改；缺失证明或启动周期变化仍先拒绝，人工阻塞不能冒充历史自然等待。

资源 profile 必须由当前包的 recorder S0 经已有 profile.exe 提取，不能沿用旧 EXE 的 profile。准备阶段先独立确认源进程清理，入口再验证 profile 自身哈希及绑定的 EXE SHA256。旧默认套件的历史源码审计只额外认可 input.cpp 在 1ef786b 的已核对提交版本；该文件有后续未批准修改、其他旧实现或历史证据变化仍拒绝。默认构建与启用输入诊断的变体分别记录，不因审计认可而启用宏。

test-focus.ps1 对三/四帧应用交替运行 inline:/deferred:，核对真实阶段、GUI TID、未写源现场及硬件 SSP。同一最终候选包用于本体组合复验。外部调用 test-one.ps1 新增的可选前缀默认空，不改变旧用例。

## 两类人工对照

~~~powershell
./experiments/r5-004c/build-probe.ps1
./experiments/r5-004c/test-probe.ps1 -ResultsDir D:/fv/c/newprobe
./experiments/r5-004c/test-diagnostic.ps1 -OutDir D:/focus-views/experiments/r5-004c/out -ResultsDir D:/fv/c/newdiag
~~~

probe 为独立原生控制器/目标，两者都不执行 Snapshot 恢复。由 WM_DESTROY 的事件协调工作线程：普通完成、真实抛出并处理 C++ 异常、增删 VEH。没有随机延时触发；工作线程完成期限为 1 秒，监督期限有界。两种模式都成功表示假设未得到该反例支持，不能造一个预设失败当作根因。

blocked: 仅实验性地在自有窗口过程等待自有事件，强制产生真实 DestroyWindow 阻塞，验证两秒诊断、错误保留和故障隔离。它不证明历史 TSF/COM 等待来自相同原因。原始用例的失败 summary 必须保持 false；另一个 assessment 只判定反例检测及诊断是否符合预期。

没有宿主重启、断电、任意 GUI、未知资源或整体发布可靠性声明。人工宿主交互未执行，CreateProcessW/5 门禁独立保留；不进入 R5-005。
