# Core / 和平精英美化项目 AI 交接文档

更新时间：2026-10-16 UTC。本文记录用户目标、已完成工作、实机证据、失败经过和接手位置。

**当前结论：金龙外套已实机确认；r8 已有 8/8 次车辆 3349001 的成功读取，使用 pawn+0x60C8 控制器回退。r9 已实现 3349001→3349002 的 15 秒数据请求试换和恢复记录，等待 r9 实机报告。该待换肤字段能否单独触发原生消费和画面变化仍未证明，不能宣称完整车辆换肤完成。**

## 1. 接手必须先知道的约束

1. 用户明确要求：**不要在这台服务器上验证、编译或运行测试，容易卡死。所有编译、自动化测试、签名核验、游戏二进制分析和反汇编都放在 GitHub Actions。**
2. 本机只做文本编辑、Git 操作、读取云端分析报告和 CI 日志、下载产物、轻量版本与摘要查看。不要在本机运行 `scripts/build.py`、`scripts/verify.py`、`analysis/cosmetics/analyze.py`、`vehicle_review.py`、PAK 解析、clang、Unicorn 或 unittest。仓库 `AGENTS.md` 已记录此限制。
3. 仓库是私有仓库。游戏主程序、美化包和原始 Core IPA 不要发布到公开 Release。现有分析输入保存在私有草稿 Release 中。
4. 用户希望直接推进工作，不反复询问已授权的常规操作。原生菜单位置已经指定：**Core 的“位置”栏目下面添加“美化”**，不能另做一个与 Core 分离的悬浮面板。
5. 游戏版本固定为 **和平精英 1.38.12 / build 15915**；手机为 **iOS 17.0 / 21A327、已装 TrollStore（巨魔）**。更换游戏 UUID、版本、Core 输入或系统条件后，旧地址不能直接沿用。
6. 用户问过哪种方式不易被检测。当前没有“防检测”或“保证不封号”的验证结果，不要作这种承诺。当前美化路线使用已有游戏资源和进程内外观数据，不替换游戏 PAK。
7. 不要把静态候选、模拟内存测试通过、IPA 签名通过，表述为手机实机成功。也不要把“安装包可以”扩大成所有功能均已验证。

## 2. 用户要达到什么目标

### 2.1 仓库与历史版本整理

用户最初要求删除 `v19-600m` 之后的版本及相关分支，包括 17pm 测试版本。

该清理此前已经执行：恢复主分支到 v19 基线，清理后续测试分支、标签、发布及部分 Actions 产物；保留需要的旧诊断分支。历史操作记录为删除 9 个后续分支、4 个标签、5 个 Release（含草稿）、55 个产物，以及重复构建。历史快照位置为 `/tmp/core-release-cleanup-hfs5iyf2`，该临时目录不是长期唯一凭据。

**不要再次按“v19 之后全部删除”自动删除现在的美化开发分支。** 后续会话已经明确授权在 `feat/kernel-cosmetics` 上继续开发。

### 2.2 将两个美化包的效果集成到 Core

用户提供了两个 ZIP、游戏 `Info.plist` 和 `ShadowTrackerExtra`，希望做成 Core 内核美化，并将操作放入原有 Core 菜单。

用户最初用“sh 内核”描述需求，随后明确集成对象是 Core，并选择了内存外观 ID 的路线。目前实际产物是 **修改后的 TrollStore IPA：原 Core + 适配 dylib + 原生菜单入口 + 原 Core 读写接口**，不是一个独立 `.sh` 文件，也不是完整重写的内核源码项目。

### 2.3 当前优先级

用户最新功能要求是“车辆弄好”。服装其他类别、大厅、头盔、背包、枪械仍有未完成项，但当前不要偏离车辆问题。

用户已经确认：

- 在训练场车辆驾驶位操作过车辆采集。
- 游戏有换车辆皮肤的按钮，但账号没有车辆皮肤。
- 采集普通车不需要买车皮，也不需要点游戏内换肤按钮。r8 已取得普通车辆的稳定样本，尚未开始车辆外观写入。

## 3. 工作区、分支和当前代码状态

| 项目 | 位置 / 状态 |
|---|---|
| 工作区 | `/root/Core-21A327-TrollStore` |
| 用户素材和下载产物 | `/root/和平精英` |
| GitHub | `https://github.com/zwthys-cyber/Core-21A327-TrollStore`，私有 |
| 开发分支 | `feat/kernel-cosmetics` |
| 主分支基线 | `e258037c8341f6290859fd310b4f6a09b2fe7ee4`，v19-600m |
| 最新试换代码 | `feb99d180133d2d0e643b7e5b9b5a8580ffe9682`，r9 |
| 最近有用户实机报告的代码 | `b49e157b7249a9500bbc78626bcd5d1c60108e6c`，r8，8/8 次成功采集 |
| r8 | **只读取车路径及当前车辆布局已有实机样本；尚未修改车辆外观** |
| r9 | **单车待换肤数据请求和恢复已实现，尚无实机画面与消费确认** |

本文提交之后，HEAD 会再多出文档提交；识别最新可安装包时，以构建对应的 `headSha` 为准，不要把分析或文档提交当成新安装包。

原 Core 没有完整源码。本仓库编译适配库、环境准备工具和辅助程序，对固定摘要的原 IPA 实施有边界的修改、注入和重签。

## 4. 项目经过：先恢复 Core 基线，再逐步做美化

### 4.1 Core / iOS 17.0 基础适配历史

更早阶段处理过 21A327 的 kernelcache 获取、缓存准备和原有远程初始化问题。完整历史在仓库 `README.md` 及 `docs/releases/`。

关键事实：

- 使用独立“Core 环境准备”应用，读取当前启动对应的 kernelcache，交给 Core 原缓存发布器与校验器。
- 只接受当前设备、系统、主板、启动时间、启动哈希、路径、长度和摘要匹配的缓存。
- 早期启动、权限和子进程问题经历 v2～v7 调整；v7 曾在 iPhone16,2 / 21A327 上确认环境就绪、原后端 ready。
- v8 曾确认 SpringBoard 远程打开及悬浮菜单成功，但 launchd 远程初始化的接收超时仍有未解决记录。
- 后续主分支保留 v19 显示基线。美化任务不应顺带改动这些历史基础模块或启用已关闭的输入工作线程。

### 4.2 美化 r1～r9

| 阶段 | 做了什么 | 实际证据 / 限制 |
|---|---|---|
| 素材整理 | 云端解析 ZIP / PAK 目录、配料表和游戏 Mach-O、反射元数据 | 得到候选资源、名称、ID 和布局；不是完整可用配置 |
| r1 | 只读角色外观数组、检查 UUID、归属和多次读取稳定性 | 用户报告得到初始化套装，30 个同步槽位、客户端数组为空 |
| r2 | 调用 Core 原比较写接口，对现有值做同值 4 字节写入和回读 | 用户报告 `same-value-write-confirmed`；没有改变外观 |
| r3 | 白 T 恤 `403251` → 至尊金龙外套 `403188`，利用游戏自己的角色延迟刷新，15 秒后恢复 | 用户明确说“出现了，随后恢复白 T 恤”；这是最强实机画面证据 |
| r4 | 把操作接进 Core 原生悬浮菜单，“美化”位于“位置”下面 | 用户说“r4安装包可以”；不能扩展为所有控件和项目都已验证 |
| r5 | 扩充为 143 个候选，分类、分页、单项试穿、恢复实际原 ID；附带只读武器候选采集 | 云端测试通过；其余 142 个候选没有逐项实机画面确认 |
| r6 | 新增“车辆适配 → 采集车辆”，尝试从角色 `+0x3710` 取得当前车 | 用户坐驾驶位仍失败，39 次和 17 次采样都无车辆；该直接候选路径未成立 |
| r7 | 改读“载具使用组件”缓存，再取组件中的 Vehicle；增加阶段诊断、上下文和最近 12 次历史 | 用户最新报告 30 次仍失败；缓存字段可读，但未取得有效组件；车辆采集仍未成功 |
| r8 | 云端确认中间对象为 STExtraPlayerController；核对当前角色与控制器，按原生稀疏位图查找控制器所属组件；允许原皮车辆的空外观数组 | 用户报告 8/8 次成功：3349001、11 槽位、控制器及组件归属一致；车辆写入仍未实现 |
| r9 | 固定 3349001→3349002，提交主槽 / 更新计数 / Pending 数据请求，约 15 秒后恢复并记录原生字段 | 属于限时实验；请求单独消费、画面变化和恢复仍需用户手机报告 |

实机证据文件：

- `analysis/cosmetics/device-evidence-r1.json`
- `analysis/cosmetics/device-evidence-r2.json`
- `analysis/cosmetics/device-evidence-r3.json`
- `analysis/cosmetics/device-evidence-r4.json`
- `analysis/cosmetics/device-evidence-r8.json`（用户完整重复报告的压缩摘要，8 次稳定车辆采样）

注意：`trial-profile.json` 内仍有早期 `visual_effect_device_verified: false`，它不是对后续用户画面确认的否定。金龙结果应结合 `device-evidence-r3.json` 和用户原话；不要把整个候选目录改成已验证。

## 5. 当前能用和不能宣称完成的内容

### 已实现

- Core 原生菜单“美化”入口。
- 5 个服装类别：外套 60、头发 53、裤子 14、鞋子 5、降落伞 11，共 143 个名称与 ID 候选。
- 每页 4 项，上一页、下一页、换分类。
- 每次只试穿一个槽位，约 15 秒自动恢复；手动关闭可提前恢复。
- 记录当前实际原 ID，恢复原物品，而不是统一恢复白 T 恤。
- 切换项目前尝试恢复上一项；未解决的恢复记录会阻止继续覆盖。
- 会话、目标、角色、数组、物品实例与预期值检查；失败时停止写入。
- 服装报告、只读武器候选、车辆采集入口和车辆失败诊断。
- r8 控制器解析和稀疏组件查找已有实机确认：8 次采集成功，当前车 3349001。
- r9 单车数据请求试换、三字段比较写和恢复记录；没有真实画面结果。

### 尚未完成

- **车辆请求消费、画面换肤及可见恢复尚未实机验证；r9 的数据写入不能当作完整换肤完成。**
- 大厅换装尚未适配。
- 头盔、背包、枪械换肤尚未实现；武器部分只是只读候选读取。
- ZIP 中自定义模型、材质、发光、特效、检视等内容未整体转换。
- 143 个候选不等于 143 个手机上已验证生效的外观。
- 修改已有资源的外观 ID，不等于服务端账号获得相应皮肤。

## 6. 输入文件和云端取用方式

本地输入都在 `/root/和平精英/`。固定清单为 `analysis/cosmetics/inputs.json`。

| 文件 | 大小 | SHA-256 |
|---|---:|---|
| `ShadowTrackerExtra` | 298440800 | `93e52106e465717b43ea13c65ff5ee8e5d392696f90fe3bfaa90fd9069aeb280` |
| `Info.plist` | 10734 | `6bdb258ae68e98ed188bb518ebab3f06f5661952e6f557a838005be85ca13ea5` |
| `9.23美化通用原版.zip` | 65922123 | `603f107a08fd24dfe64d39ac809ccb6357ff8fcefa7b297dba63bd25b951d421` |
| `10月11日小白至尊版最新美化修复和谐.zip` | 60592286 | `cb867a14760de366e653eb66c28d660e2bca36d1648c991339e9b597f8edcb1b` |

游戏身份：

- Bundle ID：`com.tencent.tmgp.pubgmhd`。
- 版本：`1.38.12`，build `15915`。
- UUID：`34B785B2-0DAB-3992-985D-359E6BF45585`。
- 原始主程序为 thin arm64，云端确认加密标记为 0。

原 Core 输入：

- `input/Core.ipa` SHA-256：`04d5f9763f5c4dcd19d40fcd52d152cebaa7c18ec68dedb0e90c972fb1c2153c`。
- Core 主程序 UUID：`4ED1FD29-A46A-36B3-8DFC-36F67EA929D1`，arm64e。
- 原 Core 主程序 SHA-256：`ed69f33902e17c499aa7904751a01ce3521a663503fb8958ea01a7c5489d5c58`。

私有草稿 Release：

- tag：`analysis-cosmetics-1.38.12-15915`。
- Release ID：`414807300`。
- 游戏主程序 asset：`646926381`。
- Info.plist asset：`646926375`。
- 旧 ZIP asset：`646926374`，上传名称 `9.23.zip`。
- 新 ZIP asset：`646926378`，上传名称 `10.11.zip`。

Actions 通过 asset ID 下载，并检查 GitHub 返回的 size / digest，再由分析脚本核对摘要。访问草稿 Release 需要现有工作流中的 `contents: write`；不要未经确认改成只读权限后误判输入丢失。

## 7. 名称、ID 和 PAK 是怎样处理的

分析在云端进行，步骤是：

1. 读取压缩包的文件目录和文字配料表。
2. 对 PAK 做有界目录和已支持压缩方式解析。
3. 从包内 Item 等 uasset 的 NameMap 提取标识信息。
4. 对照固定版本的外部纯文本名称 / ID 目录。
5. 仅将目标名称精确且唯一、ID 出现在包内 Item 名称表、且符合已支持角色槽位的条目加入服装目录。

外部目录不是可信代码来源：

- 仓库：`yunmengnb666/YouthfulTool`。
- 固定提交：`e0a0674772fa039a46bb0ac8b1fece5f1cebe68e`。
- 文件：`和平美化代码.h`。
- Git blob SHA-1：`3245306baf3335697cbd1fcd570a30ee06be72b1`。
- 只读取其中名称和数字 ID，不执行代码，不使用它附带的地址偏移，不捆绑第三方二进制。

相关代码：`catalog_match.py`、`catalog-source.json`、`selected-avatar-catalog.json` 和生成的 `patch/CoreCosmeticsCatalog.h`。

新 ZIP 有 3 个配料表，曾整理出 1157 条文字记录、1110 对去重关系、11 个源名称冲突。9.23 ZIP 只有两个 PAK，没有文字配料表。两个包里同名的 15990 PAK 内容不同。

PAK 配料中存在跨车型替换，不能把每条文字关系直接当成原生车辆皮肤的兼容关系。截断名称、多 ID 同名、仅前缀匹配的条目，不应猜一个 ID 后自动写入。

车辆文字清单另存于 `/root/和平精英/包内车辆皮肤清单.md`。里面包括法拉利、URUS、保时捷、M4、Ghibli、Cybertruck、Imola、宾利、摩托、快艇、自行车和坐骑；这是包内配料记录，不是已实现功能名单。

## 8. 源码文件分工

| 文件 | 用途 |
|---|---|
| `patch/CoreBootCache.m` 及环境准备相关文件 | Core 原有 21A327 适配；当前车辆任务不要顺带改动 |
| `patch/CoreCosmeticsProbe.m` | 原 Core 接口绑定、采样、角色试穿控制、车辆采集控制、报告、原生菜单导出 |
| `patch/CoreCosmeticsRead.h` | UUID / 读边界 / TArray 稳定读取，角色 MeshSynData 解析 |
| `patch/CoreCosmeticsWriteCheck.h` | 原比较写 ABI 与同值写检查 |
| `patch/CoreCosmeticsTrial.h` | 泛化后的单槽位试穿与恢复；旧函数名仍有 Coat，不代表只支持外套 |
| `patch/CoreCosmeticsCatalog.h` | 生成的 143 项服装候选 |
| `patch/CoreCosmeticsPage.h` | 原生分类、分页、控件和车辆采集按钮 |
| `patch/CoreCosmeticsUIProfile.h` | 原生 UI 函数 RVA 与入口字节保护 |
| `patch/CoreCosmeticsVehicleRead.h` | r8 控制器来源解析、组件归属与 Character 核对、稳定车辆外观读取 |
| `patch/CoreCosmeticsComponents.h` | r8 新增；原生类继承核对、内联 / 堆位图和有界稀疏组件查找 |
| `patch/CoreCosmeticsVehicleTrial.h` | r9 固定车型 / 实例 / 数组门槛、三字段比较写、修改记录和恢复观察；不调用游戏函数 |
| `scripts/build.py` | 固定输入 IPA、编译适配库、打包与元数据 |
| `scripts/cosmetics_menu.py` | 在 v19 允许范围内增加原生菜单入口和导出函数桥 |
| `scripts/verify.py` | 最终 IPA 的修改范围、ABI、资源、签名等检查；只在云端执行 |
| `analysis/cosmetics/analyze.py` | 游戏 / Core 固定二进制的反射和反汇编分析 |
| `analysis/cosmetics/vehicle_review.py` | 车辆专项云端分析 |
| `analysis/cosmetics/vehicle-windows.json` | 车辆函数窗口、指针表和字段访问的云端分析配置 |
| `analysis/cosmetics/vehicle-source-evidence.json` | r6 / r7 失败证据与 r8 云端确认的控制器、类型及位图路径；不能当作实机通过证明 |
| `analysis/cosmetics/vehicle-trial-profile.json` | r9 配料对应、主槽类型及数量、原生请求写入证据与未验证边界 |
| `analysis/cosmetics/采集版使用说明.txt` | 随安装包交付的使用说明 |
| `tests/test_cosmetics_*.c`、`*.py` | 角色、写入、目录、菜单、车辆等回归，仅云端运行 |

## 9. 已有角色试穿机制：不要重新逆向一遍

以下偏移 / RVA 只适用于固定输入。RVA 要加对应进程的运行时 image base；不要笼统地把文件偏移等同于运行时地址。

### 9.1 原 Core 读写接口

| 项目 | Core RVA / 说明 |
|---|---|
| 快照 | `0x64C7C`，输出 0x88 字节 |
| 读取 | `0x64D04`，内部转到原读取实现；单次上限 16 KiB |
| 比较写 | `0x620B8`，接受 1 / 4 / 8 字节预期值比较写 |
| 绑定身份 | `0xC288B0`，0x30 字节，包含后端 / 进程 / pmap 身份、PID 和代次 |
| Core 上下文 | `0xC54270` |

上下文中的字段：game base `+0x18`、world `+0x370`、controller `+0x390`、pawn `+0x3A0`。原 Core 的 controller → pawn 路径使用 `controller+0x3540`。

比较写的 C ABI：

```c
typedef struct { uint64_t state, detail; } CCPNativeWriteResult;
CCPNativeWriteResult Write(uint32_t pid, uint64_t address,
                          const void *expected, const void *replacement,
                          size_t length);
```

**成功必须是返回 state 的低 32 位枚举等于 1。3、5 是失败，不是可按位判断的成功标志。** 还要核对完成长度、原生回读可用标记，以及自己的稳定回读。

原接口负责 PID / 绑定 / 代次和底层映射检查。不要强制改能力位、绕过原准备状态或另写一个不带这些限制的写入口。具体 32 字节入口保护值已在 `CoreCosmeticsProbe.m`，不要凭本文重新抄一套。

arm64e 本地函数指针通过 `ptrauth_sign_unauthenticated` 与正确 C ABI 绑定。

### 9.2 角色外观布局

- `pawn+0x3F78` → CharacterAvatar。
- Avatar UObject Outer `+0x20` 必须等于 pawn。
- 同步 TArray：`avatar+0x1180`；客户端数组：`avatar+0x1190`。
- 实机初始同步槽位数量为 30；客户端数组数量为 0。
- 角色 MeshSynData 步长 `0x38`，type 在 `+0`，物品 ID 在 `+4`，gender 在 `+0x18`，operation 在 `+0x1C`，version 在 `+0x30`。
- 物品实例标识等私有字段只在进程内用于身份比较，不导出到 JSON。

用户初始非零槽位：

| 槽位 | ID | 说明 |
|---:|---:|---|
| 1 | 400014 | 初始头部物品 |
| 2 | 40601010 | 头发 |
| 5 | 403251 | 初始白 T 恤 |
| 6 | 404007 | 裤子 |
| 7 | 405011 | 鞋子 |
| 11 | 703001 | 降落伞 |

已支持的目录范围：槽 2 为 `[40600000,40700000)`，槽 5 为 `[403000,404000)`，槽 6 为 `[404000,405000)`，槽 7 为 `[405000,406000)`，槽 11 为 `[703000,704000)`。

### 9.3 已验证的角色刷新

- CharacterAvatar 原生 vtable：未滑动 VA `0x10E6189A8`。
- vtable `+0x1320` → OnRep RVA `0x43D2EB0`。
- vtable `+0xC68` → 延迟更新 RVA `0x43D25A8`。
- vtable `+0x4C0` → Tick RVA `0x43D6D10`。
- game RVA `0x11069CD5` 必须为 1。
- `avatar+0xF50` 是待刷新字节，允许 0 / 1；比较写 0→1 后由游戏线程消费。
- 原生流程设置 F51、重置 F50、调用 OnRep，再清 F51。

金龙试穿确实用这条路径显示并恢复。没有远程线程执行、远程代码注入或 vtable 改写。

恢复只在仍持有本次修改的槽位、原数组、物品实例及当前目标值上执行。游戏自行更改值后，不覆盖游戏的新值。

## 10. 原生菜单怎样接进去的

`scripts/cosmetics_menu.py` 基于 `minimal_core()` 的 v19 输出，只恢复“位置”后面原有一行，将原“物资”文字改为“美化”，沿用原行的间距、点击检测和选择状态。

- Position 仍为选择值 2；Cosmetics 使用原选择值 1。
- 桥接区 RVA `0xD0818`，符号字符串位于 `0xD0840`。
- 通过原 `dlsym` stub `0x725A10` 查找 `CoreCosmeticsRenderPage`。
- 通过 arm64e `BLRAAZ` 调用；符号为空时走原函数结尾。
- 原生 UI 入口：按钮 `0xC7A60`、BeginChild `0xC88B0`、Checkbox `0xC8928`、EndChild `0x12E620`、SetCursorPos `0x133168`、Dummy `0x14F980`。
- Core UI scale 位于 `Core+0xC15A80`，有数值范围检查。
- BeginChild / EndChild 在页面裁剪时仍必须配对。
- 原控件标题会直接显示文字，不要在标题里加 ImGui `###` 隐藏 ID，之前确认它会把这部分也画出来。

不要用 UIKit UIButton 重做独立面板，不要改回与 Core 分离的入口。

## 11. 车辆调查的已知事实与错误经过

### 11.1 车辆外观组件的静态候选

这些结构尚未取得成功实机车辆样本：

- Vehicle actor 的 AvatarComponent 候选字段：`+0x1ED8`。
- VehicleAvatar 的 OwnerVehicle：`+0x10B8`。
- VehicleAvatar 的同步 SynData：`+0x11E8`，**步长 0x30，与角色的 0x38 不同**。
- SynData 第一项的物品 ID 在 `+4`。
- VehicleAvatar vtable：未滑动 VA `0x10E6D6A38`。
- vtable `+0xD18` → 原生 OnRep RVA `0x47EE7C8`；已见另一子类包装 RVA `0x881F248`。
- vtable `+0x4C0` → 基类 Tick RVA `0x42E4F28`。
- vtable `+0xC68` → RVA `0x42E8C34`，这里只重置 F50。
- Vehicle actor 皮肤候选字段：Pending `+0x20AC`、Skin `+0x20B0`、Pre `+0x20B4`、Real `+0x20B8`。
- VehicleAvatar `DefaultAvatarID` 位于 `+0x10A0`，当前读取它的 ID 部分 `+4`。

**现有证据不足以证明把车辆组件 F50 写成 1 就能像衣服一样刷新。车辆没有实现任何外观写入。**

早期分析窗口中出现的 `0x5E33CE8` 曾被误标成车辆 Actor OnRep；该函数访问疑似武器组件的 `+0x1748` 等字段，归属未确认，配置现在已改名为 `unclassified actor OnRep; ownership unconfirmed`。不要拿它直接实现车辆刷新。

### 11.2 r6 为什么不足以诊断

r6 把角色上的 `+0x3710` 反射候选直接用于取车。它把读取失败、空值和无效指针都合成 `current-vehicle-unavailable`。

用户已坐在驾驶位，两次报告分别 39、17 次尝试，无成功样本。所以不能解释为用户没有上车，也不能从那个旧状态码断言原始字段恰好为 0。

### 11.3 r7 做了什么，仍错在哪里

云端找到了以下原生调用关系：

1. `GetVehicleOfCharacter` 包装调用 RVA `0x3A77E8C`。
2. 其中先调用输入对象 vtable `+0x1528` 的解析函数。
3. 对解析结果调用组件查找 helper `0x3965BB4`。
4. helper 获取 VehicleUserComponent 的原生 UClass，然后调用该对象的 vtable `+0x8B8`。
5. 取得组件后，读取 `component+0x158` 作为 Vehicle。

另一个 helper `0x4C86960` 会先读取输入对象 `+0x47F8` 的组件缓存，缓存为空才调用 `0x3965BB4` 并写回缓存。

**r7 不完整地复现了这条路径：直接在 Core 缓存的 pawn 上读取 `+0x47F8`，没有复现先解析对象的步骤，也没有实现缓存为空时的原生组件查找。字段所属对象不能仅凭函数名推断。**

### 11.4 用户最新 r7 报告：接手应以此为准

用户仍在驾驶位，报告：

```json
{
  "schema_version": 7,
  "capture_attempts": 30,
  "samples": [],
  "status": "vehicle-not-sampled",
  "finish_reason": "manual-stop",
  "latest_vehicle": {
    "status": "vehicle-user-component-unavailable",
    "path_diagnostics": {
      "vehicle_user_field_read": true,
      "vehicle_user_available": false,
      "vehicle_user_owner_matches": false,
      "current_vehicle_field_read": false,
      "current_vehicle_nonzero": false,
      "current_vehicle_pointer_valid": false
    }
  },
  "core_context": {
    "controller_pawn_available": 1,
    "controller_pawn_matches_cached": 1,
    "controller_pawn_field_read": true,
    "character_avatar_owner_matches": true,
    "pawn_vtable_rva": "0xe7b1108",
    "character_resolver_rva": "0x4c1158c"
  }
}
```

最近 12 次历史（第 19～30 次）相同。这里能确认：Core 的 controller→pawn 与缓存 pawn 一致，角色 Avatar 的归属一致，组件缓存字段读取成功，但未得到有效组件。`available=false` 仍不能区分原值为零还是不满足指针规则；没有导出原始指针。

### 11.5 r7 反馈后的云端分析线索

用户给出的 pawn vtable 对应未滑动 VA `0x10E7B1108`。已据此在 GitHub 分析：

- 该表 `+0x1528` 确实为 `0x4C1158C`。
- `0x4C1158C` 调用 `0x3DA2D20`，结果为空时取输入对象 `+0x60C8`。
- `0x3DA2D20` 先取输入对象 `+0x608`，调用类型判断 helper `0x36262E4`，失败后另有回退流程。后续 r8 云端读取类名字面量确认：这是 `STExtraPlayerController` 类型判断，并非普通有效性函数。
- `GetVehicleOfCharacter` 的中间对象是 controller，不能继续假设组件来源必定属于 pawn。r8 已据此实现受类型、Core 当前控制器和当前 pawn 约束的来源解析；尚未通过实机确认。
- pawn vtable `+0x8B8` 为 `0xA2EE5D8`。它先检查输入对象 `+0x658` 的特殊组件，类型不匹配后跳到通用 Actor 组件查找 `0xA149148`。
- `0xA149148` 迭代 Actor 的组件集合：相关字段 `actor+0x440`、位标记相关 `+0x450` 和 `+0x468`；组件项步长 16。类型判断读取组件 UObject 的 class `+0x10`，以及 class 的 `+0x88` / `+0x90` 继承缓存。
- 位标记迭代 helper 是 `0x27026C8`、`0x2702724`。后续 r8 已在云端审查：内联位图 `+0x450`，堆位图指针 `+0x460`，位数量 `+0x468`，容量 `+0x46C`；不是普通 TArray。
- VehicleUserComponent 原生 UClass getter 为 `0x72CA9A4`，全局缓存未滑动 VA `0x111CCB6E8`（RVA `0x11CCB6E8`）。
- VehicleUserComponent 反射构造包装 `0x72CB3EC` 跳到 `0x4D9C69C`。
- 其反射属性可见 Vehicle `+0x158`、UnmannedVehicle `+0x160`、Character `+0x168`、VehicleUserState `+0x170`、重连数据 `+0x178` / `+0x198`、LastDriveVehicle `+0x1B8`。

前期分析任务：`38932297639`，成功，代码 `fcb9330`。

报告已下载：

```text
/root/和平精英/Core美化映射分析/角色组件迭代与控制器路径/vehicle-review.json
/root/和平精英/Core美化映射分析/角色组件迭代与控制器路径/vehicle-review.txt
```

前一轮包含用户 pawn vtable 的报告：

```text
/root/和平精英/Core美化映射分析/角色组件原生查找入口/vehicle-review.json
/root/和平精英/Core美化映射分析/角色组件原生查找入口/vehicle-review.txt
```

### 11.6 r8 如何修正，以及尚未证明的部分

原生类名确认任务 `38934153584`（`bacd08e`）得到 `STExtraPlayerController` 和 `VehicleUserComponent`。前一任务 `38933859383` 审查了位集合 ABI。对应报告位于 `/root/和平精英/Core美化映射分析/车辆来源类名确认/` 和 `/root/和平精英/Core美化映射分析/组件位集合与来源类型/`。

r8 使用 `pawn+0x608` 并做原生 IsA 核对；必要时只接受与 Core 缓存控制器完全一致的 `pawn+0x60C8` 回退。控制器 `+0x3540` 必须指向当前 pawn。来源 UClass 缓存 RVA 为 `0x11C5A650`。

随后按控制器 `+0x440` 的稀疏组件集合及占用位图查找 VehicleUserComponent；类缓存 RVA `0x11CCB6E8`。最多 512 槽位，双读集合、位图和条目；只使用占用槽位，多个类型匹配会拒绝。组件 Outer 必须等于控制器，组件 Character `+0x168` 必须等于当前 pawn，再取 Vehicle `+0x158`。不读取角色或控制器的 `+0x47F8` 缓存。

原皮车辆可能没有同步外观项，因此 r8 接受稳定的零长度数组及零皮肤 ID；不会将这些情况单独判为未上车。采样前后还核对 Core 绑定身份和缓存控制器。报告 schema / probe revision 为 8，`vehicle_path=controller-owned-vehicle-user-component`，新增组件集合数量、位图、查找状态和角色归属等诊断。

构建代码 `b49e157`，Actions `38935444227` 成功。用户后续报告已确认车辆只读采集成功，见下节。具体安装和测试见 `docs/cosmetics-r8.md`。

### 11.7 r8 实机采集成功：当前最新反馈

用户粘贴的报告 `captured_at_unix=1792170414.141237`，schema / probe revision 8，8 次尝试取得 8 个成功样本，`finish_reason=capture-complete`。八次数据一致，压缩摘要保存在 `analysis/cosmetics/device-evidence-r8.json`。

- 当前、前一、Real 和 Default 皮肤均为 `3349001`，Pending 为 0。
- 同步数组有 11 槽位；主槽 0：type 33、ID 3349001、native update counter 1；其他十槽均为零。
- 组件集合 39 个槽位、39 个占用项，数组容量 47；内联位图容量 128。
- `pawn+0x608` 为零，**实际成功的是 `pawn+0x60C8` 回退路径**，且等于 Core 当前控制器。不要在重构时删除此回退。
- source vtable RVA `0xE7BBC38`，VehicleUser `0xE7C5F90`，Vehicle actor `0xEBC1680`，Avatar `0xE6D6A38`。
- Avatar OnRep `0x47EE7C8`、Tick `0x42E4F28` 与静态分析一致；组件归属、Character 与当前 pawn、数组稳定性检查均通过。

报告中的 `runtime_layout_validated=false` 是 r8 固定写入的适配阶段标记，不否定这八次真实读取结果；但也不能凭当前样本宣布所有车型、刷新及写入均已验证。衣服的 F50 刷新机制仍不能直接套到车辆。

### 11.8 r9 单车请求试换：当前最新产物

代码 `feb99d1`，构建 `38959705176` 成功，尚无 r9 手机报告。仅接受实机已读到的车辆 / Avatar vtable、type 33 和 11 个同步槽位，固定 `3349001→3349002`。原始配料对应来自新 ZIP 的衣服枪械配料表第 542 行。

原生 OnRep 在 `0x47EE9A8..0x47EE9B0` 读取主槽 ID 并写入 `Vehicle+0x20AC`；`0x47EEA10..0x47EEA64` 比较主槽计数与 `Avatar+0x1208` 缓存。专项云端分析任务为 `38937613465`、`38937875743`、`38957112182`，本地文本报告分别在 `r8车辆刷新路径/`、`r8车辆字段消费路径/`、`r8车辆寄存器字段路径/`（均位于 `Core美化映射分析` 下面）。

r9 只写主槽 ID、递增主槽 `+0x1C` 计数、Vehicle.PendingVehicleSkinID，各 4 字节。当前 / Real / Default 字段只读。15 秒后恢复主槽、再次递增计数并提交原车请求，最多另观察约 3 秒；未消费且原车字段一直没变的恢复请求会清理。游戏改值、会话、组件成员、对象实例或数组变化会停止旧地址覆盖，未恢复的记录阻止新试换。

**这只是复现已观察到的数据请求，不是完整执行 OnRep。主程序静态分析没有证明 Pending 字段单独就会驱动真实刷新消费者；r9 的模拟消费者测试不补足这项实机证据。** 若 Pending 保持目标而 Skin / Real 仍为原值，应继续核对消费入口，不应宣布换肤成功。准备阶段和空计数缓存均有诊断；详见 `docs/cosmetics-r9.md`。

## 12. 下一位 AI 的建议接手顺序

1. 先读 `AGENTS.md`、本文，查看分支与工作区状态。不要重跑本机编译或分析。
2. r8 采集已经成功，不必让用户重复发同一辆车的只读报告；先读 `device-evidence-r8.json`。
3. 读取 r9 `single-vehicle-request-trial` 报告及用户画面观察；先看 apply.prepare_stage，再分开看写入、Pending 消费和原生 Skin / Real 字段。
4. 在云端分析中以实机 Vehicle vtable `0x10EBC1680` 为依据，不能直接套用角色 F50、调用未经确认的函数或修改能力标志。
5. 若 r9 主槽 / Pending 写成 3349002、原生 Skin / Real 却一直为 3349001，不能宣称生效；下一步核对真实消费者。静态主程序未证明 Pending 字段单独可驱动完整刷新，不能盲目重复相同请求或套用衣服的 F50。
6. r9 已写入主槽 ID、主槽计数（递增）和 Vehicle.PendingVehicleSkinID；原生 Skin / Real / Default 只读。保持修改范围，且未恢复的记录会阻止新试换。退出或换车导致归属改变时不能强写旧地址。
7. 既有回归覆盖 r8 来源与位图、r9 部分失败恢复、计数范围、被消费 / 未消费请求、空缓存及游戏接管。测试的消费者是模拟器。新增修改补充具体失败案例，仅在 Actions 执行。
8. 当前只开放 4 座敞篷 3349001→宾利 3349002。未验证这组之前不要开放所有配料目标；跨车型 PAK 替换不证明原生 ID 兼容。

不要再让用户重复用 r6、r7 采集相同失败状态。用户已经提供了足够说明“坐驾驶位仍失败”的证据；新请求反馈应建立在具体修正和可安装的新包上。

## 13. GitHub 构建、测试和分析入口

| 工作流 | 用途与触发 |
|---|---|
| `.github/workflows/build-cosmetics.yml` | feature 分支改 `patch/**`、`scripts/**`、`tests/**`、使用说明或 build 工作流后触发；调用 `build.yml`，传 `cosmetics_probe: true` |
| `.github/workflows/build.yml` | macOS 15 编译、Python / C / ObjC 回归、最终 IPA 和签名核验；默认主分支路径仍是 v19 |
| `.github/workflows/analyze-cosmetics.yml` | 全量游戏 / Core / PAK / 名称 ID 云端分析；生成 JSON、反汇编和目录源码 |
| `.github/workflows/inspect-vehicle-cosmetics.yml` | 只下载固定游戏主程序，按车辆配置做专项静态分析；改 `vehicle_review.py` 或 `vehicle-windows.json` 即可在 feature 分支触发 |
| `.github/workflows/inspect-cosmetics-menu.yml` | 原生 Core UI 专项分析 |

全量分析已排除 `vehicle_review.py` 和 `vehicle-windows.json` 的改动，车辆窗口小改不必重复跑全量 PAK 解析。

云端测试通过 43 项 Python 回归，以及角色读取、同值写、单槽位事务、分页、车辆来源 / 稀疏组件 / 空数组、r9 数据请求事务与恢复、Mach 转发、环境准备等原生测试，以及最终 IPA 的修改边界和签名核验。

这些测试不会模拟手机真实游戏对象、账号资源或游戏画面；r7 的模拟回归通过并没有发现其来源对象选择不完整，实机反馈才暴露了问题。

本机允许使用的典型命令仅为 Git / CI 管理，例如：

```bash
git status --short --branch
gh run list --repo zwthys-cyber/Core-21A327-TrollStore --branch feat/kernel-cosmetics --limit 5
gh run view RUN_ID --repo zwthys-cyber/Core-21A327-TrollStore --json status,conclusion,headSha,url
gh run download RUN_ID --repo zwthys-cyber/Core-21A327-TrollStore --name ARTIFACT_NAME --dir OUTPUT_DIR
```

不要为了新工作流的手动触发入口去改主分支；保留 feature 分支的路径 push 触发即可。工作流中的 `--only-binary=:all:` 等包含冒号的命令使用 YAML 多行 `run: |`，避免语法误判。

## 14. 最近安装包、校验值和报告获取

| 版本 | GitHub run | 代码 / 产物 |
|---|---:|---|
| r9 | `38959705176` | 代码 `feb99d180133d2d0e643b7e5b9b5a8580ffe9682`；artifact `Core-cosmetics-vehicle-r9` |
| r8 | `38935444227` | 代码 `b49e157b7249a9500bbc78626bcd5d1c60108e6c`；artifact `Core-cosmetics-vehicle-r8` |
| r5 | `38922744774` | 代码 `fa2c0634a21bd0dc4bb0cba0a0c2ef2cbddd7811`；artifact `Core-cosmetics-catalog-r5` |
| r6 | `38928395631` | 代码 `277dc7b0463bf9b025edf3f066d202eb7cd5ae4a`；artifact `Core-cosmetics-vehicle-r6` |
| r7 | `38930863902` | 代码 `efecabbd0fdf0acbbe75f93b496bf575ddf3867b`；artifact `Core-cosmetics-vehicle-r7` |

最新 r9（build 20.9，ProbeVersion 9，数据请求实验尚待手机报告）：

```text
/root/和平精英/Core车辆试换-r9/Core-17.0-21A327-cosmetics-vehicle-r9.ipa
SHA256 fdb4d3697e854ab895cb3928310d7b8ab662011ced2a30c7486e81aaf83c9f56
```

历史 r8（build 20.8，ProbeVersion 8，已有 8/8 次成功只读采样）：

```text
/root/和平精英/Core车辆适配-r8/Core-17.0-21A327-cosmetics-vehicle-r8.ipa
SHA256 ff0f95a86854aa815b89dac886916e970cb81fdd096ec03b12b20de65cda6fa0
```

历史 r7：

```text
/root/和平精英/Core车辆适配-r7/Core-17.0-21A327-cosmetics-vehicle-r7.ipa
SHA256 5fb8b11cbb2b52c6b5eed8dc5a8996fe9b3f7fccee6ddfee1fa1cf57cea7a435
```

r6：

```text
/root/和平精英/Core车辆适配-r6/Core-17.0-21A327-cosmetics-vehicle-r6.ipa
SHA256 1bbf2f32ab91355d0c1073c6a66e734fe15e7d03729872088b4543962b6c4d82
```

r5 SHA256：`878acfa7487903c228eff98aa77b8b51165c424d130982c53d290b8749875780`。本地位于 `/root/和平精英/Core分类外观试穿-r5/最终构建/Core-cosmetics-catalog-r5/`。

GitHub 构建链接：

- r9：https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38959705176
- r8：https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38935444227
- r7：https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38930863902
- r6：https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38928395631
- r5：https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38922744774
- 车辆来源类名确认：https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38934153584

Actions 产物保留期为 14 天，安装包已下载到用户目录。日志另存 `/tmp/core-r9-final-build.log`、`/tmp/core-r8-build.log`、`/tmp/core-r7-build.log`、`/tmp/core-r6-final-build.log` 等；临时日志可从 Actions 重新获取。

手机报告位置：

```text
文件 App → 浏览 → 我的 iPhone → Core → Core-cosmetics-probe.json
```

原生菜单：“位置”下面“美化” → “换分类”到“车辆适配” → “采集车辆”。正常设计为取得 8 个成功样本后自动结束；未就绪时最多约 60 秒，也可手动停止。r7 未成功，r8 已取得成功样本。

r9 试换入口在同一页的“宾利试换15秒” / “恢复原车”。试换报告 mode 为 `single-vehicle-request-trial`，schema / trial revision 为 9；只读采集报告为 `vehicle-read-adaptation`，schema / probe revision 为 9。

报告只导出外观标量、状态、相对方法 / vtable 位置和诊断，不导出原始进程地址、PID 或物品实例 ID。新包报告会覆盖同名旧文件，检查 `schema_version` 和 `probe_revision` 避免拿旧报告分析新包。

## 15. 最容易重复的错误

- 把反射表里的字段偏移直接套在错误的类上；函数名不能证明参数就是当前 pawn。
- 跳过原生 getter 中的中间解析对象，或把缓存字段当成始终存在的组件来源。
- 把角色和车辆 SynData 的步长、槽位、刷新逻辑混用。
- 将比较写返回值当位图，误把失败枚举当成功。
- 用服务端同步值被修改 / 回读成功，替代实际画面变化的验证。
- 将原配料的 PAK 跨车型资源替换等同于原生皮肤兼容关系。
- 修改全局能力标志、强制就绪、破坏原 Core 检查来掩盖来源路径错误。
- 为了省事在服务器上执行“只跑一下”的编译、解析或回归。
- 忽略主分支 v19 与美化 feature 分支的不同构建路径。
- 在给用户的新版本说明中写“车辆已修好”，却只做了只读采集或模拟内存测试。

接手时最短摘要：**r8 取车已实机成立，实际走 pawn+0x60C8 控制器回退；r9 已实现 3349001→3349002 的三字段数据请求及 15 秒恢复，等待手机反馈。Pending 请求独立消费与画面刷新未证明；下一步依据 r9 报告和画面确认，不重做取车、不把请求回读当作换肤成功。**
