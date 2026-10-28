# 正常关闭整例判定的确定性修复

2026-10-28，基于 9f8a90f。修复 test-no-cleanup.ps1 的判定，不改通用监督器、清理候选、PID 身份或原生恢复实现。

先将实际脚本的整例判断提取到 assessment.ps1，保留原来忽略退出码/首错的行为，运行 test-assessment.ps1。人工证据复现了三个漏判：[red.json](red.json) 中局部关闭成功但 manager=2、controller 非零、firstApi/firstError 非空均错误返回成功。它们是确定性证据夹具，不是声称本机发生了三次新原生故障。

修正同一个、实际被 test-no-cleanup.ps1 调用的函数后，六项检查通过，见 [green.json](green.json)：正常成功、管理器后续失败、监督器退出非零、首错保留、重复/缺少完成记录。要求 controllerExit=0、managerExit=0、唯一 trim_complete、nativeCleanupPassed=true、firstApi 为空且 firstError=0、监督协议及物理清理均通过，才有 operationPassed=true。

evidenceComplete 表示所需证据字段完整，不将管理器失败和物理清理成功混成操作通过。两个层次分别保存在 summary；失败仍先写 summary 再结束。缺失证据安全拒绝，原生首错和完整 trim_complete 保留。

命令：test-assessment.ps1 -ResultsDir <新目录>；红、绿分别运行在 C:/fv-c/rd/judge-red 和 judge-green，均未启动原生进程。原上一轮两组证据只读套用新判据仍通过，见 [previous-pair-readonly.json](previous-pair-readonly.json)；不是重新运行 Windows 对照，也不覆盖旧报告/summary。

历史清理、/126、完整 R5-004 和创建可靠性门禁保持未关闭。后续原生诊断另行记录，不以此脚本修复证明历史自然故障消失。
