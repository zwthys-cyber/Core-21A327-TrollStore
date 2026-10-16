# Core 车辆限时试换 r9

r8 已在用户手机上连续 8 次读取车辆 `3349001` 成功。r9 保留该取车路径，在“位置”下面的“美化 → 车辆适配”增加“宾利试换15秒”和“恢复原车”。

**实机反馈已确认三字段写入成功，但这条请求没有被游戏消费。主槽已还原、未消费请求已清理，不能宣称车辆画面换肤成功。**

## 范围与使用

只接受当前这辆“4座敞篷跑车” `3349001`，目标是包内配料对应的“宾利欧陆GT敞篷（火焰橘）” `3349002`。主槽必须为 type 33，同步数组为 11 槽位，并匹配 r8 报告中的车辆和 Avatar vtable。其他车型尚未开放。

1. 用巨魔覆盖安装 `Core-17.0-21A327-cosmetics-vehicle-r9.ipa`。
2. 完成原 Core 初始化和采集，在训练场坐进这辆车的驾驶位。
3. “美化 → 车辆适配 → 宾利试换15秒”，约 2 秒准备后开始；保持在同一车辆和座位。
4. 观察是否出现宾利、约 15 秒后是否恢复原车，也可以点“恢复原车”。恢复请求之后最多另观察约 3 秒。
5. 导出“文件 → 我的 iPhone → Core → Core-cosmetics-probe.json”，同时提供“是否变车、是否恢复”的画面观察。

报告 `mode=single-vehicle-request-trial`，schema / trial revision 为 9。只读采集仍可用，mode 为 `vehicle-read-adaptation`，schema / probe revision 为 9；两者共用同名报告文件。

## 实现与证据边界

固定游戏主程序的 OnRep 在 RVA `0x47EE9A8..0x47EE9B0` 把主槽 ID 写入 `Vehicle+0x20AC`（PendingVehicleSkinID）；随后还会比较槽位 `+0x1C` 与 `avatar+0x1208` 的缓存计数。

r9 使用 Core 原比较写接口依次修改主槽 ID、递增主槽更新计数，再提交这个待换肤字段。每项 4 字节；请求可能立即被游戏消费，因此原接口必须确认自己的比较写及回读，再观察外部状态。

**这只复现了 OnRep 中观察到的数据请求写入，没有执行完整 OnRep。现有静态分析尚未证明这个字段单独就能触发游戏消费者。** 当前 / Real / Default 皮肤字段只读，用于确认游戏是否自行更新；不远程调用游戏函数，不改写虚表，也不使用角色 F50。

恢复将主槽还原为 `3349001`、再次递增计数并提交原车请求。程序保存各步修改记录；写入部分失败时尝试恢复已持有的修改。游戏改为其他 ID / 计数、换车、会话或对象实例 / 数组变化时停止覆盖。存在无法确认恢复的记录时阻止开始另一项试换。

如果请求一直未消费、原车字段一直保持原值，最多观察约 3 秒后清理仍属本次的恢复请求，报告 `memory-restored-refresh-unconfirmed`。这不代表画面曾变化。

## 怎样读报告

- `apply.prepare_stage`：准备走到哪一步，及同步槽位 / 缓存计数数量。空缓存允许读取，不会单独阻止试换。
- `apply.id_write_confirmed` / `pending_request_written`：写入确认，不能代替画面确认。
- `observations`：主槽 ID、原生计数、可选缓存计数、Pending、当前 / Real / Default 皮肤。
- `native_refresh_consumption_verified`：观察到 Pending 归零且当前、Real 均为目标；仍不自动确认渲染结果。
- `restore` / `restore_check` / `restore_observations`：恢复请求、原车数据和未完成修改记录。
- `visual_effect_verified`：保留 false，等待用户画面观察。

若主槽和 Pending 是 `3349002`，但当前 / Real 一直为 `3349001`，说明没有观察到这条请求被消费；下一步核对真正的消费或刷新入口，不应重复改同一 ID、强行套用角色 F50，或将写入成功表述为换车成功。

## r9 实机结果与后续阻碍

用户报告 `captured_at_unix=1792182343.3586221`，车型正确。申请阶段写入共 12 字节，状态 1；5 次观察均为主槽 / Pending `3349002`、主槽计数 2，但游戏缓存计数始终 1，当前 / Real / Default 均为原车 `3349001`。恢复后主槽回到原车，计数递增至 3；7 次观察 Pending 恢复请求仍未消费，最后清理该请求，`memory_restored=true`、`change_still_owned=false`、`native_restore_observed=false`。

摘要保存在 `analysis/cosmetics/device-evidence-r9.json`。后续需要核对原生 Handle / 刷新调度；重复相同 r9 试换不能补足这项缺失。

已提交后续分析窗口，Actions [38962343474](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38962343474) 因 GitHub 账号近期付款失败或消费上限而未启动任何步骤。需要在账号 Billing & plans 中处理后重跑；本次没有新安装包，服务器没有执行验证或编译。

## 构建记录

- 代码：`feb99d180133d2d0e643b7e5b9b5a8580ffe9682`。
- 构建：[GitHub Actions 38959705176](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38959705176)，成功。
- Artifact：`Core-cosmetics-vehicle-r9`，CFBundleVersion `20.9`，ProbeVersion `9`。
- IPA SHA-256：`fdb4d3697e854ab895cb3928310d7b8ab662011ced2a30c7486e81aaf83c9f56`。
- 安装包：`/root/和平精英/Core车辆试换-r9/Core-17.0-21A327-cosmetics-vehicle-r9.ipa`。
- 最终构建原始目录：`/root/和平精英/Core车辆试换-r9/最终构建-feb99d1/`。
- 云端完整日志下载副本：`/tmp/core-r9-final-build.log`。

所有编译、测试、二进制分析和签名核验均在 GitHub Actions。回归包含 43 项 Python 测试及原生测试；车辆试换测试覆盖固定车型、三字段比较写、计数递增、部分失败、请求被消费 / 未被消费、恢复、对象变化和游戏接管新值。测试里的消费者是显式模拟器，不代表真实游戏已经消费请求。

详细证据见 `analysis/cosmetics/device-evidence-r8.json`、`vehicle-trial-profile.json` 和 [AI 交接文档](AI_HANDOFF_ZH.md)。
