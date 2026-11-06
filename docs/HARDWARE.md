# 监控数据来源与硬件兼容性

[返回项目首页](../README.md)

## 数据来源与适用范围

| 项目 | 来源与含义 |
| --- | --- |
| 处理器 | Windows PDH `% Processor Time`，逻辑处理器平均忙碌时间；必要时回退 `GetSystemTimes`。 |
| 频率 | Windows `Processor Frequency × % Processor Performance` 估算 GHz；回退 `CallNtPowerInformation`。不是每个核心的瞬时有效时钟。 |
| 温度 | Intel：优先封装 DTS，回退首个核心 DTS。AMD K8：首个封装中可读核心的最高温度；K10 / FX / 早期 APU：PCI 或 SMU 报告的 Tctl；Zen：SMN 温度，处理范围标志及早期 Ryzen / Threadripper 的 Tctl 偏移。 |
| 功耗 | Intel RAPL / AMD Zen Package Energy 能量增量除以实际采样时间，处理硬件单位与 32 位回绕。失败时尝试 Windows `Energy Meter(rapl_package0_pkg)\Power`，将毫瓦转换为 W。两条路径均只取首个 CPU 封装，不汇总其他插槽、GPU、DRAM 或整机功耗。 |
| 内存 | `GlobalMemoryStatusEx` 的已用物理内存 / 总物理内存。 |
| 显卡 | DXGI 枚举硬件显卡，优先选择独立显存最大的适配器；Windows GPU Engine 只汇总该显卡同一物理引擎的各进程负载，取最繁忙引擎，范围 0–100%。 |
| 显卡传感器 | 使用与 WinMonitor 相同固定构建的 LibreHardwareMonitor 原生桥接库，读取所选显卡的核心温度、核心频率和封装/板卡功耗；不把其他显卡或显存温度混入。缺测显示 `--`。 |
| 显存 | Windows GPU Adapter Memory 的 `Dedicated Usage` 提供该显卡所有进程的用量；悬浮窗只显示已用量，例如 `显存:4.2GB`，按 1024³ 字节换算。DXGI 容量仍用于适配器选择及诊断。 |
| 帧率 | 与 WinMonitor 相同的 PresentMon 1.9.2 ETW 采集；按前台进程、交换链分别统计应用提交帧率，取最繁忙交换链。不以屏幕刷新率或桌面合成器帧率替代。 |
| 1% Low | 与 FPS 使用同一前台进程及交换链，最近 10 秒最慢 1% 帧间隔的平均耗时换算；至少 100 个有效间隔。切换前台进程、隐藏后重新显示时重新积累，样本不足显示 `--`。 |
| 键盘 | 罗技 Alto Keys K75M 的 Logi Bolt 接收器（046D:C548），HID++ 2.0+ 名称识别、Unified Battery 0x1004，必要时回退 0x1000；同时保留 VGN V98Pro V4 1K（391D:2A04）的官方 V HUB 协议。两者同时连接时优先尝试 K75M。 |
| 鼠标 | 通过匹配的只读 HID/HID++ 协议读取；雷蛇包括本机 Viper V3 HyperSpeed（1532:00B8），原始 0–255 换算成百分比。扩展型号及接口清单见下文。 |

键鼠读取已参考 [Voltide 固定提交](https://github.com/sdparklab/Voltide/tree/6f36629b415942ad49305190604382644f7240dc) 扩展：罗技 HID++ 2.0 接收器按配对设备类型分别识别键盘/鼠标；雷蛇 23 个已列入的无线接收器 PID；迈丛 K99 v2/G75 Pro、K7 V2 Ultra+、A5 V3 协议；ROG AZOTH EXTREME/OMNI；IMECOO P1、ATK A9 Air、泰度 TSG809。保留 VGN 旧路径。严格匹配接收器标识、HID 集合及报文格式，不将同品牌所有型号或蓝牙接口视为兼容。详细 VID/PID、来源和逐型号验证边界见 [兼容清单](PERIPHERAL_BATTERY.md)。本机实测为 K75M 和 Viper V3 HyperSpeed，其余为协议移植与报文测试覆盖。

每类显示一个设备，优先使用成功读取的匹配接口；罗技键盘优先，鼠标先尝试明确型号协议，再扫描通用罗技槽位。首次未读到电量显示 `--%`；设备未响应时保留本次运行中最近一次成功读数并标记 `(未更新)`，接收器拔出标记 `(断开)`。键盘睡眠可能不响应，按键唤醒后下次查询会更新；`未更新` 也可能是通信错误。充电标志只显示在有效新读数上。HID++ 仅电量档位、旧 0x1000 的未知零值以及 A5 的离线零值不伪造为百分比；其他协议的真实零电量正常显示。鼠标显示取整，雷蛇原始值始终按 0–255 换算，避免低电量时误报。

HID 查询与系统采样分线程，使用可取消、带超时的 I/O，不阻塞界面和 CPU 刷新。正常每 30 秒更新，超过 35 秒未有新值视为过期；隐藏时暂停查询，恢复显示后重新查询。无需安装 Python、HIDAPI DLL、V HUB 或 Synapse；只使用 Windows 自带 HID / SetupAPI。实现仅发送已验证的电量读取请求，不修改灯光、按键、DPI、配对或固件。协议来源与边界见 [PERIPHERAL_BATTERY.md](PERIPHERAL_BATTERY.md)。

程序通过 CPUID 自动选择官方签名的 PawnIO 模块；支持范围按硬件家族划分，不依赖营销名称匹配：

| CPU 范围 | 温度路径 | 封装功耗路径 |
| --- | --- | --- |
| AMD Family 0Fh：K8 / Athlon 64 / 早期 Opteron、Turion、Sempron 的对应型号 | `AMDFamily0F`，要求 CPUID 声明数字温度传感器，包含已知 65 nm 偏移修正 | 仅在系统暴露封装 Energy Meter 时可用 |
| AMD Family 10h、11h、12h、14h、15h、16h：Phenom / FX / A、E 系列及对应 Athlon、Opteron 等 | `AMDFamily10`，Carrizo / Bristol Ridge / Stoney Ridge 使用 SMU，其余使用 PCI Tctl | 仅在系统暴露封装 Energy Meter 时可用 |
| AMD Family 17h–1Ah：Zen 1–5 的 Ryzen / Ryzen AI / Threadripper / EPYC 等 | `AMDFamily17` SMN Tctl/Tdie | AMD Package Energy；系统计量回退 |
| Intel 提供标准 DTS / RAPL 的 Core、Core Ultra、Xeon 等 | `IntelMSR` DTS，无型号白名单 | Intel RAPL；系统计量回退 |

**“覆盖系列”表示已实现该家族的读取路径，不代表每个型号都提供两项有效读数。** 老 AMD 的 Tctl 是相对热控制值，尤其低温时不能视作准确的结温；部分 Family 10h 还存在已知传感器勘误（AMD erratum 319）。许多旧 CPU 根本没有封装能量计数器，Windows 也未提供该计量项，此时功耗仍显示 `--`，不会用 TDP、使用率推算值或 0 代替。32 位 K7 平台不属于本程序的 Windows x64 支持范围；未知未来家族不会猜测寄存器。

Intel 已核对 Meteor Lake、Arrow Lake、Lunar Lake，以及 Core Ultra Series 3（Panther Lake）/ Core Series 3（Wildcat Lake）。它们使用现有通用读取路径，无需像 WinMonitor 的 LHM 型号分派表那样额外添加白名单；是否可读仍以实际 MSR 和系统权限为准。代码回归覆盖这些型号的 CPUID 解码、DTS 有效位、TjMax 及功耗换算；**最新 Intel 和旧 AMD 尚无逐型号实机验证**。Intel 旧路径此前在 i5-12600KF 实测，Zen 路径在 Ryzen AI 9 HX 370（Family 1Ah / Model 24h）上实测温度和封装功耗。

参考 WinMonitor 时也核对了它固定的 LHM 提交：其中 Family 10h–16h 路径因[频率测量冻结问题](https://github.com/namazso/PawnIO.Modules/issues/32)被停用，并非所有 AMD 均已支持。CpuOverlay 仅调用旧 AMD 模块的温度接口，不执行该频率校准或 C-state 操作。PCI 选择及读取使用共享互斥锁；温度超时不会阻止独立的功耗采样。缺少寄存器、模块或权限时相应项显示 `--`。GPU 占用要求显卡驱动提供 Windows GPU Engine 计数器。

正常运行不联网，不保存历史监控数据。支持 DPI 和显示器变化，选中的显示器断开时回退主显示器。游戏中每 500 毫秒检查遮挡，前台窗口切换时立即检查；被其他置顶窗口盖住后恢复层级，保持鼠标穿透且不抢焦点。隐藏、拖动定位及本程序设置/菜单打开时不强行盖过自己的界面。适用于窗口化和无边框全屏；真正的独占全屏、锁屏和 UAC 安全桌面无法保证覆盖。详细来源及测试见 [GRAPHICS.md](GRAPHICS.md)。

## 参考资料

参考：[PawnIO 模块接口](https://github.com/namazso/PawnIO.Modules/wiki/Using-PawnIO-Modules)、[AMDFamily17 模块源码](https://github.com/namazso/PawnIO.Modules/blob/0.2.11/AMDFamily17.p)、[AMD Family 17h 寄存器参考](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/56255_OSRR.pdf)、[Linux k10temp 温度范围与型号偏移](https://github.com/torvalds/linux/blob/master/drivers/hwmon/k10temp.c)、[WinMonitor 所用 LHM 家族分派](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/3f2b4baa6d1d8d20795e0bc3311cfe819b1567c8/LibreHardwareMonitorLib/Hardware/Cpu/CpuGroup.cs)、[Intel Core Ultra Series 3](https://www.intel.com/content/www/us/en/content-details/871380/intel-core-ultra-series-3-processors-quick-reference-guide.html)、[Intel Core Series 3 / Wildcat Lake](https://edc.intel.com/content/www/us/en/design/platforms/core-processor-series-3-datasheet-volume-1-of-2/001/introduction/)、[Windows Energy Meter](https://learn.microsoft.com/en-us/windows-hardware/drivers/powermeter/energy-meter-interface)、[PawnIO 官方安装器](https://github.com/namazso/PawnIO.Setup)、[Windows GPU 统计口径](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/)、[Windows 服务的桌面隔离](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services)。
