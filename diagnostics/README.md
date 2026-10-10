# 独立 Core 诊断工具

用于 Core 立即闪退且没有可见日志的设备。用 TrollStore 安装 `Core-Diagnostics-TrollStore-v2.ipa`，覆盖旧诊断工具，与 Core 分开安装。

1. 打开 **Core 诊断**，点 **记录启动**。如果系统拒绝启动，会继续尝试捕获底层错误，请等待“启动错误捕获已完成”。
2. Core 退出后回到工具，点 **收集诊断**。
3. 点 **分享报告**，发送 `Core-diagnostic-report.json`；也可从文件 App 的 Core 诊断文件夹取出。

工具读取 Core 的注册状态、安装版本、可执行文件结构、动态库版本、嵌入签名元数据、代码节 SHA-256、两个指定日志和最多 5 份 Core 崩溃报告，检查当前启动内核文件的读取条件。签名元数据不代表 CoreTrust 已验证通过。需要用户点击按钮才发出 Core 启动请求；请求被接受不等于 Core 已启动成功。

诊断工具不加载 Core 的动态库，不修改 Core 二进制，不执行内核利用，不联网。FrontBoard 启动失败后，会以 Core 自身数据目录作为 HOME / CFFIXED_USER_HOME 创建一个独立的 Core 子进程，将其标准输出及错误保存到诊断工具自己的 Documents/Core-launch-stderr.txt，最多运行 3 秒后终止这个子进程。它不会终止已经运行的 Core。这个独立启动方式与桌面启动不同，报告明确区分两个结果，不能将独立启动中的 UIKit/父进程限制错误直接视为桌面秒退的原因。

如果系统目录没有报告或不允许读取，错误信息也会保存在报告中。独立应用自身尚未在用户设备上测试，诊断权限不保证覆盖所有系统限制。

参考：[TrollStore 的数据权限及应用注册说明](https://github.com/opa334/TrollStore)、[FrontBoard 运行时头文件](https://github.com/nst/iOS-Runtime-Headers/blob/master/PrivateFrameworks/FrontBoardServices.framework/FBSSystemService.h)。
