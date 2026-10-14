# 完整人物轮廓的数据检查

要贴合实际衣服和头发，需要与本帧人物外形一致的网格/姿态/透明裁剪数据，或相应人物遮罩。现有 Core 的人物绘制路径使用 14 段关节投影，不能提供这些外形。此检查工具用于确定手机上实际安装的游戏版本及可见资源来源，尚未实现完整人物填色。

Apple 的公开查询将和平精英 App Store ID `1321803705` 对应到 `com.tencent.tmgp.pubgmhd`。工具只通过该身份查找本机已安装应用，报告中的版本来自其实际 Info.plist，不把公开商店版本作为手机版本。

## 使用

1. 用 TrollStore 安装独立的 `Core-Model-Source-Inspector-TrollStore-v1.ipa`。Core v19 可继续保留。
2. 关闭和平精英，打开“Core 模型检查”，点击“检查游戏资源”。
3. 点击“共享检查报告”，保存 `Core-model-source-report.json`。也可从“文件 → 我的 iPhone → Core 模型检查”取出。
4. 将报告放到 `/root/core`，后续据实际版本、主程序头信息及资源清单选择需要分析的数据。

检查完成只表示清单已生成；不表示已取得人物模型、遮罩或已实现白色实体轮廓。若清单达到数量或时间限制，报告会标出 `truncated`，不能把不完整清单中的缺失项当作不存在。

## 读取范围

报告包含系统构建、实际游戏版本/build、主程序的 Mach-O UUID、架构、最低系统及文件加密声明；安装目录和游戏数据容器中匹配 `.pak/.utoc/.ucas`、Unreal 资源、通用网格和 Metal 着色器库的相对文件名及大小。文件名中的人物相关词只记为线索，不能证明内容包含人物模型。

只读取主程序头及最多 1 MiB 的加载命令。扫描每个根目录最多 15,000 项、512 个资源、8 秒，并跳过符号链接。只列资源文件名和大小，不读取资源内容、账号或设置文件内容。报告保存在检查工具自己的 Documents，用户手动共享；应用没有网络请求，也不启动游戏、读取游戏进程内存或修改游戏文件。

编译、原生文件解析/扫描回归及 IPA 签名核验均在 GitHub Actions 的 **Build model source inspector** 工作流运行。真实设备上的安装应用查询、权限和资源可读性尚需报告确认。

## 参考

- [和平精英 App Store 页面](https://apps.apple.com/cn/app/id1321803705)
- [Apple 公开应用信息](https://itunes.apple.com/lookup?id=1321803705&country=cn)
- [Epic 网格渲染数据接口](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USkeletalMesh)
- [Epic Custom Depth / Stencil 轮廓说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-materials?application_version=4.27)
