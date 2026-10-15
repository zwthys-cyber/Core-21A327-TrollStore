# Core 17.0 / 21A327 巨魔适配补丁

适用于提供的 Core 1.6 IPA（输入 SHA-256 在 `scripts/build.py` 中固定）。

当前版本为 [**v19 · 0fe975f 基础 / 600 米测试版（1.6 / build 20 / patch 19）**](https://github.com/zwthys-cyber/Core-21A327-TrollStore/releases/tag/v19-600m)：恢复用户指定提交 `0fe975fc6053a0af8cac90c4a860696d08dec8c2` 的显示逻辑，将姓名、方框、射线、骨骼、屏幕外方向提示和顶部敌人数的范围统一提高到 600 米；射线起点改为屏幕上方中央，向下连接目标。真人红名、人机白名，不显示身份文字。射线使用原数量和透明度规则：100 米内不透明白色，超过 100 米采用透明度 112 的白色。详见 [v19 说明](docs/releases/v19-600m.md)。

v19 已设为 [最新 Release](https://github.com/zwthys-cyber/Core-21A327-TrollStore/releases/latest)。在 Assets 中下载 `Core-17.0-21A327-TrollStore-v19-600m.ipa`，7 个发布附件、6 项 SHA-256 摘要和根目录主 IPA 已核对一致。仓库私有，浏览器需登录有访问权限的 GitHub 账号。600 米实机效果仍待确认，显示依赖原采集流程能获取的目标。

衣服、头发完整轮廓及可见白色／遮挡红色的开发工作在 `feat/live-mesh-fill-v1` 分支推进，正式 Release 仍为 v19。开发包已接入实时模型蒙皮及逐像素红白填色，build 28 用户确认可见，但轮廓或边界不准；build 29 的原生结果缓冲越界已在 build 30 修复。当前 **build 31 / render v4** 针对随后出现的填充间歇和卡顿减少重复读取、查询取锁及绘制开销，已通过[云端验证](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38678390262)，实机性能改善与完整精确效果仍未验证。安装包、实机证据和未完成项见[实时绘制说明](docs/live-mesh-renderer.md)。历史[模型检查 v3](https://github.com/zwthys-cyber/Core-21A327-TrollStore/releases/tag/model-inspector-v3)及[采集 v1–v7](docs/model-runtime-capture.md)为该路径提供模型与姿态输入；单独的采集包不含实时填充。

用户已确认 [v12 遮挡启动修复版](https://github.com/zwthys-cyber/Core-21A327-TrollStore/releases/tag/v12-200m) 可用。历史版本见 [v18](docs/releases/v18-300m.md)、[v17](docs/releases/v17-300m.md)、[v16](docs/releases/v16-300m.md)、[v15](docs/releases/v15-300m.md)、[v14](docs/releases/v14-300m.md)、[v13](docs/releases/v13-300m.md)、[v11](docs/releases/v11-200m.md)、[v10](docs/releases/v10-200m.md)、[v9](docs/releases/v9-minimal.md) 和 [v8](docs/releases/v8.md)。后续 launchd 超时仍未解决，先前游戏封禁原因未知。

AppleDB 的 21A327 记录没有 `sources`；原程序无法下载该出厂预装版本的 kernelcache，导致环境适配失败并禁用内核按钮。补丁在原缓存验证失败后，读取 IORegistry 当前 `boot-manifest-hash` 对应目录中的 kernelcache，交给原缓存发布器生成元数据及 SHA-256，再由原验证器检查。原准备流程通过后才会变为就绪。

补丁仅针对当前系统及请求身份完全匹配的 21A327，不读取其他启动哈希目录，不替换成 21A329，不跳过原校验。失败时显示提示，日志保存在 Documents/Core-21A327-adaptation.log。主程序使用 platform-application、no-sandbox 和 AppDataContainers 权限，保留 Core 数据容器，并限定图形 IOKit 用户客户端例外，使用 TrollStore 安装。

## GitHub 构建

按用户要求，服务器不运行编译和自动化回归；编译、测试及最终签名核验均放在 GitHub Actions。此处只修改文件、执行 Git 操作、查看 CI、下载发布产物和轻量核对版本及摘要。

在私有仓库的 Actions 中运行 **Build TrollStore IPA**，下载 `Core-17.0-21A327-TrollStore-v19-600m` artifact，解压得到 Core 和“Core 环境准备”两个 IPA。原应用没有完整源码，此处编译适配 dylib、独立读取程序和缓存准备应用，按固定输入摘要实施显示修改，再注入并重签用户提供的 IPA。编译和最终包核验均保存完整的允许修改范围。

输入 IPA、已有库只在私有仓库保存，不包含用户崩溃报告或本地凭据。请保持仓库私有。

## 安装和核验

1. 用 TrollStore 覆盖安装 `Core-17.0-21A327-TrollStore-v19-600m.ipa`；已安装的准备工具可以继续使用。
2. 完全关闭 Core，打开“Core 环境准备”，点击“准备 Core 环境”。
3. 显示准备成功后，再打开 Core，等待原环境校验；两步之间不要重启手机。
4. 环境就绪后，才测试原内核按钮。
5. 悬浮注册和获取信息完成后，位置页开启“附近敌人”。顶部显示 600 米内已获取敌人红色总数；屏幕目标显示红色真人姓名或白色人机姓名、细方框、射线和距离；标记范围为 600 米；屏幕外目标显示黄色方向箭头和距离；原遮挡数据可用后显示红白骨骼。红白依据关节点，无法给出身体皮肤的精确遮挡边界。安装和签名核验通过不能证明游戏使用安全。
6. 若准备工具失败，导出“文件 → 我的 iPhone → Core 环境准备 → Core-cache-preparation.log”。运行诊断仍为 Core 的 `Core-remote-bootstrap.log`、`core-offline-original-id.log`、`Core-21A327-adaptation.log`。

用户已确认 v3 能正常打开，v4 已不再出现卡密弹窗。v6 的设备日志显示三项签名权限实际存在，但 IORegistry 当前启动哈希属性仍不可读，独立读取程序在 posix_spawn 时仍被拒绝，返回 EPERM；内核利用尚未开始。v7 已在该 iPhone16,2 / 21A327 上验证：完整缓存传输通过原校验器，环境就绪，原 pe-v1 后端到达 state=2 / ready=1，用户界面显示内核初始化成功。该结果只确认本次设备运行；随后 launchd 远程初始化出现第一次异常消息等待失败，悬浮层进入隔离状态。

## v8 远程初始化排查

原远程打开函数返回 0 表示成功。最新验证的 v8 运行中，SpringBoard 首次异常消息在 59 ms 内收到并成功打开，三个窗口注册 ID 全部匹配，用户确认获取信息成功、悬浮菜单可操作；launchd 随后仍在约 8005 ms 后接收超时，最终同时存在 hosted=1 和 quarantine=1。此前一次 v8 运行在 SpringBoard 阶段就超时，因此不能将本次成功等同于每次运行都稳定。

逆向确认首次等待调用 mach_msg，参数为接收消息和接收超时，等待 8000 ms；原代码把所有非零返回值都标记为 `bootstrap-first-exception-timeout`。v8 诊断已确认当前失败是真正的接收超时，而不是无效端口或端口死亡。

v8 为 1.6 / build 9 / patch 8。只将主程序的 mach_msg、thread_set_exception_ports、thread_resume 三个 libSystem 导入绑定到适配库中的转发函数；适配库仍导入并调用原 libSystem 实现。使用等长别名，保留导入索引、原 stub、指针认证 fixup 和全部程序代码节。按原 UUID、导入格式、库序号、符号类型、原库绑定和字符串不重叠条件核验后才修改。其他导入不变。

转发函数完整保留参数、返回值及 errno，只在原远程模块调用范围内记录调用偏移、异常端口安装/线程恢复结果、mach_msg 原始错误码和耗时、当前进程接收端口类型及消息 ID/长度。日志为 `Documents/Core-remote-bootstrap.log`，启动时写入 patch=8 检查点。没有消息体、卡密或目标进程内存内容。文件写入失败不影响原调用。不延长等待、不绕过隔离、不修改目标线程参数，不把状态强制设为成功。

回归检查覆盖全部非签名主程序字节及精确导入重绑定、未知格式/错误绑定拒绝，并在 macOS 通过真实 Mach 消息验证发送、接收、消息头、超时、无效端口、原异常/恢复调用错误、errno、调用范围及诊断写入失败。它不能模拟手机 launchd 的异常处理策略；后续超时尚未解决。

v2 修复原方法指针发布的并发窗口：先保存并发布原实现，再安装钩子。v1 将保存动作放在方法替换返回之后，原离线库已启动的预取线程可能在该窗口调用尚未保存的空指针。这是确认的代码缺陷，是否对应用户本次秒退仍需新 `.ips` 证明。构造函数改用 C / Objective-C runtime API，不在启动入口创建 Foundation 对象，并在每个启动步骤写入同一个诊断日志。版本号为 1.6 / build 3，产物名包含 `v2`。

macOS 运行时回归测试会在方法替换函数返回前立即调用新钩子，然后并发调用 2000 次，验证原实现已可用。该测试不模拟 iOS 的完整应用启动或 ARM64e 指针认证。

v3 为 1.6 / build 4 / patch 3：将动态库实际的 `LC_ID_DYLIB` current / compatibility version 显式设为 1.0.0，与主程序注入的 `LC_LOAD_DYLIB` 一致，并在构建及最终包验证中检查两者。v1/v2 的库采用链接器默认的 0.0.0，但这个元数据缺陷不能直接证明是用户 iOS 17 的启动失败原因：苹果现代 dyld 不对该目标执行旧式兼容版本检查。此版本同时显式保留 Core 自身的数据容器身份，早期日志优先使用 CFFIXED_USER_HOME，再回退 HOME。

v4 为 1.6 / build 5 / patch 4。根据用户要求跳过本地卡密弹窗，停用 `CCCoreLicense.dylib` 的两个自动入口：`+[CCCoreLicenseGuard load]`（0x4360）和唯一的 C 构造函数 `_CCCoreLicenseConstructor`（0xd5c4）。只把各入口首条 `PACIBSP` 换成普通 `RET`，在修改栈或签名 LR 之前返回，因此不安装 scene 钩子、授权弹窗、续期定时器及网络策略退出逻辑。主程序及离线库没有引用该组件的授权接口；保留库的加载命令和序号，避免改变原有绑定。构建先核验整库 SHA-256、UUID、ABI、符号地址、唯一构造函数记录和完整入口指令，再执行这两处修改；任一不匹配就停止。

这项修改只停用此 IPA 的本地授权组件，不访问授权后台、不提交卡密、不变更服务器上的设备绑定。它也不修改主程序原有的内核初始化或缓存校验。实际是否还存在其他功能门槛，需要设备测试。

v5 为 1.6 / build 6 / patch 5。v4 日志的具体失败是 `current boot-manifest-hash unavailable`，而相同读取代码在独立诊断程序中成功。两者的相关权限差异是诊断程序带有 `platform-application`；这是此次读取失败的最有依据的修复方向，尚不能仅凭日志确定权限检查的具体返回点。

新增独立 arm64 可执行程序 `CoreBootProbe`，持有 `platform-application` 和 no-sandbox，负责读取当前启动哈希及对应 kernelcache。主程序仍使用 v4 的权限配置，避免将平台权限的 IOKit 用户客户端限制引入原内核执行流程。Core 通过 posix_spawn 启动读取程序，并把在自身缓存目录创建的临时文件描述符传给它；子进程不需要打开 Core 的数据容器。父子两侧都检查 21A327 设备身份、启动哈希路径和文件大小，完成后由原发布器及原 SHA-256 缓存校验器处理数据。读取程序不运行内核利用、不联网、不加载 Core 的库，失败或超时后只清理本次子进程及临时文件。

macOS 进程回归测试验证实际描述符传递、复制内容、错误构建/路径/长度拒绝、响应超限、失败退出和超时清理，并检查描述符没有泄漏。该测试不模拟设备的 IORegistry 权限；安装 v5 后仍需查看设备日志中的 `probe current-boot acquisition passed` 和原缓存验证 `SUCCESS`。

v6 为 1.6 / build 7 / patch 6。v5 的设备日志显示 `Probe launch failed (1): Operation not permitted`；该失败发生在子进程运行前，因此只给读取程序平台权限不足以解决当前设备的父进程权限问题。TrollStore 官方说明 no-sandbox 在部分情况下需配合 platform-application，且平台应用需要单独声明 IOKit 用户客户端例外。结合独立诊断程序能够读取当前启动哈希的结果，v6 给 Core 本身增加 platform-application，同时保留原 no-sandbox、AppDataContainers 和明确的数据容器身份。

Core 导入 IOSurfaceCreate 和 MTLCreateSystemDefaultDevice。为避免新增平台权限影响这两类图形接口，声明 AGXDeviceUserClient、AGXSharedUserClient、AGXGLContext、AGXCommandQueue 和 IOSurfaceRootUserClient 例外；名称参考 TrollStore 与相同内核后端作者公开的签名配置。未引入 root 身份切换或其他应用管理权限。独立读取程序保留为后备，原缓存发布器和校验器仍决定成功与否。新日志只查询平台、no-sandbox、AppDataContainers 三项运行时权限，不记录其他签名权限或凭据。

这是根据权限失败证据实施的修复，不能从 EPERM 单独断言系统内部拒绝原因。设备核验应先看到 `patch=6` 和上述三项权限为 1，再看到原缓存校验 `SUCCESS`，最后确认 Core 的环境状态。若仍在获取资源阶段失败，完整日志会区分签名权限、直接读取和子进程读取结果。

v7 为 1.6 / build 8 / patch 7。用户更新的 v6 日志已确认 platform-application、no-sandbox、AppDataContainers 三项均为 1，但启动哈希属性和 posix_spawn 仍失败。这排除了“仅缺少平台权限”作为完整解释。不能把签名声明等同于实际无沙箱状态，也不能从单个 EPERM 区分沙箱与执行签名限制；新增 sandbox_check 自身状态记录，不修改原内核后端。

新的“Core 环境准备”是单独安装、由桌面启动的 arm64 UIKit 应用，沿用此前在设备上成功读取启动哈希的诊断应用配置，并通过 LSApplicationProxy 查找 Core 自己的数据容器。它只读取当前启动哈希下的 kernelcache，原子写入 `Documents/CoreBootCacheInput/kernelcache.bin`，随后发布 `manifest.json`。目录保存设备、构建、主板、kern.boottime、启动哈希、源路径、实际长度和 SHA-256。准备应用写入后，使用与 Core 相同的接收检查回读确认完整。它不启动 Core 子进程、不执行内核利用、不联网。

Core 在直接获取前先检查自己的输入目录：严格拒绝型号/主板/系统不符、其他启动时间、启动路径异常、类型错误、超限数据、符号链接、损坏文件或摘要不符。接受完整传输后，仍调用原 qm539 发布器和 qm509 验证器；只有两者成功才返回原准备流程。成功后删除这两个传输文件，保留原发布器建立的正常缓存。当前启动时间不可读时拒绝输入，重启后的旧传输不会被接受。Core 采用已能启动的 v6 签名和容器设置；不依赖 Core 的沙箱限制被解除。

macOS 接收测试覆盖真实文件传输、独立已知 SHA-256、错误设备/系统/主板、两种启动时间不符、路径越界、摘要/长度/数字类型错误、缺失当前启动时间、损坏文件、链接、目录及超限清单。独立应用的读取和写入权限仍需实机测试；之前的诊断结果证明读取路径可用，尚不能保证新应用写入成功。设备成功记录应依次为准备工具的 `SUCCESS prepared current-boot cache`、Core 的 `prepared current-boot acquisition passed` 和原缓存校验 `SUCCESS`。

独立诊断工具已确认用户安装的是 v2，注册状态为 System，当前启动的 kernelcache 可读；FrontBoard 返回 FBProcessExit 64 / RBSRequestErrorDomain 5，表示启动期间退出。收集到的五份 `.ips` 全部属于旧 build 1，不能解释 v2。诊断 v2 会读取实际安装后的嵌入签名元数据，并在正常启动失败后创建短时独立子进程捕获 stderr；独立启动与桌面启动的差异会在报告中注明。

## 参考

- [AppleDB 21A327 元数据](https://api.appledb.dev/ios/iOS%3B21A327.json)
- [TrollStore 权限说明](https://github.com/opa334/TrollStore#unsandboxing)
- [TrollStore 图形 IOKit 权限配置](https://github.com/opa334/TrollStore/blob/main/TrollStore/entitlements.plist)
- [内核后端作者的图形 IOKit 权限配置](https://github.com/opa334/darksword-kexploit/blob/main/entitlements.plist)
- [当前 boot-manifest-hash 读取方式](https://github.com/0x7ff/dimentio/blob/main/libdimentio.c)
- [苹果 dyld 的兼容版本策略](https://github.com/apple-oss-distributions/dyld/blob/dyld-1122.1.2/common/MachOFile.cpp#L1279)

`python3 -m unittest discover -s tests -v` 验证设备身份与启动目录隔离、未知构建拒绝、注入后代码保留、授权组件允许范围，以及精简后的 ARM64 菜单分派、输入工作线程返回、旧设置隔离、唯一开关与窗口配对。ARM64 测试使用 `unicorn==2.1.4`，只执行合成配置与 UI 桩，不模拟设备初始化或游戏。`scripts/verify.py` 检查最终 IPA 的全部非签名字节、允许精简变更、ABI、资源和签名页哈希。
