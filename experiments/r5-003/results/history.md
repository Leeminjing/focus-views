# R5-003：原始开发与诊断记录索引

最终受限通过结论只来自 [正式第三轮](r5003/report.md)。以下早期红项、对照及各自实际范围保留，不把后续通过覆盖到原失败。

各目录完整原件在同名 `*-raw.tar.xz` 中，配套 `.manifest.json` 列出每个原文件的长度和 SHA256；大包的 `.parts.json` 指向无损分卷，下载/拼接见 [归档索引](archives.md)；归档创建后重新解压，数量/逐文件哈希全部核对。目录中的原始 summary、身份、控制器/输入日志及二进制字节不作换行修复。

| 原始目录 | 实际结果与范围 | 原始归档 |
| --- | --- | --- |
| dev-first | 开发最小 v1/v2 双管理器正例，不是完整回归 | [raw](dev-first-raw.tar.xz) |
| dev-matrix-1 | 失败：原 first-fixture 命令行故障注入不能触发 recorder 的资源错误；没有改成通过 | [raw](dev-matrix-1-raw.tar.xz) |
| dev-matrix-2 | 失败：实际恢复后输入查询记录总配额耗尽，API/111 后清理失败 | [raw](dev-matrix-2-raw.tar.xz) |
| dev-matrix-3 | 失败：同 SHA256 recorder 在较晚进程加载到不同 ASLR 基址；严格拒绝 layout，不归因具体系统组件 | [raw](dev-matrix-3-raw.tar.xz) |
| dev-matrix-4 | 开发矩阵 20 场景/766 检查通过，未跑旧回归；非最终正式证据 | [raw](dev-matrix-4-raw.tar.xz) |
| query-quota-red | 被更早 ASLR 布局拒绝遮蔽，不能算作查询配额复现 | [raw](query-quota-red-raw.tar.xz) |
| query-quota-red-2 | 实际单分支空闲达到 8192/111，确认旧诊断容量问题 | [raw](query-quota-red-2-raw.tar.xz) |
| default-query-quota-red | 宏关闭的原默认路径实际编译、运行并产生同一配额拒绝；非恢复通过 | [raw](default-query-quota-red-raw.tar.xz) |
| query-stream-green | 新诊断流实际超过原配额，保留完整连续日志并继续 GUI/清理 | [raw](query-stream-green-raw.tar.xz) |
| regression-profile-dev | 旧有限 helper/DLL/reuse 原始备份，不是新能力结果 | [raw](regression-profile-dev-raw.tar.xz) |
| query-regression-fixed | 两个原 R5-002 recorder 反例在显式输入依赖配置下的聚焦验证，不代替全量回归 | [raw](query-regression-fixed-raw.tar.xz) |
| input-profile-transaction / -final / -wide / -wide-final | 真实 DLL 只读文件锁：首个 helper 复制已经发生，下一步失败；三份原字节完整回滚、保留主要错误、不发布部分依赖配置 | [初次](input-profile-transaction-raw.tar.xz)、[复核](input-profile-transaction-final-raw.tar.xz)、[扩展](input-profile-transaction-wide-raw.tar.xz)、[最终](input-profile-transaction-wide-final-raw.tar.xz) |
| profile-three-managers | 三套独立 manager 的显式诊断依赖配置及旧版本备份/来源，不是业务恢复证据 | [raw](profile-three-managers-raw.tar.xz) |
| legacy-recorder-profile-positive | 原 R5-001b recorder 单个正例实际完成，不代替最终全回归 | [raw](legacy-recorder-profile-positive-raw.tar.xz) |
| 20261026-r5003 | 第一次正式运行：本体通过、完整回归失败，原 false summary 保留 | [报告](20261026-r5003/report.md)、[raw](20261026-r5003-raw.tar.xz.parts.json) |
| 20261026-r5003-verified | 第二次正式运行：本体与 R5-002 通过，R5-001b 配额和深层 CET 路径失败；目录标签不是成功结论 | [报告](20261026-r5003-verified/report.md)、[raw](20261026-r5003-verified-raw.tar.xz.parts.json) |
| cet-short-path | 同一 CET 原生二进制短路径完整 14 次/285 项通过，仅为路径单变量对照 | [raw](cet-short-path-raw.tar.xz) |
| 20261026-r5003-path-refusal | 过长完整嵌套证据路径在任何应用启动前拒绝，保留 summary；不支持任意长路径 | [raw](20261026-r5003-path-refusal-raw.tar.xz) |
| r5003 | 第三次正式运行：本体及完整嵌套回归均通过，具体依赖与范围见正式报告 | [报告](r5003/report.md)、[raw](r5003-raw.tar.xz.parts.json) |

ASLR 加载基址一致、固定受管布局和有限协作 ABI 仍是恢复前置条件。没有关闭 ASLR/CET/GS/CFG，没有反复重试同一失败 Snapshot；新的完整运行使用新的存储和独立结果目录。历史 CreateProcessW/5 的可靠性门禁独立保留，人工宿主交互未执行。
