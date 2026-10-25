# R4-001：固定 Artifact、Action 与 C 的私有文件处理

本实验复用 R3-005c-001 的同一 S0、统一调试事件泵、独立 Desktop、CET 协作续接和既有顶层/EDIT/BUTTON。新增的是已完成文件的成果协议，不是 GUI 应用的通用文件系统隔离，也不把 Artifact 当作 Snapshot。

## 构建和运行

~~~powershell
New-Item -ItemType Directory -Path '.\experiments\r4-001\out' -Force | Out-Null
.\experiments\r4-001\build.ps1 -MsvcRoot '<MSVC 根目录>' -SdkRoot '<Windows Kits\10>' -SdkVersion '<版本>' 2>&1 | Tee-Object '.\experiments\r4-001\out\build.log'
.\experiments\r4-001\out\r305a.exe --output '<全新结果目录>' --scenario positive
.\experiments\r4-001\test.ps1 -ResultsDir '<另一个全新结果目录>'
~~~

保留实验内部旧 runner 文件名 r305a.exe；没有新增公共 SDK。默认 positive、export-interrupt、hash-mismatch、import-failure 各两轮，随后执行未修改的 R3-005c-001 完整回归（其脚本会执行 b001 和 CET-002）。旧实验需已构建。-Repeats 1 -SkipRegression 只供诊断，未执行的回归明确标记。所有场景使用新目录，不覆盖历史证据。

## 真正的应用文件操作

application.cpp 是测试应用的普通文档功能：控制器仅提交操作种类、Action 编号及文件路径，随后通过原有受管鼠标点击真实 BUTTON。原生 BN_CLICKED 执行 CreateFileW、WriteFile、FlushFileBuffers、CloseHandle；文档字节由应用从自己的原生 EDIT、恢复后的 seed/receipt 生成。控制器不提交期望文档正文。

C 完成原始 S0 的真正 CALL/RET 续接后，应用通过普通 ReadFile 读取指定私有副本，追加自己的修改并生成处理结果；结果同时使用两份实际读取内容和 C 自己的恢复状态。GUI 绘制应用自己的文件读取/处理计数。业务文件操作不是用于恢复 CPU/内存的文档导入函数。

应用文档、Runtime 原始查询和输入日志分别放在 A/B/C 子目录、workspace/<View> 和各自 input-*.jsonl。输入重新安装时不覆盖旧日志。

## Runtime 成果协议

artifacts.hpp 只接受已完成且关闭文档句柄的应用回执。以原生只读句柄稳定读取文件，复制到新的临时版本，核对长度/SHA256，再写来源 View、PID、版本及产生它的 Action。完整目录在同卷重命名后才进入发布记录。运行期间持有不共享写入/删除的只读句柄，并保留只读属性；后续所有导入仍验证内容和来源。

A-v1/B-v1 与 A 后续的工作文件及 A-v2 是不同文件对象。C 的两个副本先在私有临时目录中全部复制、验证，再发布 imports 目录；C 的 WriteFile 只修改这些普通私有副本。没有硬链接、共享可变打开实例或全局路径 Hook。

该不可变约束限于本实验管理协议和生命周期，不是对同用户任意恶意代码、管理员或宿主文件系统的安全沙箱；管理进程崩溃、断电和跨版本恢复未验证。

## Action 与反例

actions.jsonl 为每个 View 分配单调序号，分别记录 submitted、accepted、delivered、effect_confirmed 或 failed，并链接 GUI 原生查询、输入日志或应用真实文件回执。delivered 是控制器确认投递的观测点，可能晚于目标实际处理的时刻；实际发生顺序另见目标/桥日志。输入接受或投递不等于文档操作已完成。该账本不提供通用确定性重放。

- export-interrupt：完成部分临时文件后，在声明的取消点拒绝并移除候选；既有版本不受影响。这不是任意崩溃恢复。
- hash-mismatch：只破坏导入临时副本的一个字节，实际哈希校验拒绝并回滚整批，不修改原 Artifact。
- import-failure：第二份临时文件使用只读原生句柄，真实 WriteFile 返回拒绝，第一份已复制文件也回滚。
- 失败操作保留 failed，随后另起独立 Action 验证正常操作仍可完成，不静默重试或把失败改记成功。

SA/SB 尝试先正常关闭输入、冻结受管处理，再调用原有资源采集适配器；实际 EM_CANUNDO=1 时仍拒绝，不清空历史、不发布快照。之后重新安装输入通道。私有 runtime.cpp 仅修正已完全恢复的 IAT 事务对象在模块仍加载时的退休，保留失败时的自持引用；没有增加输入 API。input.cpp 仅增加目标归属诊断，旧 R3-004 源码不改。

开发时首次尝试静态 CRT 数字格式化/流路径曾访问异常。当前应用使用不依赖 locale 的 to_chars 和原生文件日志；这不构成完整 CRT/TLS 恢复。继续保持 CET-002 的已知 ABI、/Od /Ob0、固定布局及既有控件范围。

正式证据、原始 Artifact 和逐项结果见 [报告](results/20261025-r4001-resident/report.md)。只声明本轮受限成果流；可恢复 SA/SB、完整 R4、任意 GUI 应用兼容均未实现。

控制和取图使用每 View 一个常驻 courier，按序处理本桌面的 PostMessage/原生像素采集请求；控制器仍在同一调试线程分发所有目标事件。各命令有期限，工作进程退出或超时明确失败；没有自动重试单次操作。结束时确认 courier 退出，再终止所属 GUI。逐操作创建临时助手时发生过 CreateProcessW 拒绝，原始失败矩阵保留，不用新版本的成功记录覆盖。