# Core 车辆实体和模型状态诊断 r10

r9 手机报告确认三项数据写入成功，但 Pending 未消费、游戏缓存计数停在 1、当前和 Real 皮肤一直为原车。用户进一步确认“完全没有变化”，并提出可能没有下载资源包。2026-10-17 随后确认目标宾利在游戏内预览“可以正常显示”；预览资源可以使用，当前重点是完整原生加载和刷新调用。

r10 增加当前车辆外观实体、已加载物品及模型状态的只读采集，保留原 Core 菜单和服装试穿。车辆适配页关闭已确认无效的 r9“宾利试换15秒”入口。本版用于定位原因，尚未实现有效的车辆换肤。

## 安装和采集

适用 iOS 17.0 / 21A327、TrollStore、和平精英 1.38.12 / build 15915。

1. 巨魔覆盖安装 `Core-17.0-21A327-cosmetics-vehicle-r10.ipa`，按原流程初始化 Core。
2. 在训练场坐进上次成功读到 `3349001` 的四座敞篷车驾驶位。
3. Core“位置”下面“美化”→“换分类”到“车辆适配”→“采集车辆”。
4. 取得 8 个车辆样本后自动保存；未就绪时最多约 60 秒，也可手动停止。
5. 从“文件 → 浏览 → 我的 iPhone → Core → Core-cosmetics-probe.json”导出报告。

报告应为 `schema_version=10`、`probe_revision=10`、`mode=vehicle-entity-resource-diagnostics`。报告会覆盖同名旧文件，请先保留需要的历史报告。r10 采集过程中没有车辆写入，不能通过等待 15 秒判断是否修复。

若导出的报告仍是 schema / probe 9、mode `vehicle-read-adaptation`，它属于 r9 采集逻辑。先关闭悬浮并彻底退出 Core，用本页指定的 r10 包覆盖安装，重新启动、初始化和采集，再从 Core 文件夹重新导出。资源缺失不会使 r10 的固定报告版本变成 9；旧文件、旧进程或不同安装包的具体原因需要进一步区分。

## 新增内容与判断边界

`latest_vehicle.refresh_diagnostics` 和各样本新增：

- 主槽实体映射、占用位和原生哈希链查找结果，以及稳定性检查。
- 实体类继承、Slot 0、所属 Avatar 的核对结果。
- 当前 LoadedHandle 的类型、当前已加载物品 ID，以及缓存 / 延迟加载句柄是否存在。
- MeshComponent、MeshAssetPath、LastUseMeshPath 的记录是否存在。
- RenderDirty、DelayRenderDirty、性别刷新标记和可选缓存计数。
- 实体 vtable 及处理函数的相对位置。

这些字段描述**当前实体的加载记录**。路径记录存在不证明目标宾利文件完整，空记录也不能单独证明未下载资源。r10 不扫描游戏磁盘资源、不主动加载宾利；`target_resource_availability=not-checked` 明确表示目标资源尚未检查。当前实体诊断未确认时会保留具体原因，原有车辆采样仍可保存。

诊断接口只有读取能力。原生哈希、稀疏位图、对象身份、归属、数据边界和重复读取均有检查；报告不导出原始地址、PID、物品实例 ID、模型名称或磁盘路径。没有新增远程函数调用、虚表改写或 PAK 替换。

原有角色试穿仍可使用。r9 车辆事务代码保留在编译产物中，但新试换请求被控制器拒绝，菜单不提供启动入口；保留旧修改记录的恢复处理。构建元数据分别记录“包含车辆写入代码”“车辆试换不可用”“车辆采集只读”，避免混淆。

## 构建和交付

- 代码：`061d253cccfed672a4047f5bafd8705ced30917a`。
- [GitHub Actions 38965497830](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38965497830)：2026-10-16 20:54 UTC 成功。
- Artifact：`Core-cosmetics-vehicle-r10`；build `20.10`，ProbeVersion `10`。
- 安装包：`/root/和平精英/Core车辆刷新诊断-r10/Core-17.0-21A327-cosmetics-vehicle-r10.ipa`。
- SHA-256：`d96ba86adfb99a3cab0bd5fe363dcd614c40146077247b83715880c44c4eaa64`。

所有编译、自动化测试、二进制分析和签名核验都在 GitHub Actions。43 项 Python 回归、原有原生测试、新增实体读取测试及最终 IPA 检查均通过。新增测试覆盖稀疏映射、哈希链、已删除槽位、类和归属、加载句柄、路径边界及读取过程中对象变化。

## 实机结果（2026-10-17）

用户已提供真正的 r10 报告，8 次实体诊断全部为 `vehicle-entity-state-sampled`。映射 / 哈希查找一致，类与归属匹配；实体 vtable 为 `0xE67A9B8`，处理函数与静态来源一致。LoadedHandle 的类型和 key 读取成功，type 33、ID `3349001`，MeshComponent 存在。

当前主槽计数与缓存计数同为 1，Pending 和渲染标记均为 0，当前 / Real / Default 仍为原车。这是只读采集的正常原车状态，没有尝试加载宾利。实体两个路径记录为空，不代表目标资源缺失。与旧 r9 的对象实例是否连续没有证据，不能把本次计数 1 描述为旧写入仍未恢复。

用户随后确认目标宾利预览正常显示。这是单独的用户画面反馈，r10 报告中的 `target_resource_availability=not-checked` 保留原值；不能将它改成自动检测通过，也不能由预览推断对局全部依赖已就绪。

证据保存在 `analysis/cosmetics/device-evidence-r10.json`。不需要用户重复同样的只读采集；下一步在云端核对真正的资源请求和刷新调度，以及 Core 远程调用的目标会话和线程。当前没有新安装包。

完整接手背景和静态来源见 [AI 项目交接文档](AI_HANDOFF_ZH.md)。
