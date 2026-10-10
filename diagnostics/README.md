# 独立 Core 诊断工具

用于 Core v2 立即闪退且没有可见日志的设备。用 TrollStore 安装 `Core-Diagnostics-TrollStore.ipa`，与 Core 分开安装。

1. 打开 **Core 诊断**，点 **记录启动**，记录系统返回的启动请求结果。
2. Core 退出后回到工具，点 **收集诊断**。
3. 点 **分享报告**，发送 `Core-diagnostic-report.json`；也可从文件 App 的 Core 诊断文件夹取出。

工具只读取 Core 的注册状态、安装版本、可执行文件结构与代码节 SHA-256、两个指定日志和最多 5 份 Core 崩溃报告，检查当前启动内核文件的读取条件。它不加载 Core 的三份动态库，不修改 Core，不执行内核利用，不联网。需要用户点击按钮才发出 Core 启动请求；请求被接受不等于 Core 已启动成功。

如果系统目录没有报告或不允许读取，错误信息也会保存在报告中。独立应用自身尚未在用户设备上测试，诊断权限不保证覆盖所有系统限制。

参考：[TrollStore 的数据权限及应用注册说明](https://github.com/opa334/TrollStore)、[FrontBoard 运行时头文件](https://github.com/nst/iOS-Runtime-Headers/blob/master/PrivateFrameworks/FrontBoardServices.framework/FBSSystemService.h)。
