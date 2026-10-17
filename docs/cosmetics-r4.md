# Core 原生菜单美化 r4

r3 已由用户确认：初始白 T 恤能变为至尊金龙外套，随后恢复。
r4 将同一试穿逻辑放入 Core 原悬浮菜单，“美化”栏目就在“位置”下面。
主界面独立按钮已移除。仍只提供这一件外套的 15 秒试穿；大厅及整包转换未完成。

## 使用

1. 巨魔覆盖安装 `Core-17.0-21A327-cosmetics-menu-r4.ipa`。
2. 穿初始白 T 恤进入训练场，完成 Core 内核初始化及原有游戏采集。
3. 在游戏中打开 Core 悬浮菜单，点“位置”下面的“美化”。
4. 打开“试穿至尊金龙外套（15 秒自动恢复）”；关闭开关可提前恢复。

页面标题显示等待、试穿、已恢复或失败状态。报告自动保存至
`Documents/Core-cosmetics-probe.json`，可在“文件 → 我的 iPhone → Core”中读取。
取不到当前角色时只等待，未执行换装。恢复保留原有的绑定、角色、实例和预期 ID 检查。

大厅相关 getter 和属性名称已在当前游戏输入中定位，但其对象归属链尚未确认，
本版没有启用大厅写入。新菜单的实际显示和点击仍需要手机确认。

## 云端核验

代码提交：`425248e4bba07c685c6ed0a45fef9e4485646895`。

- [最终构建](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38919094531)：42 项 Python 回归、原生外套写入/恢复与页面测试、打包及签名核验通过。
- [下载产物](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38919094531/artifacts/12052734796)：Core、美化说明及原环境准备工具。
- [原生菜单接口复核](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38918884006)：通过。

IPA SHA-256：`c992d3510dd6b62282291af0c0024721af53117962ef5c5cdd4dcf3f907bba46`。

菜单回归覆盖原生栏目点击、选择状态、页面分派、失效旧栏目回退、解析失败返回、
回调前后的原栈帧及 Apple 汇编器确认的 ARM64e 调用指令。
最终包检查默认 v19 与可选美化包各自的精确允许修改范围。
编译、测试和二进制分析全部在 GitHub Actions 执行，服务器只编辑、推送和下载产物。
