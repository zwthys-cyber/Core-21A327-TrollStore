# Core 17.0 / 21A327 巨魔适配补丁

适用于提供的 Core 1.6 IPA（输入 SHA-256 在 `scripts/build.py` 中固定）。

AppleDB 的 21A327 记录没有 `sources`；原程序无法下载该出厂预装版本的 kernelcache，导致环境适配失败并禁用内核按钮。补丁在原缓存验证失败后，读取 IORegistry 当前 `boot-manifest-hash` 对应目录中的 kernelcache，交给原缓存发布器生成元数据及 SHA-256，再由原验证器检查。原准备流程通过后才会变为就绪。

补丁仅针对当前系统及请求身份完全匹配的 21A327，不读取其他启动哈希目录，不替换成 21A329，不跳过原校验。失败时显示提示，日志保存在 Documents/Core-21A327-adaptation.log。主程序增加 no-sandbox 和 AppDataContainers 权限，使用 TrollStore 安装。

## GitHub 构建

在私有仓库的 Actions 中运行 **Build TrollStore IPA**；下载 `Core-17.0-21A327-TrollStore-v5` artifact，解压得到 IPA。原应用没有完整源码，此处编译适配 dylib 和独立读取程序，再修改本地授权组件的启动入口、注入并重签用户提供的 IPA。

输入 IPA、已有库只在私有仓库保存，不包含用户崩溃报告或本地凭据。请保持仓库私有。

## 安装和核验

1. 用 TrollStore 打开生成的 IPA，覆盖已有 Core。
2. 完全关闭 Core 后重新启动，等待环境适配完成。
3. 环境就绪后，才测试原内核按钮。
4. 若仍失败，在“文件 → 我的 iPhone → Core”导出上述日志。

用户已确认 v3 能正常打开，v4 已不再出现卡密弹窗。v4 的设备日志明确显示适配补丁成功加载，但 IORegistry 当前启动哈希读取失败；内核利用尚未开始。v5 保留 v4 主程序的权限和数据容器设置，实际环境就绪仍需设备验证。构建、签名和静态检查不能代替设备验证；资源获取补丁不保证原内核利用成功。

v2 修复原方法指针发布的并发窗口：先保存并发布原实现，再安装钩子。v1 将保存动作放在方法替换返回之后，原离线库已启动的预取线程可能在该窗口调用尚未保存的空指针。这是确认的代码缺陷，是否对应用户本次秒退仍需新 `.ips` 证明。构造函数改用 C / Objective-C runtime API，不在启动入口创建 Foundation 对象，并在每个启动步骤写入同一个诊断日志。版本号为 1.6 / build 3，产物名包含 `v2`。

macOS 运行时回归测试会在方法替换函数返回前立即调用新钩子，然后并发调用 2000 次，验证原实现已可用。该测试不模拟 iOS 的完整应用启动或 ARM64e 指针认证。

v3 为 1.6 / build 4 / patch 3：将动态库实际的 `LC_ID_DYLIB` current / compatibility version 显式设为 1.0.0，与主程序注入的 `LC_LOAD_DYLIB` 一致，并在构建及最终包验证中检查两者。v1/v2 的库采用链接器默认的 0.0.0，但这个元数据缺陷不能直接证明是用户 iOS 17 的启动失败原因：苹果现代 dyld 不对该目标执行旧式兼容版本检查。此版本同时显式保留 Core 自身的数据容器身份，早期日志优先使用 CFFIXED_USER_HOME，再回退 HOME。

v4 为 1.6 / build 5 / patch 4。根据用户要求跳过本地卡密弹窗，停用 `CCCoreLicense.dylib` 的两个自动入口：`+[CCCoreLicenseGuard load]`（0x4360）和唯一的 C 构造函数 `_CCCoreLicenseConstructor`（0xd5c4）。只把各入口首条 `PACIBSP` 换成普通 `RET`，在修改栈或签名 LR 之前返回，因此不安装 scene 钩子、授权弹窗、续期定时器及网络策略退出逻辑。主程序及离线库没有引用该组件的授权接口；保留库的加载命令和序号，避免改变原有绑定。构建先核验整库 SHA-256、UUID、ABI、符号地址、唯一构造函数记录和完整入口指令，再执行这两处修改；任一不匹配就停止。

这项修改只停用此 IPA 的本地授权组件，不访问授权后台、不提交卡密、不变更服务器上的设备绑定。它也不修改主程序原有的内核初始化或缓存校验。实际是否还存在其他功能门槛，需要设备测试。

v5 为 1.6 / build 6 / patch 5。v4 日志的具体失败是 `current boot-manifest-hash unavailable`，而相同读取代码在独立诊断程序中成功。两者的相关权限差异是诊断程序带有 `platform-application`；这是此次读取失败的最有依据的修复方向，尚不能仅凭日志确定权限检查的具体返回点。

新增独立 arm64 可执行程序 `CoreBootProbe`，持有 `platform-application` 和 no-sandbox，负责读取当前启动哈希及对应 kernelcache。主程序仍使用 v4 的权限配置，避免将平台权限的 IOKit 用户客户端限制引入原内核执行流程。Core 通过 posix_spawn 启动读取程序，并把在自身缓存目录创建的临时文件描述符传给它；子进程不需要打开 Core 的数据容器。父子两侧都检查 21A327 设备身份、启动哈希路径和文件大小，完成后由原发布器及原 SHA-256 缓存校验器处理数据。读取程序不运行内核利用、不联网、不加载 Core 的库，失败或超时后只清理本次子进程及临时文件。

macOS 进程回归测试验证实际描述符传递、复制内容、错误构建/路径/长度拒绝、响应超限、失败退出和超时清理，并检查描述符没有泄漏。该测试不模拟设备的 IORegistry 权限；安装 v5 后仍需查看设备日志中的 `probe current-boot acquisition passed` 和原缓存验证 `SUCCESS`。

独立诊断工具已确认用户安装的是 v2，注册状态为 System，当前启动的 kernelcache 可读；FrontBoard 返回 FBProcessExit 64 / RBSRequestErrorDomain 5，表示启动期间退出。收集到的五份 `.ips` 全部属于旧 build 1，不能解释 v2。诊断 v2 会读取实际安装后的嵌入签名元数据，并在正常启动失败后创建短时独立子进程捕获 stderr；独立启动与桌面启动的差异会在报告中注明。

## 参考

- [AppleDB 21A327 元数据](https://api.appledb.dev/ios/iOS%3B21A327.json)
- [TrollStore 权限说明](https://github.com/opa334/TrollStore#unsandboxing)
- [当前 boot-manifest-hash 读取方式](https://github.com/0x7ff/dimentio/blob/main/libdimentio.c)
- [苹果 dyld 的兼容版本策略](https://github.com/apple-oss-distributions/dyld/blob/dyld-1122.1.2/common/MachOFile.cpp#L1279)

`python3 -m unittest discover -s tests -v` 验证设备身份与启动目录隔离、未知构建拒绝、注入后代码保留，以及授权组件仅两个入口指令改变；`scripts/verify.py` 检查最终 IPA 的所有代码节、ABI、资源和签名页哈希，严格限定许可库的八字节修改。
