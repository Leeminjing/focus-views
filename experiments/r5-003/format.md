# 实验磁盘格式 v1

仅 Windows x64 little-endian、相同本机 ABI/构建与声明资源 profile。不是长期兼容协议，也不是不可信输入的认证格式。

## 包外壳

| 字节偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 8 | `FVSNAP1\0` magic |
| 8 | 4 | 格式版本，当前 1 |
| 12 | 4 | `IMAGE_FILE_MACHINE_AMD64` |
| 16 | 4 | 种类，Snapshot=1、Session=2 |
| 20 | 4 | 保留，必须为 0 |
| 24 | 8 | Payload 字节数 N |
| 32 | N | Payload |
| 32+N | 32 | 前 32+N 字节的 SHA256 |

整包上限 128 MiB，长度必须完全匹配，不接受尾随字节。所有变长字段先核对上限及剩余字节，再分配/复制。

## Snapshot payload 的顺序

`block(tag,T)` 为 32 位 tag、64 位 `sizeof(T)` 和原生固定记录字节；读取必须匹配当前精确大小。这里的固定记录只包含数字和固定数组，不含容器或管理器对象。

1. `block(1,Environment)`：会话号、ntdll/kernel32 文件 SHA256。
2. `block(2,Metadata)`、`block(3,ExecutionLayout)`、`block(4,CONTEXT)`。
3. Memory blob、Stack blob，各为 64 位长度及原始字节。
4. `block(5,executable SHA256)`；64 位 cookie、complement。
5. 32 位返回向量元素数，随后逐个 64 位返回地址。
6. `block(6,Resources)`，包含原生窗口关系与受限 Undo 资源记录。
7. FileVersion：32 位 present/PID，64 位 Action/object，32 位 volume；随后 view/SHA256/name 字符串及 file-content blob。
8. 64 位目标诊断映像 base，随后 image blob。

字符串为 32 位字节长度及非 NUL 的内容；当前记录的来源/文件名为受限 ASCII。最大内存 16 MiB、栈 32 MiB、映像 64 MiB、关闭工作文件 4 KiB。原生 wchar_t 固定记录属于本机构建 ABI。

载入后继续验证 Runtime ABI、实际三/四帧声明、地址/引用及局部量范围、CPU 安全点、PE 头、控件/Undo 状态、文件版本、目标文件身份和会话身份。诊断映像不执行；源 SSP、PID、HWND 和文件对象编号不直接作为新进程资源使用。

## Session payload

四个实例身份（manager/source/A/B）分别编码 32 位 PID、64 位创建时间，**没有写入 C++ 结构体 padding**。然后是目标 EXE SHA256、客户端标识、S0/SA/SB 各自包哈希和运行时指纹，以及两个固定 Artifact 的标识、来源 View/Action、数据/清单哈希、版本、源 PID 和长度。

Session 在三个 Snapshot、固定成果及源实例退出都确认后最后发布。新管理器先验证 Session、旧实例退出、全部必要 Snapshot 和 Artifact，再创建自己的原生进程/窗口/输入环境。

## 发布和生命周期

新包写入同目录唯一 staging，flush/关闭后读回并校验，再以不替换既有版本的 rename 发布。被中断的写入不发布最终名称；已有版本不得覆盖。原生文件错误、长度/哈希/版本错误明确报告，不重试 Snapshot 或冷启动业务。

本轮只验证管理器正常退出后的读取，不保证断电原子性、异常崩溃恢复、宿主重启或长期跨构建兼容。固定 EXE 哈希也不保证未来加载基址相同；布局变化仍拒绝。
