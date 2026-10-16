# Core 内核美化试穿 r3

适用输入为和平精英 1.38.12 / build 15915，UUID
`34B785B2-0DAB-3992-985D-359E6BF45585`，以及当前 iOS 17.0 / TrollStore
环境下的 v19 Core。此版只验证一件外套的内存替换和画面刷新；整包转换尚未完成，
实际画面效果仍待设备反馈。

## 安装和试穿

1. 使用 TrollStore 覆盖安装 `Core-17.0-21A327-cosmetics-trial-r3.ipa`。
2. 游戏穿回已采集的初始白 T 恤，进入训练场。
3. 返回 Core，完成内核初始化并开启原有游戏采集。
4. 点“内核美化 → 试穿至尊金龙外套”，立即切回训练场观察。
5. 试穿约 15 秒后尝试恢复原样，也可以在 Core 点“恢复原样”。
6. 返回 Core 导出报告，反馈是否变成金龙外套、是否恢复白 T 恤，以及报告中的
   `apply`、`observations` 和 `restore`。报告仍在 `Documents/Core-cosmetics-probe.json`。

尚未就绪时会等待 Core 和当前角色。系统结束后台运行时会提前尝试恢复。
恢复前检查当前绑定、角色、数组位置、物品实例和服装 ID；若游戏已经更换这些数据，
会停止写入，报告记录具体原因。此时可重新进入游戏以清除本次进程内的数据修改。

## 已取得的证据

r2 设备报告确认原 Core 写接口返回状态 1、完成四字节、回读稳定；
采集到 30 个同步槽位和空客户端数组，外观回调 RVA 为 `0x43d2eb0`。
前七次未就绪，最后一次采集成功，见 `analysis/cosmetics/device-evidence-r2.json`。

r3 将槽位 5 的 `403251`（初始白 T 恤）改为 `403188`（至尊金龙外套），
再通过角色组件的待刷新标记请求游戏自行刷新。目标名称由固定版本的文本目录和
当前包内 Item 表的名称共同核对；该证据不能代替设备画面确认。
GitHub 静态分析确认该游戏版本默认开启延迟刷新，并在处理时清除待刷新标记。
写入仍经过原后端的能力、绑定、预期值和回读检查。

## 云端构建

代码提交：`b85023f7d15ba8860d68cb69516929c9e81091da`。

- [编译、回归测试及最终 IPA 核验](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38905921102)：通过。
- [固定输入及刷新路径分析](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38905921057)：通过。
- [下载构建产物](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38905921102/artifacts/12047712899)：包含试穿 IPA、准备工具和说明。

IPA SHA-256：`a3861e442406780e5ae85bfd3239b75fa4388d8616af84422d142830185b7767`。
所有编译、自动化测试、二进制分析及签名核验均在 GitHub Actions 执行；服务器只编辑、
推送源码及下载产物。
