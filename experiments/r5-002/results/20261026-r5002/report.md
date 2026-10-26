# 首轮正式运行：失败保留

本目录原始 summary 保持 false。旧 fixture 20 次/3,934 项与旧 recorder 20 次/4,030 项通过；四帧 recorder 共执行 30 次，其中 29 次完成证据核对，omit-register-2 在源 S0 前拒绝，尚未进入寄存器遗漏验证。该拒绝不计为预期负例通过。

控制器主错误是 `validate / 13 / expected debug halt business, got rejected`；目标 GUI 安全点报告 `ERROR_BUSY (170)`。已完成资源采集和真实图像，尚未保存 S0。原日志未记录具体 PeekMessage 返回，所以无法确认历史消息类型。后续确定性 WM_PAINT 对照只证明同类竞态可复现，不认定它是本次的确切触发消息。

此轮 R5-001b、R5-001a 和 R4-003 的完整嵌套回归另行通过；不能据此把本轮总结果改成通过。汇总见各 `*-summary.json`；首个失败的完整原始材料在 `first-failure.tar.xz`，当时的二进制、源文件和构建身份在 `first-build.zip`。其余此轮原始材料保留于本地同名子目录。最终验收使用后续独立目录，不覆盖本目录。
