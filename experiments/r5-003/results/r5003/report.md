# R5-003：Snapshot 持久化与正常退出后的跨管理器恢复

受限通过。两款显式协作应用均在 Manager 1 和其源/A/B 进程退出后，由不同 PID 的 Manager 2 读取磁盘 S0/SA/SB，真实恢复 RA/RB，并从原 S0 恢复 C。原始执行现场、正常 CET CALL/RET、原生单次 Undo、受管输入、目标 GDI 绘制、独立工作文件及固定 Artifact 的私有处理在同一次运行中通过。

## 实际运行与身份

2026-10-26，Windows 11 x64 build 26200、普通用户、同一 Windows 会话；MSVC 19.44.35229.0、Windows SDK 10.0.18362.0、C++20。CET、/GS、CFG 保持启用，没有修改系统安全配置。编译信息与逐文件源码/二进制 SHA256 见 [build.json](build.json)，测试脚本的原始字节与身份见 [original-test.ps1](original-test.ps1)、[test-identity.json](test-identity.json)。正式测试源码 HEAD 为 `aa2ac08ebabbfa2d5c1be99127188305d4eaf52d`；原生构建记录的 sourceHead 是构建时 HEAD，精确输入以 sourceInputs 的逐文件 SHA256 为准，打包时全部重新核对一致。

归档器在原生测试结束后仅优化了文件读取缓冲、独立文件并行校验和原生 xz 压缩线程；没有修改实验源码、快照或日志。测试时的归档脚本原字节另存 [tested-archive.ps1](tested-archive.ps1)，每份归档清单固定实际归档器 SHA256。正式测试脚本仍逐字节匹配 test-identity，原生包的编译输入/二进制校验也保持通过。

同一 manager.exe SHA256：`09d90818f4a9e07c435b393738a098b20f35e83c346b237b32a5db7402c30627`。恢复 kernel.lib：`d9d0a53c5d50f1cfc8d52a2a5280a0acdb24ebc8fdfb832bacb8a139752bc181`，与本轮 R5-002 构建产物一致。ABI v1 三帧/五返回 fixture.exe 与 ABI v2 四帧/六返回 second.exe 沿用前轮构建；业务观察/oracle 留在两个客户端。helper/runtime304.dll 是为明确的诊断传输修正单独重建的产物，不能称为原输入组件逐字节复用。

## 结果分别判定

- 跨管理器本体：22 个场景、30 个管理器实例、1051 项本体证据核验符合预期。只有 6 次是完整恢复正例（4 次标准流程、2 次空闲诊断对照）；2 次是 RB 资源重建故障隔离；14 次是磁盘加载明确拒绝。
- 整套脚本：1052 项核验全部通过，最后一项是完整旧回归汇总；不能将它理解为 1052 项产品能力。
- 原生本体另记录 1090 项控制器断言、142 个自有进程、48 个 Desktop、150 张真实 Frame，均有实例清理和绘制来源核对；磁盘发布 24 个 Snapshot、读取 40 次。详见 [statistics.json](statistics.json)、[summary.json](summary.json)。
- [regressions.json](regressions.json) 逐层列出实际执行结果。没有额外重跑 R1/R2 或独立键鼠矩阵，人工宿主交互未执行。

| 原始 summary 路径 | 场景数 | 该脚本检查数 | 通过 |
| --- | ---: | ---: | --- |
| `r5-002-regression/summary.json` | 0 | 20 | True |
| `r5-002-regression/checkpoint-guard/summary.json` | 0 | 10 | True |
| `r5-002-regression/fixture/summary.json` | 20 | 3934 | True |
| `r5-002-regression/legacy/summary.json` | 20 | 4030 | True |
| `r5-002-regression/r5-001b-regression/summary.json` | 0 | 14 | True |
| `r5-002-regression/r5-001b-regression/fixture/summary.json` | 20 | 3766 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/summary.json` | 20 | 3710 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/summary.json` | 14 | 2519 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-001-regression/summary.json` | 8 | 2860 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-001-regression/c001-regression/summary.json` | 6 | 670 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-001-regression/c001-regression/b001-regression/summary.json` | 18 | 280 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-001-regression/c001-regression/b001-regression/cet002-regression/summary.json` | 14 | 285 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-002a-regression/summary.json` | 20 | 1674 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-002a-regression/c001-regression/summary.json` | 6 | 670 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-002a-regression/c001-regression/b001-regression/summary.json` | 18 | 280 | True |
| `r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-002a-regression/c001-regression/b001-regression/cet002-regression/summary.json` | 14 | 285 | True |
| `r5-002-regression/r5-001b-regression/recorder/summary.json` | 22 | 4248 | True |
| `r5-002-regression/recorder/summary.json` | 30 | 6160 | True |

嵌套汇总与子套件检查不可重复相加成为“唯一能力数”。R5-001a、R5-001b、R5-002 的回归明确使用已记录的有界诊断 helper/DLL 配置；管理器、应用及恢复库没有因此修改，旧断言没有删减。依赖来源与原版本备份见原始归档内 `regression-input-profile` 及相关 reuse.json。

## 独立于旧管理器的证据

1. PowerShell 启动 Manager 1，记录 PID、创建时间和实际退出；源/A/B 的退出由实例生命周期审计确认。正常退出后将 Manager 1 证据目录移到 `manager1-offline`，旧路径不存在，再启动 Manager 2，不以同一管理器内的顺序函数调用冒充重启。
2. Session 最后发布，固定三个 Snapshot 包的 SHA256、执行指纹、Artifact 来源/版本/Action/数据与清单哈希、旧实例 PID+创建时间。Manager 2 检查旧实例退出并先验证全部必要磁盘对象，再创建 View；不反序列化旧 C++ 容器指针、进程句柄、HWND 或调试事件。
3. 解码后逐字节比较原始 memory、完整 managed stack、CPU CONTEXT、Metadata、ExecutionLayout、实际返回向量、原生资源/Undo、文件内容/版本和目标诊断映像。应用独立 oracle 再核对原始内存、栈局部量和 R10 的计算依赖，未重新执行业务初始化或检查点前计算。
4. 新应用进程自己通过正常 CALL 建立 CET 返回链，使用目标 PE/unwind 核对原始返回关系并实际完成 RET；源 SSP 仅保留诊断，不写入目标影子栈。新窗口/7 个声明 HWND 引用、Desktop、输入通道和关闭工作文件分别重建，RA/RB/C 文件对象相互独立。
5. 实际原生 Undo 两次、后续输入、应用普通 ReadFile/WriteFile 处理两份 Artifact 与工作文件重写均有目标效果确认。整个固定存储前后清单/哈希不变，C 不写回其他分支或 Artifact。

实例身份见 [manager-identities.json](manager-identities.json)，每次磁盘发布/加载哈希见 [disk-snapshot-identities.json](disk-snapshot-identities.json)。原始 CET、原生查询、Action、文件 API 和绘图日志完整保存在逐字节校验的原始归档中；另保留一组未修改的 [Manager 2 调试日志](recorder-manager2-controller.jsonl)、[Action 日志](recorder-manager2-actions.jsonl) 和 [清理审计](recorder-manager2-lifetime-audit.json)。

## 磁盘格式与明确拒绝

实验格式 1 的 magic/版本/架构/种类/长度与 SHA256 全部校验；整包最多 128 MiB，各资源还有独立长度上限。按字段/元素重建运行时对象，固定原生记录须匹配当前 SDK/ABI 大小。私有 CREATE_NEW staging 全量写入、FlushFileBuffers、关闭、完整读回后，同卷 rename 发布且不替换旧版本；Session 为最后可见提交标记。具体顺序见 [format.md](../../format.md)。哈希只用于完整性，不提供恶意存储认证。

缺包、内容篡改、截断、未知磁盘版本、错误 Runtime ABI、错误目标 EXE、Artifact 哈希错误均拒绝加载且不发布 View。RB 的资源父子关系故意错误时，原生适配器实际拒绝重建；清理失败分支后 RA/C 继续输入/绘图，固定对象不变。native codec 四次各 22 项检查包括长度/边界、不可覆盖发布、FIFO 环绕及未确认满环拒绝；不是只篡改预期结果。

## 首轮红项与本轮修正

- [开发期原始记录索引](../history.md) 保留首次错误失败注入、真实查询配额耗尽和 ASLR 基址变化后的正确拒绝。后者仍是固定布局限制，没有禁用 ASLR、猜测重定位或重试 Snapshot。
- [第一次正式运行](../20261026-r5003/report.md)：本体通过，R5-002 两个旧反例的输入诊断总量达到 8192 后 API/111、通道及清理失败；原始 false summary 未改写。
- [第二次正式运行](../20261026-r5003-verified/report.md)：本体和 R5-002 通过，但更早的 R5-001b 仍用旧诊断配额，另有深层 CET 证据路径达到 262 字符导致文件缺失；同样保留 false。目录的 verified 标签不代表本轮成功。
- 输入查询记录现在是明确启用的 8192 槽单生产者/消费者传输；桥写出并 flush 才 ACK，空闲/等待回执时同样消费，不覆盖未确认数据、不抽样。GUI 对照真实越过原上限后仍健康并继续业务：fixture: 28632 条，health=1，bridgeAlive=True；recorder: 27580 条，health=1，bridgeAlive=True。旧默认宏关闭路径的真实 8192/111 失败、满环拒绝和 DLL 文件锁下的依赖事务回滚均保留。
- 对 R5-001a/b/002 使用事务式显式诊断依赖配置，失败保留原错误并逐字节回滚。旧深层 CET 写入器仍受 MAX_PATH 限制：当前入口在启动任何应用前拒绝过长证据路径，正式运行使用短路径；没有修改系统 LongPaths 设置。原生 CET 同一二进制短路径 14 次/285 项完整对照通过，不宣称任意长路径支持。

没有用删除断言、隐藏错误或自动重试把前两轮红项改成绿色。可靠性 `gateClosed=false` 仍然保留：历史 CreateProcessW/5 未解决，本轮没有继续压力或 ETW 循环。

## 原始材料与复现

- [完整原始归档与下载](../archives.md) 与 [逐文件 SHA256/长度清单](../r5003-raw.tar.xz.manifest.json)：解压后每个文件数量、长度和哈希与本机原件重新核对。报告和派生汇总不替代原始日志。
- [可运行 Windows x64 构建包](r5-003-windows-x64.zip)、[每个条目身份](binary-package.json)。打包前核对编译输入及测试的 manager/kernel 身份，ZIP 每个条目重新读取并核对 SHA256。
- [RA](RA-final.png)、[RB](RB-final.png)、[C](C-final.png) 为 recorder 正例中目标自行绘制的实际最终图像；[selected-evidence.json](selected-evidence.json) 记录原始路径和哈希。人工查看图像并结合目标绘制序号及像素检查，控制器没有拼画面。
- 构建和完整测试入口见 [README](../../README.md)。测试前不并行重建相关目标，采用新的短结果目录；已有磁盘版本和历史失败均不得覆盖。

## 结论范围

只验证**同 Windows 会话、正常退出、同构建/相同映像基址与受管地址、显式协作三/四帧、单 GUI 线程、已知 top/EDIT/BUTTON、单次 ASCII 全选替换 Undo、指定已关闭同步普通文件**。不声明管理器崩溃、宿主重启、断电、稳定跨版本协议、任意第三方应用/调用栈、持有按键或拖拽现场恢复、通用文件系统沙箱。人工宿主交互未执行。

在此范围内，Snapshot 已不依赖原管理器生命周期；跨管理器持久恢复通过，整体项目与通用 Windows 恢复仍未完成。
