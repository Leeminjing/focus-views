# R5-003：正常退出后的跨管理器 Snapshot 恢复

实验目标：同一 Windows 会话中，Manager 1 保存 S0/SA/SB 和固定成果，确认所属应用退出并正常退出；不同 PID 的 Manager 2 只读取磁盘存储，恢复 RA/RB、从 S0 恢复 C，再实际 Undo、输入、GDI 绘制和私有文件处理。

本轮沿用 R5-002 的同一份 `kernel.lib` 和 ABI v1 三帧 `fixture.exe` / ABI v2 四帧 `second.exe`，不重新研究调用深度。新增 `snapshot-store.lib` 只负责磁盘编码、校验和发布；业务 oracle 仍在两个显式实验客户端。两个模型从 R5-002 原客户端小幅提取，Observation 只读重建，不写入应用。helper/输入 DLL 因诊断传输耗尽问题独立重建，不宣称逐字节复用。

## 复现

```powershell
./experiments/r5-003/build.ps1 -MsvcRoot <MSVC> -SdkRoot <SDK> -SdkVersion <version>
./experiments/r5-003/test.ps1 -ResultsDir <新的结果目录>
```

构建需要已经构建的 R5-002 `out`，保留其可执行文件和恢复内核逐字节身份。`test.ps1` 默认两款应用各执行两次完整双管理器链路、磁盘拒绝及独立分支资源重建失败，并执行 R5-002 及其完整嵌套旧回归。开发时可 `-Repeats 1 -SkipRegression`，这种结果不能声明旧回归通过。

单独操作：

```powershell
./experiments/r5-003/out/manager.exe --client <fixture-client.dll> --fixture <fixture.exe> --mode save --store <新的存储目录> --output <新的 Manager1 输出目录>
# 等上一个管理器进程真正退出之后
./experiments/r5-003/out/manager.exe --client <fixture-client.dll> --fixture <fixture.exe> --mode restore --store <同一存储目录> --output <新的 Manager2 输出目录>
```

## 磁盘边界

实验格式版本 1，little-endian x64、同构建和原生记录尺寸。每个包有 8 字节 magic、版本/架构/种类/保留字段、64 位 payload 长度和 SHA256；长度上限 128 MiB。Metadata、ExecutionLayout、CONTEXT、Resources 有显式 tag/大小；memory、stack、returns、目标诊断 image 和 FileVersion 的内容/字符串分别按元素编码。没有保存 `std::vector`/`std::string` 对象内存或 C++ 指针，不反序列化管理器的进程/线程/窗口/事件句柄。

同一 EXE 的 SHA256 相同不保证之后所有新进程都得到相同的 ASLR 加载基址。第三轮开发记录捕获了基址变化后的明确拒绝；本轮仍要求原映像/受管地址布局一致，没有关闭 ASLR、强制重定位或猜测代码指针。需要进入执行阶段的独立故障场景各有自己的 Manager1 检查点设置，不以重试失败恢复操作获得通过。

资源记录中的旧 PID/HWND/文件对象编号仅是来源和重关联前像；恢复继续使用既有运行时创建的新进程、Desktop、CET CALL 链、原生窗口和独立关闭文件。新管理器由 raw bytes 重新计算只读客户端观测与 oracle，不能以观测 DTO 改写业务状态。

私有、未发布的 staging 文件以 CREATE_NEW 写入、FlushFileBuffers、关闭、完整读回和校验后同卷 rename；不替换已发布版本。session 包最后发布，列出三个 Snapshot 包哈希和运行时指纹、固定 Artifact 来源与哈希、旧管理器/应用 PID 加创建时间。加载器先核对全部必要材料和旧实例退出，再允许创建新 View。拒绝路径保留 API/错误和失败结论，不回退冷启动。

哈希用于检测完整性，不提供恶意存储的身份认证。仅限可信本地实验目录；不声明跨系统/SDK/构建的稳定格式。Windows 会话号以及 ntdll/kernel32 文件身份要一致。目标映像字节身份、有限执行结构、地址和局部量边界及已有资源范围继续核对。

发布方式使用原生 [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers) 和 [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)，本轮只验证正常退出；这些调用不被解释为突然断电、崩溃或宿主重启恢复保证。

## 必须核对的证据

- Manager 1、source/A/B 正常清理与退出；Manager 2 不同 PID，创建时间更晚。PID 复用按创建时间区分，不能拿 PID 数值独立代表实例。
- 保存后的 Manager1 输出目录被移走，旧路径不存在；Manager2 没有业务初始化/前缀事件。
- 三个磁盘 Snapshot 解码后的全部原始数据、CPU、栈、返回向量、资源/Undo 和文件版本与原记录一致。
- 新进程自己的 CET 返回向量、真实 RET 结果、原生 Undo 和正常输入；目标绘制计数、日志与 PNG 像素共同核对。
- Artifact 固定内容、来源、Action、文件对象及 C 的真实 ReadFile/WriteFile 处理；整个存储目录前后字节不变。
- 缺包、篡改、截断、未知磁盘版本/Runtime ABI、错误目标映像、坏 Artifact、真实窗口关系重建失败；不发布失败分支，不损坏既存 RA/C 或固定包。

## 查询诊断传输的独立限制

开发第二轮真实恢复完成后，RA 的旧输入诊断记录达到 8,192 条上限，`cursor.query_log_capacity / 111` 使通道失效；原始 false summary 保留。独立单分支七秒空闲重现同一限制，业务初始化、Snapshot 解码和 CET 返回不是此故障的原因。

R5-003 仅在编译 helper/输入 DLL 时启用 `FOCUS_VIEWS_QUERY_STREAM`：单 GUI 生产者和常驻桥使用另一个具身份核对的 8,192 槽映射，查询序号单调，消费者写出并 flush 日志后才 ACK；空闲和等待输入回执时也消费。旧 Channel ABI、键鼠状态和 IAT 语义不变，旧默认构建仍保留有限总配额路径。不丢弃/抽样日志；未确认满环、序号耗尽或日志写失败明确拒绝。新诊断流不进入 Snapshot，恢复后重新建立。

native codec 同时验证 FIFO 环绕和未确认满环拒绝；GUI 的 `query-capacity` 对照需真实空闲越过原上限、日志索引连续无损，并继续完成恢复后的控件操作和清理。单生产者/消费者协议不是通用多线程日志系统。

首次正式本体通过后，R5-002 recorder 的两个旧反例也暴露了相同的诊断配额清理失败。完整回归前，`prepare-regression-input.ps1` 显式给 **R5-002** 配置已构建的同一 helper/DLL 诊断对，更新依赖来源清单并保存原版本。管理器、应用和恢复库逐字节不变；不修改旧测试断言，R5-001b 及更早嵌套依赖保持原件。两份输入二进制与清单先全部校验和备份，任一步失败均回滚验证；真实 DLL 文件锁的事务拒绝见 `test-input-profile.ps1`。本体与该明确依赖配置下的全量回归分别报告，首次失败结果不改写。

## 当前验证状态

开发最小链路已编译并分别运行 v1/v2；正式矩阵、故障和全量回归尚待执行。最终结果以新的 `results/*/report.md`、`summary.json` 和原始日志为准。

仅正常退出、同 Windows 会话、固定地址/同构建、单 GUI 线程、已验证三/四帧及既有 top/EDIT/BUTTON、单次 ASCII Undo 和关闭同步普通文件。不支持崩溃/断电/宿主重启、任意应用/调用深度、持有键鼠现场、长期跨版本协议。历史 CreateProcessW/5 门禁仍未关闭，人工宿主交互未执行。
