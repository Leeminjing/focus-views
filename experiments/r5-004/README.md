# R5-004：管理器强制终止后的提交边界与清理

沿用 R5-003 的磁盘格式、同构建/固定地址、ABI v1 三帧与 v2 四帧、单 GUI 线程、top/EDIT/BUTTON、一次 ASCII 全选替换 Undo 和关闭同步普通文件。不扩大恢复资源范围，不研究宿主重启、系统崩溃或断电。

`manager.exe` / 两个客户端 DLL 复用既有执行、资源、成果与恢复编排。仅显式编译 `FOCUS_VIEWS_CRASH_EXPERIMENT` 时启用同步观察点；普通 R5-003 的提交与写入路径保持原行为。新实验在所有数据固定后、A/B 仍活着时发布 Session，再进入清理，以实际检验提交后的崩溃。

独立 `controller.exe` 在 ResumeThread 前将 manager 纳入外层 Kill-on-close Job，子进程按原生嵌套 Job 归属继承，防止创建/加入内层 Job 之间的归属空隙。达到匹配事件后直接调用 TerminateProcess，不抛异常模拟。控制器按 PID+创建时间固定实例、查询实际 Job 成员和退出、关闭观测句柄，再验证 Desktop 对象消失。若内层自动清理不完整，只终止此控制器拥有的 Job，并单独标记显式接管；不将其冒充自动清理。

退出证明使用实验内部有界、SHA256 校验的 `owner.fvc`，绑定 Session 哈希。`<store>.owners/` 在可变所有权侧目录中追加 PID＋创建时间的 `.owner` 及实际清理后的 `.done`；它不属于不可变 Snapshot/Artifact。新操作持独占锁，登记当前实例后才允许 manager 执行；后续 manager 检查全部前驱清理、最新凭证和旧 Desktop 缺失，随后只读取原 Session/快照。任何未确认清理的尝试都会阻止该存储的继续恢复，不以旧的有效凭证跳过失败尝试。证明文件不是应用状态，不携带可复用进程/线程/窗口句柄。仅可信本地目录，完整性校验不是认证。

## 故障矩阵

- 提交前：S0 staging 半写/验证后、S0 发布、A Artifact 发布、SA/SB 发布、Session staging 半写/验证后。新 manager 必须拒绝未提交集合。
- 提交后：Session rename 完成但日志尚未返回，以及 Session 日志完成但 A/B 尚未清理。验证新 PID RA/RB/C、真实 CPU/栈/CET 返回、Undo、GDI、输入和成果私有处理。
- 恢复中：RA 原生资源准备后、RA 发布后、RB 发布后。确认固定存储不变，再由不同 manager 的显式新操作恢复；没有内部重试失败恢复。
- 额外拒绝/隔离：真实完整 staging 残留、截断已提交文件、缺 Session、RB 原生资源重建失败。staging 永不自动提升；属于已确认退出写入者的 partial 进入独立隔离目录，原字节/哈希保留。

## 构建与测试

```powershell
./experiments/r5-004/build.ps1
./experiments/r5-004/test.ps1 -ResultsDir D:/focus-views/experiments/r5-004/results/v
```

需要既有 R5-003 out 的工具链记录与构建依赖；已相同的依赖不重复覆盖，避免无必要地更换目标映像文件对象。所有构建必须在测试开始前结束。测试复用 R5-003 的只读原始字节、CET、Undo、文件、像素与 Action 判据，不运行其驱动部分；最后实际运行 R5-003 及其完整嵌套回归。`-SkipRegression -Repeats 1` 仅为开发矩阵，不可声明完整回归通过。旧原生证据写入器的 MAX_PATH 边界继续明确预检。

## 当前状态

已编译并实际运行最小 staging 拒绝和提交后恢复；首轮 34 个、后续 42 个开发场景均保留原始 false summary：两款应用的 RB 资源失败场景均未通过 Desktop 消失核对。进程退出、观测句柄/Job 释放顺序调整、枚举替代打开及独立监督阶段的有界对照均未解决该红项，未保留无效绕行作为成功路径。首轮正式矩阵 63 个场景、2,895 项检查中仍有上述两项红项；完整旧回归通过。随后在已响应且未发布的窗口失败路径增加目标 GUI 线程原生 WM_CLOSE 与 HWND 失效核对，最小 fixture 实验通过；新双轮矩阵和完整回归尚待完成，不提前宣布 R5-004 通过。历史 CreateProcessW/5 门禁独立保留，人工宿主交互未执行。

原生依据：[TerminateProcess 与等待退出](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)、[嵌套 Job 的归属和终止](https://learn.microsoft.com/en-us/windows/win32/procthread/nested-jobs)、[进程结束与对象引用生命周期](https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-process)。文档依据与本机观测分别报告，不从 Job 标志本身推导恢复或清理成功。
