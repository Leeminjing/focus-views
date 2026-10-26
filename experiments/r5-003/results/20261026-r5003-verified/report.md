# R5-003 第二次正式运行：本体与 R5-002 通过，旧嵌套回归失败

本目录名是运行前指定的标签，不表示成功。原始 `summary.json` 保持 `allEvidenceChecksPassed=false`、`normalExitCrossManagerRestorePassed=true`。

22 个恢复场景、30 个管理器实例和 1051 项本体检查再次符合预期。R5-002 的全部 70 场景在显式诊断依赖配置下通过，旧应用/管理器/恢复库未改；本轮仍有两类旧嵌套红项，整套第 1052 项失败：

1. R5-001b recorder 的 `positive-1` 与 `artifact-hash-2` 在清理时耗尽旧诊断配额。原始输入日志直接记录 `async.query_log_capacity / 111`；没有把这些用例算作恢复通过。后续输入依赖准备范围扩展到三个独立管理器，仍不改应用、内核或断言。
2. 最深层 CET-002 的部分 `target-active-stack-before-continue.bin` 完整路径达到 262 字符。较短证据文件已写出，但该文件缺失，负向实验未到达所需验证，旧脚本因缺材料继续报错。对照使用同一 CET 二进制和短路径完整执行 14 次、285 项检查通过；没有声称已修复任意长路径。新入口在启动任何应用前明确拒绝过长的旧证据路径。

原始脚本从提交 `6dd9b05aa` 按字节提取到 `original-test.ps1`，SHA256 与 `test-identity.json` 的 `682c5031894f072cb7e462e0550f5832fe3e363d6447fdc962d905577694a9db` 一致。原始日志、summary 和缺失材料状态均保留，后续在更短的独立目录整套重新执行，不能翻改本轮红项。

本轮仍不证明崩溃、宿主重启、任意加载布局、任意应用或稳定跨版本格式。历史 CreateProcessW/5 门禁不关闭。
