# build 29 原生射线结果越界

2026-10-15 用户上传 build 29 六份运行日志及两份 Core IPS，并确认“Core 退出，游戏仍运行”。原件和 SHA-256 清单只保留在本地归档，未上传仓库或 CI。

两份系统报告分别为 11:14:28 和 11:17:56（UTC+08）。均为 build 29、iOS 17.0 / 21A327，主线程 `EXC_BAD_ACCESS / SIGSEGV`，发生于像素批量查询；终止类型并非 Jetsam。CoreBootCache.dylib 的 UUID 均为 `9834dde3-79ee-362f-92e3-c022fdc5aa0a`，与实际交付的 build 29 包吻合。

## 定位证据

两次堆栈均为 `CMLClassifyPixels +964 → CMLDraw → CoreModelRenderDispatch`，主程序返回位置都是 `Core+0x2AD2DC`。build 29 对应 dylib 的 `0xFE30` 是调用批量原生入口的 `blraaz x8`，下一条为 `0xFE34`；这与报告中的 `CMLClassifyPixels +964` 对应。原生 `Core+0x2AD2D8` 随后调用从场景句柄取到的函数指针，跳到无效地址。

两次寄存器中，原生句柄 x19 都为 `0x1FFFFFFFF`，而 x0 为它加 `0x58`。编译后的调用者把 RayHit 放在 `SP+0x30`，句柄变量紧跟在 `SP+0x80`，即 RayHit 起点后 80 字节。

旧声明仅保留 80 字节，依据是已识别的字段位置：距离 +32、几何 ID +72、首个实例字段 +76。这个推断遗漏了结果尾部。原 Core 调用者 `D33F4` 的三个结果槽分别为 `SP+0x30`、`SP+0x90`、`SP+0xF0`，间隔 **96 字节**，均按 16 字节对齐，最后一槽也在栈 canary 之前留有完整空间。结合两次一致的句柄损坏，证据指向原生尾部写入破坏了相邻变量，继而造成无效间接跳转。

不能从“首个实例字段在 +76”推导整个结构只有 80 字节。[Embree 的实例命中说明](https://github.com/RenderKit/embree/blob/master/doc/src/tutorials.md) 也说明实例 ID 栈长度由编译配置决定；此处修复以原 Core 的调用者布局和设备崩溃为依据，不把通用库默认布局套到该二进制。

## 修复范围

- 所有采集、原包装查询及批量查询统一改为 **96 字节、16 字节对齐**的结果结构。距离与几何 ID 偏移不变，最后 16 字节保留为不解释的原生可写尾部。
- build 30 / render v3 停用新增的直接批量调用，使用原 `E0BE8/E0CF0` 包装函数逐像素查询；实时蒙皮、填充及 build 29 的可见性/缓存修正保留。
- 反射报告改为发现字段时发布不可变快照。旧 build 29 状态因写入时绘制互斥锁忙，输出 `schema_snapshot_busy=true` 和空列表；这不能证明没有找到字段。新写入路径不需要取得绘制锁。
- 没有把内存占用优化当作已证实的退出根因，也没有在本次修复中改变像素位置计算。

## 自动化验证

`test_model_render_import.py` 固定检查原 Core 的帧大小及三份 96 字节返回槽指令。`test_model_ray_abi_runtime.c` 的独立原生写入器使用固定 96 字节契约，旧的 80 字节布局可复现 `0x1FFFFFFFF` 覆盖相邻句柄；更正后的结构前后 canary 保持不变，尾部完整返回，地址满足 16 字节对齐。

生产实时管线合成测试也写入第 80–95 字节，覆盖多个像素及多个场景，检查没有损坏句柄。云端对这条管线启用 AddressSanitizer，独立 ABI 检查同时启用 AddressSanitizer / UndefinedBehaviorSanitizer；另外验证绘制锁已占用时仍能报告字段快照。

[修复构建 38676541750](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38676541750) 对源码 `eeba16ecffd7d35ecc3f8bb7e65fa0999981af8b` 全部通过：41 项 Python 检查、上述边界测试、既有采集/蒙皮/像素/反射及 Node 检查、签名/精确负载和编译后的 ARM64 入口检查。[普通构建 38676408911](https://github.com/zwthys-cyber/Core-21A327-TrollStore/actions/runs/38676408911) 在生产修复源码 `38fe2efa192813b187c43018d08c57b136367013` 通过；之后仅更改测试断言、增加旧布局复现和显式禁用批量状态标志。

仍需 build 30 实机确认长时间运行和遮挡命中时不再退出。该修复不等于完整人物轮廓、材质裁剪及精确墙沿分色目标已完成。
