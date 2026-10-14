# 完整人物轮廓的数据检查

要贴合实际衣服和头发，需要与本帧人物外形一致的网格/姿态/透明裁剪数据，或相应人物遮罩。现有 Core 的人物绘制路径使用 14 段关节投影，不能提供这些外形。此检查工具用于确定手机上实际安装的游戏版本及可见资源来源，尚未实现完整人物填色。

Apple 的公开查询将和平精英 App Store ID `1321803705` 对应到 `com.tencent.tmgp.pubgmhd`。工具只通过该身份查找本机已安装应用，报告中的版本来自其实际 Info.plist，不把公开商店版本作为手机版本。

当前检查工具为 [Releases：model-inspector-v2](https://github.com/zwthys-cyber/Core-21A327-TrollStore/releases/tag/model-inspector-v2)，1.1 / build 2，诊断协议为 2。IPA 为 25,254 字节，SHA-256 为 `2e62c127bbfe6e86ca22b6325989d8af125f22443757ec6e92e62a676852ab59`。发布的 5 个附件、4 项摘要及 `/root/Core-Model-Source-Inspector-TrollStore-v2.ipa` 已核对一致。Core 主包最新版本仍为 v19。

历史 [v1](https://github.com/zwthys-cyber/Core-21A327-TrollStore/releases/tag/model-inspector-v1) 为 1.0 / build 1，仅提供文件清单，IPA 22,384 字节，SHA-256 `547c72082ea1e0a185d46c57564114fabd6a0c824a9f46df7a073029e007b322`。

## 使用

1. 用 TrollStore 安装独立的 `Core-Model-Source-Inspector-TrollStore-v2.ipa`，覆盖已有检查工具。Core v19 可继续保留。
2. 关闭和平精英，打开“Core 模型检查”，点击“检查游戏资源”。
3. 点击“共享检查报告”，保存 `Core-model-source-report.json`。也可从“文件 → 我的 iPhone → Core 模型检查”取出。
4. 将报告放到 `/root/core`，后续据实际版本、主程序头信息及资源清单选择需要分析的数据。

检查完成只表示清单已生成；不表示已取得人物模型、遮罩或已实现白色实体轮廓。若清单达到数量或时间限制，报告会标出 `truncated`，不能把不完整清单中的缺失项当作不存在。

## 读取范围

报告包含系统构建、实际游戏版本/build、主程序的 Mach-O UUID、架构、最低系统及文件加密声明；安装目录和游戏数据容器中匹配 `.pak/.utoc/.ucas`、Unreal 资源、通用网格和 Metal 着色器库的相对文件名及大小。文件名中的人物相关词只记为线索，不能证明内容包含人物模型。

读取主程序头及最多 1 MiB 的加载命令。v2 扫描每个根目录最多 15,000 项、2,048 个资源、8 秒，并跳过符号链接。服装资源目录另行扫描，最多 4,096 项、2,048 个资源、4 秒，避免活动资源先耗尽全局清单额度。限时按条目之间检查，不是中断系统文件调用的硬性期限。

v2 根据实际游戏版本/build 定位 `map_avatar_base`、`map_avatar_filter` 和 `map_avatermesh`，另外按名称排序选择最多 8 个 `avatarpaks` 中的资源包；每个包只读前 64 字节及最后 4,096 字节，将位置、长度和 Base64 数据保存为 `pak_edge_samples`。最多 11 个样本、45,760 字节原始采样数据。不解析资源索引或模型载荷，不读取账号或设置文件内容；格式及加密状况仍需检查样本才能判断。报告保存在检查工具自己的 Documents，用户手动共享；应用没有网络请求，也不启动游戏、读取游戏进程内存或修改游戏文件。

首份 v1 实机报告已确认安装游戏为 `1.38.12 / build 15915`，安装目录清单完整，下载资源清单达到 512 项上限。其中有三个候选人物资源包和 `avatarpaks` 目录；名称仅是线索，尚未取得网格或实时遮罩。主程序的 `cryptid=1` 只说明可执行文件声明加密，不能据此推断 `.pak` 是否加密。原始用户报告保留在本地，不提交到仓库。

编译、原生文件解析/扫描回归及 IPA 签名核验均在 GitHub Actions 的 **Build model source inspector** 工作流运行。真实设备上的安装应用查询、权限和资源可读性尚需报告确认。

[构建与检查 38515678591](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38515678591) 已通过，源码为 `bfae94c0a815ae55150227a434b00ab9afd407e4`，发布标签指向该实际构建提交。检查覆盖大小端 thin/fat Mach-O、UUID/版本/加密声明、损坏或超限加载命令、非法切片、非普通文件和符号链接拒绝、资源类型清单、目录别名、路径越界及扫描上限。扫描修正后，[原 Core 的云端回归 38515678590](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38515678590) 也通过；Core 显示、范围及主包构建代码与发布 v19 的源码相同。

v2 的[构建与检查 38542283342](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38542283342) 已通过，源码及发布标签指向 `6ca0d16bd1a6be2e65f9a6f82a358a90d65afd74`。新增检查覆盖准确的头尾字节、1..65 字节小文件、大于 4 GiB 的稀疏文件、空文件/管道/目录拒绝及相对路径和链接越界；设备 IPA 构建及签名核验通过。[原 Core 的云端回归 38542283227](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38542283227) 同样通过。v2 实机采样结果仍待新报告确认。

## 参考

- [和平精英 App Store 页面](https://apps.apple.com/cn/app/id1321803705)
- [Apple 公开应用信息](https://itunes.apple.com/lookup?id=1321803705&country=cn)
- [Epic 网格渲染数据接口](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USkeletalMesh)
- [Epic Custom Depth / Stencil 轮廓说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-materials?application_version=4.27)
