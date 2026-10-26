# 原始证据归档与下载

22 份无损原始归档，包括开发红项、两次正式失败、最终正式运行及诊断对照。每份归档实际重新解压，文件数量、长度和 SHA256 与源文件逐项核对；每个原文件的路径/长度/哈希均在相邻 manifest 中。汇总身份见 [archives.json](archives.json)，历史结论见 [history.md](history.md)。

大归档按不超过 32 MiB 的分卷运输。将同一个包的全部分卷、`.parts.json` 和原始 `.manifest.json` 放在同一目录，执行：

```powershell
./experiments/r5-003/join-archive.ps1 -Manifest <包名>.tar.xz.parts.json -Destination D:/new-evidence.tar.xz
tar -xJf D:/new-evidence.tar.xz -C <已有的解压目录>
```

三份分卷包均实际拼接并核对完整压缩包 SHA256；不删减或归一化原始输入日志。未分卷的包直接下载解压。原生实验没有因归档而重跑或改变结果。后处理初版 PowerShell 大小汇总错误的 [原始输出](archive-tool-initial-error.log) 单独保留；归档器改为显式 Int64 累加，并对完整已有包重新提取/核对后才生成清单。这个工具错误不是 Snapshot 或执行续接失败。

最终 [R5-003 报告](r5003/report.md)、[原始测试 summary](r5003/summary.json) 与 [可运行构建包](r5003/r5-003-windows-x64.zip) 分别提供结论、原始判定和实际二进制。历史 CreateProcessW/5 门禁、人工宿主交互未执行及正常退出范围保持不变。

| 下载 | 原文件数 | 压缩 MiB | 清单 |
| --- | ---: | ---: | --- |
| [20261026-r5003-path-refusal-raw.tar.xz](20261026-r5003-path-refusal-raw.tar.xz) | 1 | 0 | [逐文件 SHA256](20261026-r5003-path-refusal-raw.tar.xz.manifest.json) |
| [20261026-r5003-raw.tar.xz.chunk-01](20261026-r5003-raw.tar.xz.chunk-01)、[20261026-r5003-raw.tar.xz.chunk-02](20261026-r5003-raw.tar.xz.chunk-02)、[20261026-r5003-raw.tar.xz.chunk-03](20261026-r5003-raw.tar.xz.chunk-03)、[20261026-r5003-raw.tar.xz.chunk-04](20261026-r5003-raw.tar.xz.chunk-04) / [拼接清单](20261026-r5003-raw.tar.xz.parts.json) | 35841 | 122.66 | [逐文件 SHA256](20261026-r5003-raw.tar.xz.manifest.json) |
| [20261026-r5003-verified-raw.tar.xz.chunk-01](20261026-r5003-verified-raw.tar.xz.chunk-01)、[20261026-r5003-verified-raw.tar.xz.chunk-02](20261026-r5003-verified-raw.tar.xz.chunk-02)、[20261026-r5003-verified-raw.tar.xz.chunk-03](20261026-r5003-verified-raw.tar.xz.chunk-03)、[20261026-r5003-verified-raw.tar.xz.chunk-04](20261026-r5003-verified-raw.tar.xz.chunk-04) / [拼接清单](20261026-r5003-verified-raw.tar.xz.parts.json) | 35852 | 101.54 | [逐文件 SHA256](20261026-r5003-verified-raw.tar.xz.manifest.json) |
| [cet-short-path-raw.tar.xz](cet-short-path-raw.tar.xz) | 202 | 0.1 | [逐文件 SHA256](cet-short-path-raw.tar.xz.manifest.json) |
| [default-query-quota-red-raw.tar.xz](default-query-quota-red-raw.tar.xz) | 173 | 0.82 | [逐文件 SHA256](default-query-quota-red-raw.tar.xz.manifest.json) |
| [dev-first-raw.tar.xz](dev-first-raw.tar.xz) | 499 | 2.63 | [逐文件 SHA256](dev-first-raw.tar.xz.manifest.json) |
| [dev-matrix-1-raw.tar.xz](dev-matrix-1-raw.tar.xz) | 1084 | 5.72 | [逐文件 SHA256](dev-matrix-1-raw.tar.xz.manifest.json) |
| [dev-matrix-2-raw.tar.xz](dev-matrix-2-raw.tar.xz) | 1114 | 4.83 | [逐文件 SHA256](dev-matrix-2-raw.tar.xz.manifest.json) |
| [dev-matrix-3-raw.tar.xz](dev-matrix-3-raw.tar.xz) | 1236 | 6.19 | [逐文件 SHA256](dev-matrix-3-raw.tar.xz.manifest.json) |
| [dev-matrix-4-raw.tar.xz](dev-matrix-4-raw.tar.xz) | 1854 | 9.63 | [逐文件 SHA256](dev-matrix-4-raw.tar.xz.manifest.json) |
| [input-profile-transaction-final-raw.tar.xz](input-profile-transaction-final-raw.tar.xz) | 9 | 0.37 | [逐文件 SHA256](input-profile-transaction-final-raw.tar.xz.manifest.json) |
| [input-profile-transaction-raw.tar.xz](input-profile-transaction-raw.tar.xz) | 9 | 0.37 | [逐文件 SHA256](input-profile-transaction-raw.tar.xz.manifest.json) |
| [input-profile-transaction-wide-final-raw.tar.xz](input-profile-transaction-wide-final-raw.tar.xz) | 9 | 0.37 | [逐文件 SHA256](input-profile-transaction-wide-final-raw.tar.xz.manifest.json) |
| [input-profile-transaction-wide-raw.tar.xz](input-profile-transaction-wide-raw.tar.xz) | 9 | 0.37 | [逐文件 SHA256](input-profile-transaction-wide-raw.tar.xz.manifest.json) |
| [legacy-recorder-profile-positive-raw.tar.xz](legacy-recorder-profile-positive-raw.tar.xz) | 214 | 0.6 | [逐文件 SHA256](legacy-recorder-profile-positive-raw.tar.xz.manifest.json) |
| [profile-three-managers-raw.tar.xz](profile-three-managers-raw.tar.xz) | 9 | 0.44 | [逐文件 SHA256](profile-three-managers-raw.tar.xz.manifest.json) |
| [query-quota-red-2-raw.tar.xz](query-quota-red-2-raw.tar.xz) | 173 | 0.79 | [逐文件 SHA256](query-quota-red-2-raw.tar.xz.manifest.json) |
| [query-quota-red-raw.tar.xz](query-quota-red-raw.tar.xz) | 44 | 0.33 | [逐文件 SHA256](query-quota-red-raw.tar.xz.manifest.json) |
| [query-regression-fixed-raw.tar.xz](query-regression-fixed-raw.tar.xz) | 409 | 1.12 | [逐文件 SHA256](query-regression-fixed-raw.tar.xz.manifest.json) |
| [query-stream-green-raw.tar.xz](query-stream-green-raw.tar.xz) | 248 | 0.97 | [逐文件 SHA256](query-stream-green-raw.tar.xz.manifest.json) |
| [r5003-raw.tar.xz.chunk-01](r5003-raw.tar.xz.chunk-01)、[r5003-raw.tar.xz.chunk-02](r5003-raw.tar.xz.chunk-02)、[r5003-raw.tar.xz.chunk-03](r5003-raw.tar.xz.chunk-03)、[r5003-raw.tar.xz.chunk-04](r5003-raw.tar.xz.chunk-04) / [拼接清单](r5003-raw.tar.xz.parts.json) | 35862 | 96.76 | [逐文件 SHA256](r5003-raw.tar.xz.manifest.json) |
| [regression-profile-dev-raw.tar.xz](regression-profile-dev-raw.tar.xz) | 4 | 0.36 | [逐文件 SHA256](regression-profile-dev-raw.tar.xz.manifest.json) |

