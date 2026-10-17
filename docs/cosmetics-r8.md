# Core 车辆适配采集 r8

r7 在驾驶位采集 30 次仍未取得载具使用组件。云端反汇编及原生类名字面量确认：取车函数先将角色解析为 `STExtraPlayerController`，再查找该控制器所属的 `VehicleUserComponent`。r7 直接读取角色上的组件缓存，跳过了这两步。

r8 保留分类服装试穿，修正车辆的只读采集。用户后续报告已确认 8/8 次成功取得车辆 3349001、11 个同步槽位，以及一致的来源和组件归属。实际使用 `pawn+0x60C8` 控制器回退路径，组件集合 39 项。证据摘要见 `analysis/cosmetics/device-evidence-r8.json`。

**车辆换肤、刷新和恢复尚未实现。**

## 改动

- 核对当前角色的解析函数、原生控制器类型、Core 缓存控制器和控制器的当前 pawn；仅接受相互匹配的来源。
- 按原生 Actor 稀疏组件集合的占用位图查找组件，支持内联与堆位图；不再依赖 `+0x47F8` 缓存。
- 使用原生类继承缓存核对组件类型，确认组件 Outer 属于控制器、Character 指向当前角色，然后读取当前车辆。
- 检查组件集合、占用位图、来源对象、车辆数组及皮肤字段在读取期间是否稳定。最多检查 512 个组件槽位，不遍历整个进程。
- 允许车辆同步数组为空、皮肤 ID 为零，保留普通原皮车辆的只读采集。
- 报告升级为 schema / probe revision 8，增加来源解析、组件集合数量、位图、查找阶段与角色归属诊断。

## 安装与采集

1. 用巨魔覆盖安装 `Core-17.0-21A327-cosmetics-vehicle-r8.ipa`。
2. 完成原 Core 初始化和游戏采集，进入训练场并坐在轿车驾驶位。
3. 原悬浮菜单“位置”下面的“美化”→“换分类”到“车辆适配”→“采集车辆”。
4. 取得 8 个成功样本会自动结束；最多等待约 60 秒，也可以停止采集。
5. 导出“文件 → 我的 iPhone → Core → Core-cosmetics-probe.json”。确认 `schema_version`、`probe_revision` 都为 8，`vehicle_path` 为 `controller-owned-vehicle-user-component`。

已坐在驾驶位仍失败时直接保留报告，不需要反复使用同一版本采集。采集不要求账号拥有车辆皮肤，也不要求点击游戏内换肤按钮。

## 构建与验证

- 代码：`b49e157b7249a9500bbc78626bcd5d1c60108e6c`。
- 构建：[GitHub Actions 38935444227](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38935444227)，成功。
- Artifact：`Core-cosmetics-vehicle-r8`；CFBundleVersion `20.8`，CoreCosmeticsProbeVersion `8`。
- IPA SHA-256：`ff0f95a86854aa815b89dac886916e970cb81fdd096ec03b12b20de65cda6fa0`。
- 本地安装包：`/root/和平精英/Core车辆适配-r8/Core-17.0-21A327-cosmetics-vehicle-r8.ipa`。

43 项 Python 回归、原生 C/ObjC 回归及最终 IPA 修改范围与签名核验均在 GitHub Actions 通过。车辆回归覆盖空缓存、稀疏集合空洞、内联 / 堆位图、512 槽位边界、子类识别、类型 / 归属错误、稳定空外观数组及采样中来源或数组变化。

原生位图 ABI 分析：[38933859383](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38933859383)；控制器及组件类名确认：[38934153584](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38934153584)。服务器没有运行编译、测试或二进制分析。

云端检查证明构建及所覆盖的模拟读取行为通过；用户后续实机报告另行确认了当前车辆的读取。外观写入和画面效果仍未验证。完整背景与下一步见 [AI 交接文档](AI_HANDOFF_ZH.md)。
