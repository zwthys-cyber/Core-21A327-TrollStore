# Core 17.0 / 21A327 巨魔适配补丁

适用于提供的 Core 1.6 IPA（输入 SHA-256 在 `scripts/build.py` 中固定）。

AppleDB 的 21A327 记录没有 `sources`；原程序无法下载该出厂预装版本的 kernelcache，导致环境适配失败并禁用内核按钮。补丁在原缓存验证失败后，读取 IORegistry 当前 `boot-manifest-hash` 对应目录中的 kernelcache，交给原缓存发布器生成元数据及 SHA-256，再由原验证器检查。原准备流程通过后才会变为就绪。

补丁仅针对当前系统及请求身份完全匹配的 21A327，不读取其他启动哈希目录，不替换成 21A329，不跳过原校验。失败时显示提示，日志保存在 Documents/Core-21A327-adaptation.log。主程序增加 no-sandbox 和 AppDataContainers 权限，使用 TrollStore 安装。

## GitHub 构建

在私有仓库的 Actions 中运行 **Build TrollStore IPA**；下载 `Core-17.0-21A327-TrollStore` artifact，解压得到 IPA。原应用没有完整源码，此处编译的是适配 dylib，再注入并重签用户提供的 IPA。

输入 IPA、已有库只在私有仓库保存，不包含用户崩溃报告或本地凭据。请保持仓库私有。

## 安装和核验

1. 用 TrollStore 打开生成的 IPA，覆盖已有 Core。
2. 完全关闭 Core 后重新启动，等待环境适配完成。
3. 环境就绪后，才测试原内核按钮。
4. 若仍失败，在“文件 → 我的 iPhone → Core”导出上述日志。

构建、签名和静态检查不能代替设备验证；此版本尚未在用户的 iPhone16,2 / 21A327 上运行。它修复资源获取路径，不保证原内核利用在该设备上成功。原许可组件的网络策略退出问题仍可能导致闪退。

v2 修复原方法指针发布的并发窗口：先保存并发布原实现，再安装钩子。v1 将保存动作放在方法替换返回之后，原离线库已启动的预取线程可能在该窗口调用尚未保存的空指针。这是确认的代码缺陷，是否对应用户本次秒退仍需新 `.ips` 证明。构造函数改用 C / Objective-C runtime API，不在启动入口创建 Foundation 对象，并在每个启动步骤写入同一个诊断日志。版本号为 1.6 / build 3，产物名包含 `v2`。

macOS 运行时回归测试会在方法替换函数返回前立即调用新钩子，然后并发调用 2000 次，验证原实现已可用。该测试不模拟 iOS 的完整应用启动或 ARM64e 指针认证。

## 参考

- [AppleDB 21A327 元数据](https://api.appledb.dev/ios/iOS%3B21A327.json)
- [TrollStore 权限说明](https://github.com/opa334/TrollStore#unsandboxing)
- [当前 boot-manifest-hash 读取方式](https://github.com/0x7ff/dimentio/blob/main/libdimentio.c)

`python3 -m unittest discover -s tests -v` 验证设备身份与启动目录隔离、未知构建拒绝及注入后代码保留；`scripts/verify.py` 检查最终 IPA 的全部原代码节、ABI、资源和签名页哈希。
