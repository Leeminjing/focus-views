# 原始包与校验

所有包都已逐项流式解包，比对原文件长度和 SHA256。完整索引见 [archives.json](archives.json)。大包的完整 `.tar.xz` 不重复提交，只提交 32 MiB 分片、分片索引和逐文件清单。

| 包 | 用途 | 下载入口 | 原始文件清单 |
|---|---|---|---|
| final f | 最终 12 场景，4,000 个原始文件 | [完整归档](../raw-final-f.tar.xz) | [SHA256](../raw-final-f.tar.xz.manifest.json) |
| regression c | 本轮实际完整旧回归，37,162 个文件 | [分片索引](../raw-regression-c.tar.xz.parts.json) | [SHA256](../raw-regression-c.tar.xz.manifest.json) |
| development | a/b/d/e 和初期探针、控制、失败记录，13,079 个文件 | [分片索引](../raw-development.tar.xz.parts.json) | [SHA256](../raw-development.tar.xz.manifest.json) |
| native f | 最终 manager、client、kernel、应用、清理 DLL/助手及冻结输入 | [运行包](../native-f.tar.xz) | [SHA256](../native-f.tar.xz.manifest.json) |
| native c | 对应历史 c 证据的运行包，非最终建议版本 | [运行包](../native-c.tar.xz) | [SHA256](../native-c.tar.xz.manifest.json) |
| development logs | 构建、控制台和诊断日志 | [归档](../development-logs.tar.xz) | [SHA256](../development-logs.tar.xz.manifest.json) |

`native-f` 中的应用来自旧构建，源码/CPU ABI 边界没有扩展。包内 `source-inputs` 保留 25 项构建输入的精确工作字节，以避免 Git 文本换行或构建基础 HEAD 与未提交实现之间的混淆。编译器和 Windows SDK 不在包中。`native-c` 对应审查前实验，不应替代最终 f 的安全保护。

## 重组

先下载索引中的全部 `.chunk-*` 到同一目录，使用新的输出文件：

```powershell
./experiments/r5-004a/join-archive.ps1 `
  -Manifest ./raw-regression-c.tar.xz.parts.json `
  -OutputPath ./assembled/raw-regression-c.tar.xz
```

脚本核对每部分长度/SHA256，再写私有 staging；完整哈希匹配后才发布归档，不覆盖已有文件。两个分片包都已实际用此脚本重组并确认哈希。归档解包由系统 tar 完成，例如 `tar -xf ./assembled/raw-regression-c.tar.xz -C ./evidence`，目标目录需先存在。

快照加载仍受同 Windows 会话、目标构建、系统环境和映像基址限制。包内旧 PID/HWND 仅为证据，不是可以继续使用的句柄；下载旧快照不等于可以在另一台机器或重启后恢复。
