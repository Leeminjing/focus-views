# R5-004：真实管理器异常终止与恢复边界

**尚未通过。** 已取得提交前安全拒绝、提交后跨管理器恢复的真实成功证据；最新完整本体矩阵在恢复中再次崩溃后的新恢复操作遇到目标映像基址变化，执行恢复明确拒绝，停止点上的失败分支 Desktop 清理亦未确认。后续所有权门禁拒绝继续使用该尝试链。不能据此宣布管理器崩溃恢复关口完成。

## 实际运行

2026-10-27，Windows 11 x64 26200、普通用户、同一 Windows 会话；C++20、MSVC 19.44.35229.0、Windows SDK 10.0.18362.0。CET、/GS、CFG 和目标原有构建条件保留。没有关闭系统保护、修改影子栈、重放业务初始化、宿主输入注入或后台进程保活来冒充恢复。

最新原生构建/矩阵源码基线为 `3ec2f2726`；精确编译输入和二进制以 [build.json](build.json) 的 SHA256 为准。g 运行的脚本与只读 oracle 原字节分别为 [original-test-g.ps1](original-test-g.ps1)、[original-verification-g.ps1](original-verification-g.ps1)，与 [identity.json](identity.json) 一致。之后仅修正错误汇总守卫：在 GUI 证据缺失时先保留原生恢复错误，已有真实失败材料的 [只读核验](failure-guard.json) 通过；未启动新原生进程，也不计为恢复通过。

## 分开判定

最新 g 调度 63 个场景，实际启动 104 个管理器实例，1982 项证据检查中有 17 项失败。以下是场景结果，不能把预期拒绝算成执行恢复成功：

| 场景 | 数量 | 符合预期 | 未通过/未完成 |
| --- | ---: | ---: | ---: |
| controller-failure-summary | 1 | 1 | 0 |
| uncommitted-rejection | 32 | 32 | 0 |
| committed-crash-restored | 8 | 8 | 0 |
| restore-crash-then-new-operation | 6 | 0 | 6 |
| recovery-fault-or-isolation | 16 | 8 | 8 |

- **提交前崩溃：32/32 符合预期。** S0 半写/完整 staging、S0 发布、A Artifact、SA/SB 发布、Session 半写/完整 staging 共八个同步点，各应用各两轮真实 TerminateProcess。没有 Session 就拒绝整集合；已发布孤立对象与遗留 staging 不成为恢复来源。
- **提交后崩溃：8/8 完整恢复正例通过。** 分别在 Session rename 完成、Session 日志完成但 A/B 尚存活时强杀；不同 PID 的新管理器从磁盘恢复 RA/RB/C，真实执行原始栈/CPU 的 CET CALL/RET、Undo、受管输入、GDI、私有文件及固定成果处理。
- **恢复中再次崩溃：最新 6 个场景均未完整通过。** 最先的显式新恢复操作遇到下述映像基址不匹配，Desktop 消失未确认；之后相关 `.owner` 没有 `.done`，新管理器正确报 `recovery.owner_cleanup_unconfirmed`。该安全拒绝不替代原要求的成功恢复。
- **旧回归：实际通过。** v、f 两次各完整执行 R5-003/R5-002/R5-001b/R5-001a/R4-003 及它们的全部嵌套回归。f 的 [R5-003 汇总](complete-regression-summary.json) 与逐层 [regressions.json](regressions.json) 保留。g 只更改新监督器采样/诊断和相应只读检查，未重跑旧套件；旧构建、编译输入的相同性另行核对，不能写成 g 重新跑过回归。

[assessment.json](assessment.json) 的 `allRequiredEvidencePassed=false`，[原始最新 summary](summary.json) 的 `crashRecoveryVerified=false`。对未改变依赖的回归证据做绑定核对，不会将最新本体失败改成成功。R1/R2 与独立键鼠矩阵本轮未额外重跑。

## 新的直接阻塞证据

同一个目标 EXE 的 SHA256 未变化，但新 PID 的实际加载基址与 SA 中原调用关系的基址不同：

| 应用 | 原 A 的实际映像基址 | 新 RA 的实际映像基址 | 执行恢复拒绝码 |
| --- | --- | --- | ---: |
| fixture | `0x7ff77c1d0000` | `0x7ff745e10000` | 1306 |
| recorder | `0x7ff7438e0000` | `0x7ff71f9c0000` | 1306 |

对应 [原始数字和事件](image-layout-failure.json)、[fixture 拒绝日志](fixture-image-rejection-controller.jsonl)、[recorder 拒绝日志](recorder-image-rejection-controller.jsonl)。`restore.build_or_image_identity` 在写回原 CPU 前拒绝，View 未发布。没有移动数据后继续使用旧指针，没有关闭 ASLR，也没有将旧绝对返回地址改成猜测的新地址。**为何 Windows 在这一时刻选择不同基址，尚无直接根因证据。**

这同时触发第二个边界：目标已停在调试检查点，不能同步要求该 GUI 线程处理关闭消息。进程退出与自有句柄释放虽有证据，Desktop 仍未在期限内消失，监督器报告 `crash.desktop_retained/170`，不签发清理完成凭证。随后所有权日志明确阻断新恢复。不能把该情况与之前已响应 GUI 线程的资源重建失败清理混为一谈。

在最新运行全部结束后，另从最初发布记录独立核对了 **120 个固定 Snapshot、Session 或 Artifact 内容哈希，全部仍一致**：[post-failure-fixed-version-audit.json](post-failure-fixed-version-audit.json)。存储未污染不等于执行恢复已经成功。

## 已实现和已验证的受限机制

1. `FOCUS_VIEWS_CRASH_EXPERIMENT` 才启用持久化阶段观察点。独立监督器在 ResumeThread 前将管理器纳入自有 kill-on-close Job，单创建/调试线程继续使用原 PID/TID 事件泵；同步事件确认后真正调用 TerminateProcess。
2. 归属按 PID＋创建时间核对，staging 名称也含实例创建时间。只隔离本实例 `.partial`，保留原始字节/SHA256；同 PID 不同创建时间的合成归属反例明确标记，不冒充自然 PID 复用或真实崩溃文件。
3. 清理凭证绑定 Session SHA256；`<store>.owners/` 作为独立可变侧目录追加 `.owner`/`.done`。所有旧尝试均有清理证明且最新前驱相符才允许恢复。信任本地目录和外部实验监督器，不提供恶意目录认证、任意复制存储间的全局所有权保证或监督器自身崩溃恢复。
4. 自有进程、Job、助手、句柄和 Desktop 分别取证。诊断采样失败不阻止继续尝试终止自有资源；主要错误和清理错误分别保留。缺少必要 GUI 日志不以零记录误判清理成功。
5. 已响应、未发布窗口的已知资源失败路径，先由同桌面助手发送有期限的 WM_CLOSE，再用独立助手检查旧窗口身份不再有效，之后终止进程。dev-13 和 f 的两款应用实际通过 Desktop 消失核验。1400 表示原 HWND/PID/TID 身份无效，**不能仅凭它声称 IsWindow 返回 FALSE**；最终 Desktop 消失由监督器独立检查。

原生依据：[WM_CLOSE 默认处理](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-close)、[DestroyWindow 的所属线程及子窗口语义](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroywindow)、[SendMessageTimeout](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagetimeoutw)。这些文档不构成停止点清理或映像重定位已经解决的证据。

## 保留的失败过程

| 原始运行 | 检查数 | 失败数 | 完整旧回归 | 结论 |
| --- | ---: | ---: | --- | --- |
| v | 2895 | 2 | 通过 | RB 资源失败后 Desktop 消失未确认 |
| f | 3055 | 1 | 通过 | 早期拒绝的监督器遇到 QueryFullProcessImageNameW/5 |
| g | 1982 | 17 | 未重跑，未变输入已核对 | 映像布局不符、清理未确认及后续拒绝链 |

v 的进程/句柄释放顺序、枚举取代打开、外层观察进程等对照未解决红项，没有保留为成功路径。正常 GUI 线程 WM_CLOSE 的修正解决已知资源失败场景，但没有解决在调试停点上的资源销毁。

f 的错误来自 **QueryFullProcessImageNameW**，不是 CreateProcessW。新监督器仅在同一已核对 PID/创建时间/Job 的进程句柄确实被签退时允许映像附加诊断缺失，仍固定句柄并独立确认退出；活动进程查询失败继续报原 API/错误。g 未自然再次触发该分支，不能以未再出现宣布根因消除。旧原生创建 `CreateProcessW/5` 门禁仍独立保留，未追加压力或 ETW 轮次。

g 一些后续拒绝还曾被测试脚本缺失 controller.jsonl 的二次错误遮蔽；原始记录不修改。发布前修正和 `failure-guard.ps1` 的只读验证现保留 `recovery.owner_cleanup_unconfirmed/170`，不算新一次原生矩阵。

## 交付和复现

- [实验入口](../../README.md)、构建/测试脚本、原生源码；原始 summary、实际编译输入、完整管理器实例和 [statistics.json](statistics.json) 分别提供。
- 原始归档在 `experiments/r5-004/results/`，使用有界运输分片和逐文件长度/SHA256 清单，完整包拼接方法见 [evidence.md](evidence.md)。压缩后流式解包逐文件重新计算哈希，不依赖只看归档整体哈希；没有改写 Snapshot 或旧失败。
- `release-v`、`release-f`、`release-g` 保存实际运行二进制、编译记录和源码输入；当前错误守卫脚本与 g 实际执行脚本分开保留。原生包仅供已声明环境的复现实验，不能当作可跨机器恢复的稳定 Snapshot。
- 正例真实绘制示例：[fixture RA](fixture-RA-final.png)、[RB](fixture-RB-final.png)、[C](fixture-C-final.png)；[recorder RA](recorder-RA-final.png)、[RB](recorder-RB-final.png)、[C](recorder-C-final.png)。失败视图的准备画面不会计作恢复 Frame。

## 审查

### Standards

源审查指出并修正了采样失败阻断清理、锁失败缺 summary、原错误被二次诊断覆盖和原生参数字段不准确等问题。采样退出保护与未发布窗口清理已分别复核。最新实验仍有真实能力失败，没有用审查通过替代执行验收。

### Spec

提交前/后边界、实例归属、Session/尝试绑定和 staging 反例已实现；停止点上失败资源回收与可重复的执行映像布局仍未闭环。该关口维持未通过。

本轮没有验证宿主重启、断电、系统崩溃、任意应用、任意调用栈或持有按键/拖拽的 Snapshot。人工宿主交互未执行。接下来需分别验证可重复的执行映像布局与调试停点上的失败清理，保留原恢复目标和安全保护。
