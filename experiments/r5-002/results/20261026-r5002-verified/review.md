# 实现复核

## Standards

审查基线 aa57ab7，依据 AGENTS.md 及研发基线，独立只读审查。发现并修正：unwind 日志应分别记录实际 RIP 与声明入口；每应用 identity/suite 增加同一 kernel.lib 哈希核对。GUI 排空复核又发现 PeekMessage 的 WM_QUIT 例外，现 PM_REMOVE 后再次验证实际消息与 HWND，并在真实交错负例中拒绝。未发现保护关闭、v1 字节布局破坏或跨 View 共享可变对象。新实验保留基线源码，未借此扩大重构。

## Spec

独立只读审查未发现剩余阻塞项：真实四层计算/RET、第四局部量/R10 反例、未重编译 v1 EXE、顺序 unwind/调用者边界、CET 与失败隔离均有实现和实际证据。后续复核确认按 raw_capture 要求一一对应 guard 能区分采集前明确拒绝，不削弱实际检查点要求。最终结论以完整矩阵/回归和报告列明范围为准。
