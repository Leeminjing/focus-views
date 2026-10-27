# 原始证据与运行包

每个归档均完整逐文件流式解包并重新计算 SHA256/长度；没有只核对压缩文件哈希。原始目录留在本地，公开仓库中的较大归档以不超过 32 MiB 的运输分片保存。归档包含失败结果，不能当作实验通过证书。

| 归档 | 原始文件数 | 原始字节 | 传输 |
| --- | ---: | ---: | --- |
| [development-history.tar.xz](../development-history.tar.xz.parts.json) | 11569 | 1866453080 | 32MiB-parts |
| [initial-v.tar.xz](../initial-v.tar.xz.parts.json) | 43387 | 4938128092 | 32MiB-parts |
| [latest-g.tar.xz](../latest-g.tar.xz) | 5919 | 976386465 | single-archive |
| [native-f.tar.xz](../native-f.tar.xz) | 34 | 77706764 | single-archive |
| [native-g.tar.xz](../native-g.tar.xz) | 36 | 77728547 | single-archive |
| [native-v.tar.xz](../native-v.tar.xz) | 34 | 77653373 | single-archive |
| [second-f.tar.xz](../second-f.tar.xz.parts.json) | 43393 | 4775237339 | 32MiB-parts |
| [tool-logs.tar.xz](../tool-logs.tar.xz) | 39 | 4721363 | single-archive |

- `initial-v`：首轮正式矩阵及完整嵌套回归；summary 为 false，保留两个 Desktop 红项。
- `second-f`：窗口清理修正后的正式矩阵及完整旧回归；summary 为 false，保留一次映像采样错误。
- `latest-g`：监督器修正后的本体矩阵；summary 为 false，保留映像布局失败和后续门禁拒绝。
- `development-history`：dev-01 至 dev-13 中实际存在的开发记录。部分中间源码没有单独冻结，不能宣称全部开发对照均有精确源码复现；正式 v/f/g 各有对应原生包/源码输入。
- `native-v/f/g`：实际二进制、编译/依赖记录、源码输入与对应脚本。g 另外包含发布前只读错误守卫的当前脚本，不将它当作 g 原来执行过的脚本。
- `tool-logs`：实际构建、测试、失败及归档工具输出。

## 拼接与检查

在短目录中保存输出，以满足历史证据文件的 Windows 路径限制：

```powershell
New-Item -ItemType Directory -Path D:/fv-evidence -Force
./experiments/r5-003/join-archive.ps1 -Manifest ./experiments/r5-004/results/second-f.tar.xz.parts.json -Destination D:/fv-evidence/second-f.tar.xz
tar.exe -xJf D:/fv-evidence/second-f.tar.xz -C D:/fv-evidence
```

拼接器逐片校验长度/SHA256，再核对整体 SHA256，不覆盖既有输出。未分片包可直接解压。逐文件原始清单位于每个 `.manifest.json`，路径对应 tar 内成员。`archives.json` 固定全部包的摘要。

当前归档器源码为 [pack_evidence.py](pack_evidence.py)，初始 v 的精确归档器原字节另存 [initial-pack-evidence.py](initial-pack-evidence.py)，两者 SHA256 分别与实际清单一致。Python 仅是交付证据的离线压缩/哈希工具，不是原生运行时依赖。

复现实验应先按既有 R5-003/R5-002 构建说明准备依赖，再运行 R5-004 build.ps1/test.ps1。预编译 native-g 包提供当前实验自己的 manager、controller、客户端、目标 EXE、helper、DLL 和静态库；仍须使用全新输出/存储路径，检查系统/构建/映像布局条件。归档中的 Snapshot 是原始证据，不保证可在另一机器或不同加载布局直接恢复。
