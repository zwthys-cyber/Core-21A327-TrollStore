# Core / 和平精英美化项目 AI 交接文档

更新时间：2026-10-17 UTC。本文记录用户目标、已完成工作、实机证据、失败经过和接手位置。

**当前结论：金龙外套已实机确认；r8 取车成立；r9 写入成功但 Pending 未消费，用户确认画面“完全没有变化”。r10 的实体诊断实机 8/8 成功，当前加载原车 3349001。用户确认目标宾利预览可以正常显示。`CoreOffline+0x4B0C` 会把路径匹配 `.app/Core` 的可执行文件头写入基址槽，之后 `CoreRemoteOpen` 经 `0x3710` 进入本地打开函数。现有打开调用仍只有 `launchd` 和 `SpringBoard`。r11 只增加内核进程名查找，名称固定为 ShadowTrackerExtra，不调用远程打开，也不改车辆外观。游戏线程仍未建立，车辆换肤尚未完成。**

## 1. 接手必须先知道的约束

1. 用户明确要求：**不要在这台服务器上验证、编译或运行测试，容易卡死。所有编译、自动化测试、签名核验、游戏二进制分析和反汇编都放在 GitHub Actions。**
2. 本机只做文本编辑、Git 操作、读取云端分析报告和 CI 日志、下载产物、轻量版本与摘要查看。不要在本机运行 `scripts/build.py`、`scripts/verify.py`、`analysis/cosmetics/analyze.py`、`vehicle_review.py`、PAK 解析、clang、Unicorn 或 unittest。仓库 `AGENTS.md` 已记录此限制。
3. 用户于 2026-10-16 明确要求将仓库公开以恢复 Actions，已执行并确认 `visibility=PUBLIC`。仓库代码、历史、已发布 Release 及被 Git 跟踪的 `input/Core.ipa` 因此可公开访问。游戏主程序和美化包所在 Release `414807300` 仍保持草稿；不要把该素材草稿发布。账号计费阻碍已解除，专项分析、r9 重新构建及 r10 构建均已成功。
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
- 采集普通车不需要买车皮，也不需要点游戏内换肤按钮。r8 已取得普通车辆的稳定样本；r9 写入已成功，但本次请求未被原生流程消费。
- 用户对 r9 最终画面确认：“完全没有变化，有可能是我的游戏里面的资源包没有下载”。不要把后半句假设当作已证实原因，也不要把前半句改写成画面待确认。
- 随后回答目标宾利游戏内预览能否显示时，用户明确回复“可以正常显示”。缺少预览资源不再是主要假设；这是用户反馈，不能改写成 r10 自动文件检测通过或对局全部依赖完整。

## 3. 工作区、分支和当前代码状态

| 项目 | 位置 / 状态 |
|---|---|
| 工作区 | `/root/Core-21A327-TrollStore` |
| 用户素材和下载产物 | `/root/和平精英` |
| GitHub | `https://github.com/zwthys-cyber/Core-21A327-TrollStore`，用户已明确授权改为公开 |
| 开发分支 | `feat/kernel-cosmetics` |
| 主分支基线 | `e258037c8341f6290859fd310b4f6a09b2fe7ee4`，v19-600m |
| 最新安装包代码 | `061d253cccfed672a4047f5bafd8705ced30917a`，r10 只读车辆实体诊断 |
| 最近车辆试换代码 | `feb99d180133d2d0e643b7e5b9b5a8580ffe9682`，r9 |
| 最近有用户实机报告的代码 | `061d253cccfed672a4047f5bafd8705ced30917a`，r10，实体只读诊断 8/8 成功 |
| r8 | **只读取车路径及当前车辆布局已有实机样本；尚未修改车辆外观** |
| r9 | **实机确认数据写入和未消费请求清理；原生刷新未触发，用户确认画面完全没变** |
| r10 | **CI 和实体只读实机采样成功；当前 LoadedHandle 为原车，目标资源下载状态未检查** |

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

### 4.2 美化 r1～r10

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
| r9 | 固定 3349001→3349002，提交主槽 / 更新计数 / Pending 数据请求，约 15 秒后恢复并记录原生字段 | 三项写入成功，Pending 未消费、缓存计数始终 1；最后还原主槽并清理请求；用户确认画面完全没变 |
| r10 | 从 Avatar 的原生映射读取 Slot 0 实体，核对类和归属，采集 LoadedHandle / 模型记录 / 刷新标记；关闭 r9 新试换入口 | GitHub 构建和检查通过；实机 8 次实体诊断成功，加载的为原车；不检查目标资源文件，没有新增车辆换肤实现 |

实机证据文件：

- `analysis/cosmetics/device-evidence-r1.json`
- `analysis/cosmetics/device-evidence-r2.json`
- `analysis/cosmetics/device-evidence-r3.json`
- `analysis/cosmetics/device-evidence-r4.json`
- `analysis/cosmetics/device-evidence-r8.json`（用户完整重复报告的压缩摘要，8 次稳定车辆采样）
- `analysis/cosmetics/device-evidence-r9.json`（两个车型不匹配及匹配车辆试换；写入成功但原生请求未消费）
- `analysis/cosmetics/device-evidence-r9-followup.json`（2026-10-17 收到的 8 次只读报告；仍是 schema / probe 9，不含 r10 实体诊断）
- `analysis/cosmetics/device-evidence-r10.json`（随后真正的 r10 报告；8 次实体、类 / 归属、LoadedHandle 和模型组件读取成功）

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
- r9 单车三字段比较写、主槽还原及未消费请求清理已有实机报告；用户确认画面完全没变。
- r10 已实现有边界的只读实体 / 加载状态诊断；云端回归通过，8 次实机读取成功。

### 尚未完成

- **车辆换肤尚未完成。r9 实机画面无变化；r10 仅诊断当前实体状态，不开放新的车辆试换。目标宾利预览已获用户确认正常显示；完整原生刷新调用仍未实现。**
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

素材草稿 Release（仓库已公开，草稿仍未发布）：

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
| `patch/CoreCosmeticsVehicleEntity.h` | r10 只读 TMap / 位图 / 哈希链查找、实体与 LoadedHandle 类及归属、模型记录和刷新标记；不接受写接口 |
| `patch/CoreCosmeticsComponents.h` | r8 新增；原生类继承核对、内联 / 堆位图和有界稀疏组件查找 |
| `patch/CoreCosmeticsVehicleTrial.h` | r9 固定车型 / 实例 / 数组门槛、三字段比较写、修改记录和恢复观察；不调用游戏函数 |
| `tests/test_cosmetics_vehicle_entity.c` | r10 实体映射、类 / 归属 / 身份、模型字段边界及并发变化的模拟读取测试，仅在云端执行 |
| `scripts/build.py` | 固定输入 IPA、编译适配库、打包与元数据 |
| `scripts/cosmetics_menu.py` | 在 v19 允许范围内增加原生菜单入口和导出函数桥 |
| `scripts/verify.py` | 最终 IPA 的修改范围、ABI、资源、签名等检查；只在云端执行 |
| `analysis/cosmetics/analyze.py` | 游戏 / Core 固定二进制的反射和反汇编分析 |
| `analysis/cosmetics/vehicle_review.py` | 车辆专项云端分析 |
| `analysis/cosmetics/vehicle-windows.json` | 车辆函数窗口、指针表和字段访问的云端分析配置 |
| `analysis/cosmetics/vehicle-source-evidence.json` | r6 / r7 失败证据与 r8 云端确认的控制器、类型及位图路径；不能当作实机通过证明 |
| `analysis/cosmetics/vehicle-trial-profile.json` | r9 配料对应、主槽类型及数量、原生请求写入证据与未验证边界 |
| `analysis/cosmetics/vehicle-refresh-evidence.json` | r10 实机后核对的原生方法身份、资源回调上下文及不可直接套用的触发器 / 下载接口 |
| `analysis/cosmetics/inspect_menu.py`、`menu-windows.json` | 固定 Core 的菜单 / 远程调用有界窗口、符号和字面量专项云端分析 |
| `analysis/cosmetics/core-execution-evidence.json` | Core 远程调用会话、`CoreRemoteOpen` 入口桥、launchd / SpringBoard 调用点，以及尚未解决的游戏线程边界 |
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

以下结构最初来自静态候选，随后已被 r8 / r9 当前车型的实机报告支持：

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

**车辆组件的 C68 只清除 F50，不执行车辆 OnRep，不能套用衣服的刷新方式。r9 已实现主槽 / 计数 / Pending 写入，但本次实机未触发原生消费。**

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
- `GetVehicleOfCharacter` 的中间对象是 controller，不能继续假设组件来源必定属于 pawn。r8 已据此实现受类型、Core 当前控制器和当前 pawn 约束的来源解析，并由后续手机报告确认。
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

### 11.7 r8 实机采集成功

用户粘贴的报告 `captured_at_unix=1792170414.141237`，schema / probe revision 8，8 次尝试取得 8 个成功样本，`finish_reason=capture-complete`。八次数据一致，压缩摘要保存在 `analysis/cosmetics/device-evidence-r8.json`。

- 当前、前一、Real 和 Default 皮肤均为 `3349001`，Pending 为 0。
- 同步数组有 11 槽位；主槽 0：type 33、ID 3349001、native update counter 1；其他十槽均为零。
- 组件集合 39 个槽位、39 个占用项，数组容量 47；内联位图容量 128。
- `pawn+0x608` 为零，**实际成功的是 `pawn+0x60C8` 回退路径**，且等于 Core 当前控制器。不要在重构时删除此回退。
- source vtable RVA `0xE7BBC38`，VehicleUser `0xE7C5F90`，Vehicle actor `0xEBC1680`，Avatar `0xE6D6A38`。
- Avatar OnRep `0x47EE7C8`、Tick `0x42E4F28` 与静态分析一致；组件归属、Character 与当前 pawn、数组稳定性检查均通过。

报告中的 `runtime_layout_validated=false` 是 r8 固定写入的适配阶段标记，不否定这八次真实读取结果；但也不能凭当前样本宣布所有车型、刷新及写入均已验证。衣服的 F50 刷新机制仍不能直接套到车辆。

### 11.8 r9 单车请求试换：历史产物

代码 `feb99d1`，构建 `38959705176` 成功。用户已给出匹配车辆的 r9 手机报告，见下节。仅接受实机已读到的车辆 / Avatar vtable、type 33 和 11 个同步槽位，固定 `3349001→3349002`。原始配料对应来自新 ZIP 的衣服枪械配料表第 542 行。

原生 OnRep 在 `0x47EE9A8..0x47EE9B0` 读取主槽 ID 并写入 `Vehicle+0x20AC`；`0x47EEA10..0x47EEA64` 比较主槽计数与 `Avatar+0x1208` 缓存。专项云端分析任务为 `38937613465`、`38937875743`、`38957112182`，本地文本报告分别在 `r8车辆刷新路径/`、`r8车辆字段消费路径/`、`r8车辆寄存器字段路径/`（均位于 `Core美化映射分析` 下面）。

r9 只写主槽 ID、递增主槽 `+0x1C` 计数、Vehicle.PendingVehicleSkinID，各 4 字节。当前 / Real / Default 字段只读。15 秒后恢复主槽、再次递增计数并提交原车请求，最多另观察约 3 秒；未消费且原车字段一直没变的恢复请求会清理。游戏改值、会话、组件成员、对象实例或数组变化会停止旧地址覆盖，未恢复的记录阻止新试换。

**这只是复现已观察到的数据请求，不是完整执行 OnRep。主程序静态分析没有证明 Pending 字段单独就会驱动真实刷新消费者；r9 的模拟消费者测试不补足这项实机证据。** 若 Pending 保持目标而 Skin / Real 仍为原值，应继续核对消费入口，不应宣布换肤成功。准备阶段和空计数缓存均有诊断；详见 `docs/cosmetics-r9.md`。

### 11.9 r9 实机：数据写入成功，游戏未消费

用户报告时间 `1792182343.3586221`。前两次车型分别为 `3336001`（两座 Coupe RB）和 `3306001`（蓝色 Dacia），均被车型门槛拒绝。第三次正确读到 `3349001`，准备完成，三项写入共 12 字节，原接口状态 1。随后 5 次观察均为主槽 `3349002`、计数 2、Pending `3349002`，但缓存计数始终 1、Current / Real / Previous / Default 始终 `3349001`。这证明当前请求没有触发已观察到的原生消费路径。

恢复请求也是 12 字节；7 次观察中主槽为 `3349001`、计数 3、Pending `3349001`、缓存仍为 1。最后清理本次未消费 Pending（4 字节），`restore_check.memory_restored=true`、`change_still_owned=false`、`native_restore_observed=false`。这里“恢复”指主槽 / 请求数据，计数按照设计只递增，不回退；不能表述成车皮先成功切换再可见恢复。

证据摘要：`analysis/cosmetics/device-evidence-r9.json`。不要让用户再次找同款车重复相同试换来代替代码修正；下一步在云端查车辆 Handle / 原生刷新调度。

用户后来明确补充画面“完全没有变化”。因此 `visual_effect_verified=false` 应理解为未生效的实机反馈，不能继续写成尚未询问用户。用户提出可能未下载资源包；没有资源检测证据支持或排除该假设。

### 11.10 云端分析曾被账号计费阻止，已按用户指示公开仓库并重跑

提交 `5c3c868c3b8e70d9cb08ad6d9d9dfa62a21d9baa` 已记录 r9 证据，并在 `vehicle-windows.json` 增加主 Handle getter、按槽位 Handle getter、VehicleUseDoRepOnce 原生判断、基类组件 Tick 和车辆 OnRep 初始化 / 尾部六组窗口。

专项分析 run `38962343474` / job `117180265753` 没有启动任何步骤，GitHub annotation 原文：`The job was not started because recent account payments have failed or your spending limit needs to be increased. Please check the 'Billing & plans' section in your settings`。同次触发的全量分析 `38962343486` 也未成功。本次没有新反汇编、测试或安装包。

用户随后明确要求“把仓库设置为公开”，已执行并通过 GitHub API 确认 PUBLIC，素材 Release 仍为草稿。已重跑 `38962343474`，第二次执行成功，计费阻碍已解除。随后分析 `38962846126`（代码 `39644e3`）和 `38963087229`（代码 `1d97aaa`）也已成功。无需用户再次处理额度，不要转到本机分析。

### 11.11 公开仓库后恢复构建，继续沿原生实体路径调查

r9 构建 `38959705176` 第二次执行成功（2026-10-16 20:34 UTC），同一代码 `feb99d1` 的编译、43 项 Python 回归、原生测试、修改边界及签名检查通过。此为构建环境恢复确认，不是车辆功能新版本；不要求用户重装或重复试车。

重新构建产物单独保存，未覆盖先前已交付 IPA：

```text
/root/和平精英/Core车辆试换-r9/公开仓库重编译-20261016/Core-17.0-21A327-cosmetics-vehicle-r9.ipa
SHA256 8c69736d6de541170a1f02db2e68074e083086984f8ed6ecf7af851271cbd132
```

新增分析工具支持有界 ASCII 名称发现（最多 256 个名称，仅云端执行），随后增加 UTF-16 类名称与有界 ADRP / ADD 引用定位。早期报告目录均位于 `/root/和平精英/Core美化映射分析/`：`r9原生刷新调度/`、`r9外观Handle索引/`、`r9车辆实体与完成回调/`；随后沿实体类和字段继续分析，见下节。

新的静态调用链：`GetMasterVehicleAvatarHandle` 包装 `0x723A754` → `0x47F5F9C`（slot 0）→ `0x47FAD00` → `0x42DD238` → `0x3796F3C`（使用 Avatar `+0x8B8` 的映射，条目步长 0x18，值位于条目 +8）。**后续类注册和属性元数据纠正了早期解释：映射值本身就是 AvatarEntity UObject，`0x42D9824` 读取该实体 `+0x30` 得到 LoadedHandle，并不是另一层实体。** 不要继续使用旧交接文档的“非 UObject 节点 → +0x30 实体”解释。r10 已实现有界映射和类归属读取，并取得 8 次一致的实机结果。

`VehicleUseDoRepOnce` 原生 `0x5AAC7F4` 是读取全局控制台配置的判断，不是每车可写刷新标记。反射名称 `OnVehicleAvatarChange` 中的一组包装 `0x726ED70` 调用 `0x5BD7FB4`，后者仅 RET，不能仅凭名称当作刷新实现。车辆 Actor vtable 的 `+0x600` 指向 `0x5A40BDC`（RET），不能将它与紧邻的 `0x5A40BE0` 函数体混为同一个入口；`+0x4C0` 的 `0xA14443C` 也没有证据可当作车辆专属 Tick。下一步应核对实体实际的类、处理函数和刷新调度；现阶段不增加未经证明的写入。

重新构建的 r9 `build-metadata.json` 仍继承旧通用键 `cosmetics_vehicle_writes_implemented=false`，与已实现且实机确认的 r9 事务写入不一致。r10 已修正构建元数据：包含旧车辆写入代码为 true，车辆试换可用为 false，车辆采集只读为 true；手机只读采集报告的 `vehicle_writes_implemented=false` 描述此次采集，不表示旧事务代码被删除。

### 11.12 r10 的静态来源和只读实体诊断

专项分析均在 GitHub Actions，下载到 `Core美化映射分析` 下的文字 / JSON 目录：

| Run | 目录 | 结果 |
|---|---|---|
| `38963556220` | `r10实体类与映射查找/` | 类名引用与映射调用链 |
| `38963790359` | `r10实体构造与字段/` | 类注册、构造和反射字段 |
| `38963996828` | `r10实体Tick和资源字段/` | 实体虚表和模型属性；目录名称不证明其中函数就是 Tick |
| `38964451486` | `r10渲染标记与物品类型/` | 渲染标记及 LoadedHandle 类型来源 |
| `38964761443` | `r10已加载物品原生类型/` | 已加载物品类缓存与类型构造 |

以下记录固定版本静态来源；其中 r10 实际读取的映射、类型、归属、当前句柄和方法 RVA 已得到 8 次手机样本支持，未读取的其他字段和完整刷新逻辑仍不能宣称实机验证：

- AvatarEntity 的类 getter `0x611795C`、类缓存 `0x11ABCB40`，大小 `0x3E8`；VehicleAvatarEntity getter `0x7240118`、缓存 `0x11CBB860`，大小 `0x3F0`；MasterVehicleAvatarEntity getter `0x68AE040`、缓存 `0x11BA1780`，大小 `0x4A0`，构造写入虚表 `0xE67A9B8`。
- LoadedHandle 属性的类型构造 `0x74BB1DC→0x74BB434`，类缓存 `0x11CEAAC0`、大小 `0x1D8`。r10 只在类继承确认后读取 key `+0xA8`：type `+0xA8`、物品 ID `+0xAC`；不根据指针非零猜物品类型。
- Avatar `+0x8B8` 的 TMap 头长 `0x50`，数据 / count / capacity 在 `0 / 8 / C`，占用位内联 `+0x10` 或堆指针 `+0x20`，bit count / capacity 在 `+0x28 / +0x2C`，free count `+0x34`，哈希桶内联 `+0x38` 或堆指针 `+0x40`，hash size `+0x48`。条目 stride `0x18`，key `0`，实体指针 `8`，下一哈希索引 `0x10`。
- r10 同时检查占用位线性唯一查找和 slot 0 原生桶链，二者必须命中同一条目；限制槽位 128、容量 256、位图容量 4096、桶 512，检测环、越界、重复 key、已删除槽位和读取变化。
- 实体 slot / subslot `+0x28 / +0x2C`；LoadedHandle / CachedLoadedHandle / DelayRenderCachedLoadedHandle `+0x30 / +0x38 / +0x40`；AvatarComponent `+0x48` 必须等于采样 Avatar；MeshComponent `+0x50`。核对 UObject 身份及 AvatarEntity / VehicleAvatarEntity 继承，Master 类型单独报告。
- DelayRenderDirtyFlag / RenderDirtyFlag / 性别刷新字节 `+0x1B8 / +0x1B9 / +0x1BA`；MeshAssetPath `+0x258`、LastUseMeshPath `+0x3B8` 均为 `FSoftObjectPath`，只报告记录是否存在，不导出名称。Avatar `+0x1208` 缓存计数为空时允许采样，并明确标记未读取。
- 实体虚表 `+0x3C0→0x45DABBC` 接收已加载句柄，写实体 `+0x30`、槽位并复制 key；`+0x478→0x45E012C` 涉及材质，`+0x4E0→0x42F1AF4` 涉及车型 / 行为。它们不是已证明可独立调用的完整换肤入口；`+0x3A0` / `+0x3A8` 均返回 false，不能仅凭位置称为 Tick。
- `CurVehicleSkinID +0xD08` 所属元数据是车辆 UI 类；`OwnerVehicleAvatarComp +0x7C0` 属于另一动画 / 移动组件。不能把它们套到当前实体。

r10 的 `CCPReadVehicleEntity` 不接受写入回调，不调用游戏函数、不写渲染标志、不修改共享 LoadedHandle。路径、位图、哈希桶、UObject 身份、句柄 key、字段均重复读取；未确认原因保留在 `refresh_diagnostics.status`，不把可选诊断失败改报成“没有上车”。导出相对 vtable / 方法位置，不导出堆地址。

报告 mode 为 `vehicle-entity-resource-diagnostics`，schema / probe 为 10。`refresh_diagnostics.target_resource_availability=not-checked`，仅描述当前实体加载状态，不扫描游戏磁盘，不主动加载目标宾利，也不保证路径记录对应的资源可用。用户怀疑未下载资源，仍需实际资源就绪证据；不要把 r10 空句柄 / 路径直接解释为缺包。

r10 控制器拒绝新的车辆试换请求，菜单移除“宾利试换15秒”，保留采集 / 停止及已有修改记录的恢复处理；服装试穿不变。完整代码 `061d253`，构建 `38965497830` 成功，43 项 Python 回归、原生实体及既有测试、最终 IPA 修改边界和签名检查均通过。**r10 已取得 8 次实机实体诊断，见 11.14 节；刷新调度和车辆换肤仍未修复。**

### 11.13 2026-10-17 后续报告仍为 r9

用户在 r10 交付后发来一份 `captured_at_unix=1792205171.538888` 的只读报告：schema / probe 都是 9，mode 为 `vehicle-read-adaptation`，`vehicle_trial_available=true`，没有 `refresh_diagnostics`。8 次采样均成功，原车 / Real / Previous / Default 为 `3349001`、主槽计数 3、Pending 为 0，控制器回退与组件归属仍匹配。这份报告支持原车读取和当前无待处理请求；没有新增实体或目标资源证据。

源码中 r10 的车辆采集在开始时固定写 schema / probe 10、mode `vehicle-entity-resource-diagnostics` 和 `vehicle_trial_available=false`；资源缺失不会使它回退成 r9 报告。交付 IPA 的轻量 SHA 查看仍与第 14 节一致；没有为此重新编译或在本机验证。

目前不能断定用户安装错包。可能取到旧导出文件、仍有旧 Core 进程运行，或使用了另一安装包。下一步关闭悬浮并彻底退出 Core，用第 14 节 r10 IPA 覆盖安装后重新启动和初始化，再采集并从原 Core 文件夹重新导出；先确认 schema / probe 10 和 `refresh_diagnostics`，无需重复 r9 宾利试换。新报告前不要把本次数据标为 r10 实机结果。

### 11.14 随后的真正 r10 报告：实体只读诊断成功

用户报告 `captured_at_unix=1792205523.253108`，schema / probe 10、mode `vehicle-entity-resource-diagnostics`，试换不可用。8 次尝试均得到稳定的 `vehicle-entity-state-sampled`。摘要保存在 `device-evidence-r10.json`，无需再让用户重复相同采集。

- Slot 0 唯一实体，TMap 只有 1 个占用项，内联位图、原生哈希链一致；AvatarEntity / VehicleAvatarEntity / MasterVehicleAvatarEntity 类与 Avatar 归属都匹配。
- 实体 vtable `0xE67A9B8`，install / material / behavior RVA 分别为 `0x45DABBC / 0x45E012C / 0x42F1AF4`，与静态来源一致。
- LoadedHandle 存在、类型和 key 可读，type 33、ID `3349001`；MeshComponent 存在；cached / delayed handle 为空。subslot 报告值 `4294967295` 是原始 uint32 标量，不据此判错。
- 原车 / Real / Previous / Default 均为 `3349001`，Pending 0，native counter 与 cache counter 均为 1，cache 数组 11 项。render / delay / gender dirty 都为 0。
- 两个 SoftObjectPath 记录为空。当前实体有加载句柄和 MeshComponent，所以空路径本身不能证明资源未下载，更不能推断目标宾利资源状态。报告明确 `target_resource_availability=not-checked`。

这是正常原车状态的只读采集，没有新试换或目标加载请求。不能把“LoadedHandle 仍是原车”称为 r10 写入失败，也不能把本次计数与旧 r9 的不同实例直接比较。全局 `runtime_layout_validated=false` 是适配阶段的固定字段，不否定这组范围明确的实机读取。

### 11.15 r10 实机后的云端方法核对：找到处理函数仍不等于实现调用

云端任务 `38996884265`（代码 `3d9059c`）、`38997001715`（`f7a29f5`）、`38997119495`（`ea2c892`）均成功。文字 / JSON 报告分别在 `Core美化映射分析/r10实机后完整渲染流程/`、`r10实机后车辆资源调度/`、`r10实机后资源下载与触发器/`。本轮没有改运行时代码、没有新 IPA，也没有本机编译 / 分析。

分析工具增加有界原生注册表读取。`0x8CD5A0C` 的真实迭代指令以 `0x18` 递增，记录由 name / function / 额外注册字组成。AvatarEntity 表 `0xEDCE658` 的 71 项、VehicleAvatarComponent 表 `0xFC51D00` 的 97 项名称与可执行函数地址全部确认；早期按 16 字节猜测的分析布局已纠正，不要据旧未确认行判断函数不存在。

| 原生名称 | 实体 vtable slot | 当前固定实现 RVA | 证据范围 |
|---|---:|---:|---|
| PutOnEquipmentLogic | `0x3C0` | `0x45DABBC` | 接收已加载物品句柄并完成关联设置；不是只接收整数 ID 的函数 |
| RenderEntity | `0x478` | `0x45E012C` | 接收资源路径 / 引用上下文并进入实体渲染 |
| RefreshEntityRender | `0x480` | `0x42F1680` | 空路径时取实体路径，再派发 RenderEntity；不证明会按新 ID 创建句柄 |
| CreateAndApplyResource | `0x488` | `0x45E0194` | 要求所属组件和已加载句柄，经车辆组件的资源 / 网格方法取得引用，更新实体 MeshComponent |
| EnterRenderPipeline | `0x498` | `0x45DD5C8` | 继续执行车辆材质等处理，不是已证明足够的独立目标加载入口 |

`OnItemHandleListLoaded` 的脚本包装 `0x723BB14` 调用 `0x47EB43C`，输入为句柄数组引用和请求代号；后者与组件 `+0xB68` 的请求代号比较。完整 OnRep 还会收集 Definition key、推进请求代号、提交 / 完成加载，再走后续安装。r9 只改主槽 / 计数 / Pending 没有执行这些流程。包装函数接收 Unreal 脚本帧，不能把包装地址当成普通 C 函数直接调用。

新增防误用结论：

- `PutOnEquipmentByResID`：车辆组件 `+0x968→0x47F2824`，先把 ID 转为 Definition 和句柄，再按槽派发；可作为后续研究完整原生调用的具体入口，当前适配库没有已验证的游戏线程调用能力。
- `SetVehicleSkinTrigger` 包装 `0x723CCBC` 接收对象参数，原生 `0x47F3104` 把 `Avatar+0x10C0` 交给 helper `0x8DFDDAC`；不能凭 Trigger 名称把字段当成可写布尔刷新标记。
- `NetSyncData +0xAC0→0x47EE508` 起始即核对 owner role == 3；不能当作普通客户端本地刷新 API。
- `GetAvatarDownloadManager +0xC30→0x47F4C10` 是带模式 / 配置条件的 getter，不是目标 ID 文件是否存在的查询。
- `TryDownloadNotExistAvatar +0x820→0x47F52EC` 有请求 / 下载流程，不能当成只读资源缺失判断。
- `0x45D94A0` 一带读取的 `+0x1B8` 属于另一组件，其构造虚表为 `0xE678C88`。偏移数值碰巧与实体刷新标记相同，不能套到 MasterVehicleAvatarEntity `0xE67A9B8`。

可复核摘要为 `vehicle-refresh-evidence.json`。当前已确认实体身份与具体原生方法，尚未实现可靠的原生加载 / 刷新触发。不要修改共享 LoadedHandle 的 key，也不要盲置 RenderDirty 后把计数变化当作加载成功。用户随后回复目标宾利预览“可以正常显示”，见下节；不能据此断言对局全部资源依赖完整。

### 11.16 目标预览正常，继续核对 Core 原生调用会话

用户在询问目标宾利预览后明确回复“可以正常显示”。该观察已单独记录于 `device-evidence-r10.json`，原手机 JSON 的 `target_resource_availability=not-checked` 保留不变。预览能加载模型，因此继续把主要精力放在 r9 未执行的原生资源请求 / 安装 / 刷新链，无需再让用户重复同样的实体采集。

本轮仅修改云端分析工具、配置、证据和文档。Core 专项任务 `38997584258`、`38997728564`、`38997837603`、`38998163729`、`38998232648` 均成功；最后分析代码 `d9196db`。最新文字 / JSON 报告在 `/root/和平精英/Core美化映射分析/Core远程会话归属与初始化/`，摘要 `analysis/cosmetics/core-execution-evidence.json`。没有新的运行时代码、IPA 或实机调用结果。

已确认的调用结构：

- Core `0x38D80` 只返回 `Core+0xC53460` 的远程目标 PID；低层函数调用使用这组全局会话，不接收本次美化采样的 PID。它与已有内存读写绑定 `Core+0xC288B0` 分开。
- `0x398C8` 按符号名解析后调度远程调用；`malloc / free / sel_registerName / objc_getClass / objc_msgSend` 字面量已核对。`0x39A80` 接收函数地址、最多八个寄存器参数字和两个输出指针，包含临时 / 稳定异常派发及超时结果；静态调用者只有返回枚举恰为 1 才认成功。尚未在适配库中声明或调用这个 ABI。
- `0x3CB70` 的 `invoke` 属于 `NSInvocation`，会设置 target / selector / 参数，并在条件满足时使用 `performSelectorOnMainThread:withObject:waitUntilDone:`；这不证明能把 Unreal 换肤函数放到正确的游戏线程。
- `0x3D624` 是经 `objc_msgSend` 解析后的检查包装，继续调用 `0x39A80`。不能把所有这些接口都称为游戏调用入口。
- 远程初始化候选 `0x3A7D4` 先跳 `0x3800`，函数体从 `0x3A7D8` 延续。入口桥仍待核对；初始化会检查旧会话 / 隔离状态，创建挂起 pthread 及异常状态，并在远端 `getpid` 对应后才发布目标 PID。不要直接跳过入口桥调用正文或覆盖全局目标。
- 历史手机上成功的远程打开对象是 SpringBoard，后续 launchd 有超时 / 隔离。这些历史结果不等于已建立游戏调用会话。`Core+0xC1D2E8 / 0xC1D2F8` 的致命 / 隔离状态、现有宿主会话及恢复逻辑均未改动。

工具没有从固定 Core 输入解出函数起始表。窗口开头可能在函数中间，不能当成完整 ABI；`remote_call` 字符串附近的原始数据指针、长度及认证字也不是已解码函数表。仅发现一个远程函数接口，仍不足以发一个声称已修复车辆的新包。入口桥和调用者的后续核对见 11.17 节。

### 11.17 初始化入口实际跳到 CoreRemoteOpen，本地函数体不在调用路径上

专项任务 `38998851479`、`38999428964`、`38999489946` 均成功，最后分析代码 `b25d70d`。只改了云端分析工具、窗口和本证据；没有新的运行时代码或 IPA。`__text` 从 `0x100004000` 开始，长度 `0x71ff50`。

`0x3A7D4` 只有一条指令，跳到 `0x100003800`。该地址位于 `__TEXT` 段，但不属于任何节，也不在 `__text` 内。跳向它的指令只有这一条。没有任何指令直接跳向 `0x3A7D8` 的本地函数体。

`0x3800` 起的桥已完整读出：

- 保存 `x0`–`x9` 和返回地址。
- 用原 `dlsym` stub `0x725A10` 调用 `dlsym(RTLD_DEFAULT, "CoreRemoteOpen")`。`RTLD_DEFAULT` 的传值是 `-2`，符号文本位于桥后的 `0x100003864`。
- 找到符号时去掉 PAC 后 `br x16`，原始参数保持不变。
- 找不到时执行 `mov x0, #0; ret`，不进入 `0x3A7D8`。

固定 Core 的符号表和本仓库源码都没有 `CoreRemoteOpen`。因此 `0x3A7D8` 里先前看到的会话、挂起线程和 `getpid` 检查，不是这两处调用的实际执行路径。桥失败时返回 0；本地函数体成功时也返回 0。调用方看到的 0 不能证明本地初始化已经跑过。

`__text` 里只有两处直接调用 `0x3A7D4`，第二参数都是 0：

| 调用点 | 所在函数 | 传入的进程名 | 门闩和清理 |
|---|---|---|---|
| `0x2E7D4` | `0x2E76C` | `launchd`，字面量 `0x73CC28` | `Core+0xC1C258` 或 `+0xC1C259` 已置位时直接返回 1。否则要求 `0x2DCC0` 非零，清掉这两个字节和 `+0xC1C25A` 后再调用。返回非零和返回零两条路都会调用 teardown `0x39F00`。唯一直接进入 `0x2E76C` 的是 `0x4E420`：`0x4E3F8` 只有在 `0x4E378` 非零时才尾调用它。`0x4E378` 要求 `Core+0xC27B2C` 的字节为 1 且 `+0xC27B28` 的字为 2，否则返回 0；条件满足时尾调用 `0x2DCC0`。 |
| `0x563A0` | `0x5625C` | `SpringBoard`，字面量 `0x7401BF` | 三个对象字段两两不同且第三个非零才调用。`__text` 内没有对 `0x5625C` 的直接分支。返回零的后续在 `0x56648` 调用 teardown。返回非零时调用 `0x38DF0`；致命字节 `0xC1D2E8` 或隔离字 `0xC1D2F8` 已置位时该助手返回 1。 |

两个进程名都是以 NUL 结尾的完整字符串，不是更长字符串的后缀。Core 的字符串表里没有 `ShadowTrackerExtra`、`backboardd` 或 `com.tencent.tmgp.pubgmhd`。

`thread_set_state` 写入函数 `0x3B7F4` 的直接调用位于 `0x38CCC`、`0x390CC`、`0x390F0`，都在既有远程调用实现内部，使用 flavor 6、计数 `0x44`。这是 Core 自己的远程线程寄存器写入，不是已确认的 Unreal 游戏线程调度。

下一步先找 `CoreRemoteOpen` 由哪个已加载映像提供，以及符号缺失时这两个调用方的零返回实际做了什么。不要跳过入口桥去调用 `0x3A7D8`，不要把返回 0 写成初始化成功，也不要改隔离或能力标志。游戏线程仍未建立。最新可安装包仍为 r10。该符号的提供者和零返回后续见 11.18 节。

### 11.18 CoreRemoteOpen 在 CoreOffline 中，零返回不会停住调用方

专项任务 `38999815492`、`38999900378`、`38999962851` 均成功，最后分析代码 `07eb9fa`。没有新的运行时代码或 IPA。

固定 IPA 里只有两个映像包含 `CoreRemoteOpen` 这串字节：主程序里的入口桥字面量，以及 `Payload/Core.app/CoreOffline.work.dylib`。`CCCoreLicense.dylib` 没有这串字节。主程序的导出 trie 长度为 0，不导出这个符号。Core 的加载命令已经通过 `@executable_path/CoreOffline.work.dylib` 链接该映像。

`CoreOffline` 的外部符号 `_CoreRemoteOpen` 位于它自己的 `0x4894`。函数先记录 `remote.open enter name=%s options=%llu`，名字为空时用 `?`。两个现有调用传入的第二参数都是 0，对应这里的 options。随后它取出 `CoreOffline+0x102F0` 的 8 字节，加上 `0x3710`，做认证调用，参数仍是原来的名字和 options。这个 8 字节在文件里是 0，所以它是运行时修复槽，还不能把 `0x3710` 写成主程序里的地址。调用返回后，它用同一个基址读取 `+0xC1D2F8` 和 `+0xC1D300`，记下隔离字和旁边的指针，然后把认证调用的返回值原样返回。

因此入口桥在这个 IPA 里会找到 `CoreOffline` 的实现，而不是执行主程序 `0x3A7D8` 的本地函数体。适配库源码没有定义 `CoreRemoteOpen`。

零返回时两个调用方都继续往下走：

- `launchd` 包装调用 `0x2E140`。该函数使用 `0x2DCC0` 已经检查过的 `Core+0xC52E00` 和 `+0xC52E04` 端口，完成后仍然调用 teardown `0x39F00`。`0x2DCC0` 在 `+0xC1C258` 或 `+0xC1C259` 已置位、致命或隔离状态被置位、或这两个端口字不是都大于等于 1 时返回 0；否则再验证 `+0xC52E08`、`+0xC52E10`、`+0xC52E18` 三个指针。
- `SpringBoard` 函数不在这里返回。`0x3C61C` 把 `Core+0xBD4C60` 换成 `0x1388`，然后继续原有远程调用，最后在 `0x56648` 拆会话并恢复原来的计时字。

`0x3600` 的 `dlsym` 桥查找 `CoreOfflineFinalize`。游戏进程名仍然不存在。游戏线程仍未建立。基址槽的去向见 11.19 节。

### 11.19 基址槽加上 0x3710 后进入本地打开函数

专项任务 `39000314518`、`39000384296` 成功，最后分析代码 `0a7c2e0`。没有新的运行时代码或 IPA。

`CoreOffline+0x102F0` 在文件中的 8 字节是 0。`__DATA` 没有链式修复段信息，这个槽不在链式修复里，所以它不是一个已解码的导入绑定。`CoreOffline` 里审查到的 ADRP 引用都只读取这个槽，没有发现对应的立即数存储。同映像 `0x47D0` 在槽为空时返回 0；`CoreRemoteOpen` 自己没有这道空检查。

同一个指针随后被加上 `0xC1D2F8` 去读取隔离状态，也被加上 `0x6CDFC`、`0x1A8C08`、`0xC1D2B8` 等主程序偏移。这些偏移只在主程序布局里同时成立，因此槽里运行时要放入的是主程序基址。

主程序 `0x3710` 的两条指令是 `pacibsp` 和跳向 `0x3A7D8`。`0x3720` 则跳向 `0x39848`，供 `CoreRemoteFault` 使用。所以基址一旦发布，`CoreRemoteOpen` 会进入先前被入口桥跳过的本地打开函数，把该函数的返回值记为 `remote.open return=%llx state=%u reason=%s` 后原样返回。本地函数成功时返回 0，调用方随即继续原有远程流程并拆会话。基址尚未发布时，不能把这个 0 当成打开成功。

发布该基址的函数见 11.20 节。不要从适配库跳过入口桥直接调用 `0x3A7D8`。现有直接调用仍然只有 `launchd` 和 `SpringBoard`。游戏线程仍未建立。最新可安装包仍为 r10。

### 11.20 基址槽由 CoreOffline 的映像遍历写入

专项任务 `39000616091`、`39000698561`、`39000750503` 成功，最后分析代码 `e57bd4b`。没有新的运行时代码或 IPA。

`CoreOffline+0x4B0C` 在 `0x4BE0` 执行 `str`，把选中的映像头写入 `+0x102F0`。选择条件是：

- 通过索引导出映像头和名称。
- 映像头 `+0xC` 的类型等于 2，也就是 `MH_EXECUTE`。
- 名称指针不是空。
- 名称与字面量 `.app/Core` 的比较结果不是 0 时才保存。这个字面量是路径后缀，因此保留的是 Core 自己的可执行文件，不是游戏进程。

`CoreOffline` 的 `__text` 里没有直接跳到 `0x4B0C` 的指令，也没有 `__mod_init_func` 节。触发方式仍是间接的。槽在这次遍历之前保持 0。

这解释了基址的来源，没有增加游戏进程打开，也没有游戏线程调度。

### 11.21 r11 只探测进程名，不打开会话

代码 `b3e728f` 起，车辆适配页增加“探测游戏进程”。它在 Core 身份和原快照入口匹配后，调用 `0x35F08`，名称固定为 `ShadowTrackerExtra`。这个函数按内核进程表做最多 32 字节的名称比较；返回 `-1` 或 `0` 都算没看见，其他返回值只记为 `located`，不导出地址。

它不调用 `0x3A7D4` / `CoreRemoteOpen`，因此不会走会写入隔离字的失败路径。报告 schema / probe 为 12，mode 为 `process-name-visibility`。Core 字符串表里没有 `GameThread`、`com.apple.main-thread` 或 `pthread_getname_np`。车辆换肤仍未完成。

### 11.22 用户这次采集没有进入车辆，游戏映像身份未确认

用户报告 `captured_at_unix=1792210479.7338228`，schema / probe 仍是 10，mode 为 `vehicle-entity-resource-diagnostics`。39 次尝试、手动停止，样本为空，最近 12 次都是 `game-identity-unconfirmed`。这是车辆采集，不是 r11 的进程名探测。失败发生在读取车辆之前：Core 缓存的游戏基址没有被确认为固定的 1.38.12 映像。不能解释成没上车。

r12 把这个总状态拆成指针不可用、映像头读不到、不是 Mach-O、UUID 不匹配、UUID 读不到、加载命令未确认。连续第二次仍是这类失败就停止，finish_reason 为 `game-image-unconfirmed`。摘要在 `device-evidence-r12-identity.json`。最新安装包以 r12 构建为准。

## 12. 下一位 AI 的建议接手顺序

1. 先读 `AGENTS.md`、本文，查看分支与工作区状态。不要重跑本机编译或分析。
2. 先读 `device-evidence-r8.json` 和 `device-evidence-r9.json`：取车已成功；r9 写入成功，但 Pending 未消费、缓存计数一直 1，用户确认画面没变。不要再重复 r6～r9 的同一失败流程。
3. 最新包为 r10，已取得 schema / probe 10 的 8 次实体诊断，先读 `device-evidence-r10.json`。沿已确认的实体继续研究完整原生加载 / 安装 / 刷新调度，无需重复同样采集。
4. 在云端分析中以实机 Vehicle vtable `0x10EBC1680` 为依据，不能直接套用角色 F50、调用未经确认的函数或修改能力标志。
5. 若 r9 主槽 / Pending 写成 3349002、原生 Skin / Real 却一直为 3349001，不能宣称生效；下一步核对真实消费者。静态主程序未证明 Pending 字段单独可驱动完整刷新，不能盲目重复相同请求或套用衣服的 F50。
6. r9 已写入主槽 ID、主槽计数（递增）和 Vehicle.PendingVehicleSkinID；原生 Skin / Real / Default 只读。保持修改范围，且未恢复的记录会阻止新试换。退出或换车导致归属改变时不能强写旧地址。
7. 既有回归覆盖 r8 来源与位图、r9 部分失败恢复及模拟消费者、r10 映射 / 原生哈希 / 类型归属 / 句柄 / 模型字段和变化。测试不能证明目标资源存在或原生刷新已执行；新增修改仅在 Actions 验证。
8. r9 曾仅开放 4 座敞篷 3349001→宾利 3349002；r10 暂不开放新的车辆写入。未验证这组之前不要开放所有配料目标；跨车型 PAK 替换不证明原生 ID 兼容。
9. 用户已确认目标宾利预览正常；r10 自动检测字段仍为 not-checked。沿类与归属正确的实体分析，不要盲写 RenderDirty、改共享句柄物品 ID，或要求下载所有资源后才继续调查。
10. 读 `core-execution-evidence.json` 和 11.20 节。`CoreOffline+0x4B0C` 把路径匹配 `.app/Core` 的可执行文件头写入基址槽，`CoreRemoteOpen` 再经 `0x3710` 进入 `0x3A7D8`。现有调用只传 `launchd` 或 `SpringBoard`，拿到 0 会继续并拆会话。不要跳过入口桥、强清隔离，或把内存读写 ready 当成执行 ready。游戏进程打开和游戏线程都还没有。最新可安装包仍为 r10。

不要再让用户重复用 r6、r7 采集相同失败状态。用户已经提供了足够说明“坐驾驶位仍失败”的证据；新请求反馈应建立在具体修正和可安装的新包上。

## 13. GitHub 构建、测试和分析入口

| 工作流 | 用途与触发 |
|---|---|
| `.github/workflows/build-cosmetics.yml` | feature 分支改 `patch/**`、`scripts/**`、`tests/**`、使用说明或 build 工作流后触发；调用 `build.yml`，传 `cosmetics_probe: true` |
| `.github/workflows/build.yml` | macOS 15 编译、Python / C / ObjC 回归、最终 IPA 和签名核验；默认主分支路径仍是 v19 |
| `.github/workflows/analyze-cosmetics.yml` | 全量游戏 / Core / PAK / 名称 ID 云端分析；生成 JSON、反汇编和目录源码 |
| `.github/workflows/inspect-vehicle-cosmetics.yml` | 只下载固定游戏主程序，按车辆配置做专项静态分析；改 `vehicle_review.py` 或 `vehicle-windows.json` 即可在 feature 分支触发 |
| `.github/workflows/inspect-cosmetics-menu.yml` | 固定 Core 的 UI 及现有远程调用专项分析；只在云端读取输入 IPA |

全量分析已排除 `vehicle_review.py`、`vehicle-windows.json`、`inspect_menu.py` 和 `menu-windows.json` 的改动，专项窗口小改不必重复跑全量 PAK 解析。修改全量工作流自身仍会触发全量任务；本轮无须重复的全量任务已取消，不影响专项成功结果。

最新 r10 云端测试通过 43 项 Python 回归，以及角色读取、同值写、单槽位事务、分页、车辆来源 / 稀疏组件 / 空数组、r9 数据请求事务与恢复、r10 实体 / 哈希链 / 类和加载记录、Mach 转发、环境准备等原生测试，以及最终 IPA 的修改边界和签名核验。

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
| r10 | `38965497830` | 代码 `061d253cccfed672a4047f5bafd8705ced30917a`；artifact `Core-cosmetics-vehicle-r10` |
| r9 | `38959705176` | 代码 `feb99d180133d2d0e643b7e5b9b5a8580ffe9682`；artifact `Core-cosmetics-vehicle-r9` |
| r8 | `38935444227` | 代码 `b49e157b7249a9500bbc78626bcd5d1c60108e6c`；artifact `Core-cosmetics-vehicle-r8` |
| r5 | `38922744774` | 代码 `fa2c0634a21bd0dc4bb0cba0a0c2ef2cbddd7811`；artifact `Core-cosmetics-catalog-r5` |
| r6 | `38928395631` | 代码 `277dc7b0463bf9b025edf3f066d202eb7cd5ae4a`；artifact `Core-cosmetics-vehicle-r6` |
| r7 | `38930863902` | 代码 `efecabbd0fdf0acbbe75f93b496bf575ddf3867b`；artifact `Core-cosmetics-vehicle-r7` |

最新可安装 r10（build 20.10，ProbeVersion 10；只读当前实体诊断已有 8 次成功手机样本）：

```text
/root/和平精英/Core车辆刷新诊断-r10/Core-17.0-21A327-cosmetics-vehicle-r10.ipa
SHA256 d96ba86adfb99a3cab0bd5fe363dcd614c40146077247b83715880c44c4eaa64
```

说明为同目录 `r10说明.md` 和 `采集版使用说明.txt`；构建日志 `/tmp/core-r10-build.log`。本机仅下载及计算摘要，没有执行 IPA 验证。

历史 r9（build 20.9，ProbeVersion 9；写入成功但请求未消费，用户确认画面无变化）：

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

- r10：https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38965497830
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

r10 同一页只有车辆采集和停止等控件，不再提供 r9 新试换入口；有旧修改记录时保留恢复处理。r10 采集报告 mode 为 `vehicle-entity-resource-diagnostics`，schema / probe 为 10，新增 `refresh_diagnostics`。历史 r9 试换报告 mode 为 `single-vehicle-request-trial`，schema / trial 为 9；旧只读采集报告为 `vehicle-read-adaptation`。

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
- 把 `0x3A7D8` 的本地函数体当成 `0x3A7D4` 的实际执行路径，或把入口桥返回的 0 当成初始化成功。

接手时最短摘要：**r8 取车已实机成立，实际走 pawn+0x60C8 控制器回退；r9 写入成功但请求未消费，画面完全没变。r10 构建 38965497830 成功，实体诊断实机 8/8 成功，当前加载原车 3349001。用户确认目标宾利预览正常。映射值本身是 AvatarEntity UObject，+0x30 是 LoadedHandle。`CoreOffline+0x4B0C` 把匹配 `.app/Core` 的可执行文件头写入基址槽，见 11.20 节；打开路径仍只有 launchd 和 SpringBoard。游戏线程仍未建立，没有 r11，车辆换肤未完成。仓库已按授权公开、Actions 正常；不要本机验证编译或把读取 / 数据写入当成换肤成功。**
