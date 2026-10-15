# CpuOverlay

Windows 10 / 11 x64 桌面性能监控，C++ / Win32 原生实现。版本 **1.3.0.0**。

```text
处理器:12.3% | 频率:4.25GHz | 温度:55℃ | 功耗:42.1W | 内存:37.5% | 显卡:18.0%
```

中文标签紧贴数值，整行黄色、同一字号、横向排列，以 `|` 分隔。背景透明、鼠标穿透、置顶，不抢键盘焦点。默认在所选显示器工作区**顶部居中**，距顶部 16 个逻辑像素；文字过宽时统一缩小，不换行。旧版升级首次运行也会切换为顶部居中，之后保留用户选择。

## 安装与使用

双击 **`package/CpuOverlay-Setup.exe`**，允许 Windows 管理员权限提示，点击“安装”。离线安装包内置 CpuOverlay、传感器模块和官方签名的 PawnIO 2.2.0 安装程序。检测到未安装 PawnIO 时自动安装；已安装则直接使用。安装时可以选择目录和登录自启。

升级默认使用已登记的目录，或当前正在运行的 CpuOverlay 所在目录。安装器先关闭旧版、备份旧 EXE，再更新文件；支持从 Windows“已安装的应用”卸载。共享的 PawnIO 驱动不会随本工具卸载。

- **Alt+F1**：显示 / 隐藏；长按不会连续切换。
- **单击托盘图标**：显示 / 隐藏。
- **右键托盘图标**：顶部居中 / 四角位置、字号、显示器、登录自启、使用说明、退出。
- 安装完成立即显示；以后登录启动时隐藏驻留，按 Alt+F1 显示。
- 每秒采样；隐藏后暂停，重新显示约一秒后出现读数。
- 再次启动只显示已有实例；热键被其他软件占用时有明确提示，可通过托盘操作。

托盘图标可能位于右下角 `^` 中。悬浮窗不能直接拖动，请使用托盘的位置菜单。

## 自动启动与权限

PawnIO 硬件访问需要管理员权限。手动启动主程序时会请求提权；安装器创建当前用户的最高权限计划任务，用户登录桌面后自动运行，无需每次登录点击 UAC。

任务名称为 `CpuOverlay-<当前用户 SID>`，使用交互式用户令牌，不保存密码。程序不是 Session 0 服务，必须在用户桌面会话中才能显示窗口和响应热键。此版本面向拥有本机管理员权限的用户。

配置保存在 `HKCU\Software\sixunited\CpuOverlay`。旧版 Run 启动项会迁移为计划任务；取消自启后不会在下次启动自行恢复。移动便携目录前先退出，移动后重新运行并启用自启以更新路径。

## 数据来源与适用范围

| 项目 | 来源与含义 |
| --- | --- |
| 处理器 | Windows PDH `% Processor Time`，逻辑处理器平均忙碌时间；必要时回退 `GetSystemTimes`。 |
| 频率 | Windows `Processor Frequency × % Processor Performance` 估算 GHz；回退 `CallNtPowerInformation`。不是每个核心的瞬时有效时钟。 |
| 温度 | Intel：优先封装 DTS，回退首个核心 DTS。AMD K8：首个封装中可读核心的最高温度；K10 / FX / 早期 APU：PCI 或 SMU 报告的 Tctl；Zen：SMN 温度，处理范围标志及早期 Ryzen / Threadripper 的 Tctl 偏移。 |
| 功耗 | Intel RAPL / AMD Zen Package Energy 能量增量除以实际采样时间，处理硬件单位与 32 位回绕。失败时尝试 Windows `Energy Meter(rapl_package0_pkg)\Power`，将毫瓦转换为 W。两条路径均只取首个 CPU 封装，不汇总其他插槽、GPU、DRAM 或整机功耗。 |
| 内存 | `GlobalMemoryStatusEx` 的已用物理内存 / 总物理内存。 |
| 显卡 | Windows GPU Engine 计数器；先汇总同一物理引擎的各进程负载，再取所有显卡中最繁忙的引擎，范围 0–100%。 |

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

正常运行不联网、不保存历史监控数据。支持 DPI 和显示器变化，选中的显示器断开时回退主显示器。普通置顶窗口不能保证覆盖独占全屏游戏、锁屏或 UAC 安全桌面。

## 发布文件

```text
package/
├── CpuOverlay-Setup.exe       # 离线安装器，内含以下运行文件及官方 PawnIO 安装器
├── CpuOverlay.exe             # 主程序
└── bin/
    └── pawnio/
        ├── IntelMSR.bin       # 官方签名 Intel 模块
        ├── IntelMSR.p         # 对应源码
        ├── AMDFamily0F.bin    # 官方签名 AMD K8 模块
        ├── AMDFamily0F.p      # 对应源码
        ├── AMDFamily10.bin    # 官方签名 AMD K10 / FX / 早期 APU 模块
        ├── AMDFamily10.p      # 对应源码
        ├── AMDFamily17.bin    # 官方签名 AMD Zen 模块
        ├── AMDFamily17.p      # 对应源码
        ├── COPYING
        └── README.md
```

安装器单独复制到其他机器即可使用。便携包请同时保留主 EXE 和 `bin/pawnio`（含可替换模块、源码和许可证），并预先安装 [PawnIO](https://pawnio.eu/)。程序优先从 EXE 所在目录加载外置模块；文件缺失时使用 EXE 内嵌的同一官方签名模块，保证登录任务仍可采样。损坏或拒绝访问的外置模块会报告错误，不静默忽略。`PawnIOLib.dll` 从 PawnIO 系统安装目录加载，不依赖工作目录或开发环境 PATH；MSVC 运行库静态链接，无需安装 .NET / VC++ Redistributable。

安装目录的 `bin` 下还保存说明文档、卸载用安装器及升级备份。卸载只移除本程序登记的文件，保留备份和用户偏好。

## 构建与验证

需要 Visual Studio 2022 / 2026 C++ 桌面开发组件、Windows SDK 和匹配版本的 CMake（VS 2026 需要 CMake 4.2+）。

```powershell
cd D:\Code\CpuOverlay
.\build.ps1
# 无需管理员权限或 PawnIO 的算法测试：
cmake --build build --config Release --target CpuOverlaySensorTests
ctest --test-dir build -C Release --output-on-failure
# 无驱动环境下验证发布文件、依赖、绘图和操作系统指标：
.\scripts\verify-release.ps1 -SkipHardware
# 在管理员 PowerShell 中验证真实传感器：
.\scripts\verify-release.ps1
```

构建脚本首次从官方 GitHub 下载固定版本 PawnIO 安装器，并校验 SHA-256 和 Authenticode 签名；随后可复用缓存。四个模块固定为官方 0.2.11 版本，附来源、校验和及 LGPL 许可证。发布验证同时检查外置、主程序内嵌和安装器内嵌模块的一致性。无需另装安装包编译器。

完整硬件验证要求温度和功耗都可用；缺少功耗计量的旧平台可以用 `--diagnostics` 检查温度及来源，命令返回 3 表示存在缺测项。`pawnio_status` / `pawnio_hresult` 保留原后端状态；若 `power_source` 为 `windows_energy_meter_package0`，最终功耗由系统回退提供，不能再用 MSR 原始值复算。`energy_meter_milliwatts` 记录独立的系统计量读数。

验证脚本检查两个发布 EXE 的版本资源、英文语言、静态运行库依赖、Intel / AMD 外置模块和内嵌模块的哈希、安装器载荷、真实六项采样与原始 MSR / SMN 计算、透明黄色横排绘图，并用仅包含系统目录的 PATH、异于 EXE 的工作目录运行发布程序。`-SkipHardware` 会明确将温度 / 功耗实测标记为跳过，不代表硬件读取验证通过。

桌面集成测试：构建 `CpuOverlayProbe` 目标，运行测试窗口后按两次 Alt+F1，再点击顶部监控文字，检查焦点、穿透点击和居中位置。测试工具不发布。

## 命令行

| 主程序参数 | 用途 |
| --- | --- |
| `--show` / `--hide` / `--toggle` | 显示 / 隐藏 / 切换 |
| `--startup` | 登录启动，隐藏驻留 |
| `--exit` | 退出已有实例 |
| `--enable-startup` / `--disable-startup` | 配置当前用户计划任务后退出，需管理员权限 |
| `--no-autostart` | 本次启动不自动更新自启配置 |
| `--diagnostics <JSON绝对路径>` | 采集四个真实样本、CPU 厂商 / Family / Model、读取来源、原始 MSR / SMN / PCI 温度、能量单位、温度偏移与系统计量毫瓦值；需从管理员进程启动才能读 PawnIO；不改自启 |
| `--preview <PNG绝对路径>` | 输出同一绘图路径生成的透明示例图；不改自启 |

诊断退出码：0=六项数据正常，2=文件/参数错误，3=至少一项不可用。预览数字是示例数据。

安装器可使用 `--silent --target "绝对目录"`；可选 `--no-launch`、`--no-startup`、`--report "JSON绝对路径"`。`--uninstall --target "登记的安装目录"` 卸载。静默模式仍需管理员权限，返回 0 成功、1 失败、1223 取消；不会绕过 UAC。已经安装的 PawnIO 不会被重复安装或降级。

## 程序属性

产品 CpuOverlay，版本 1.3.0.0，CompanyName `sixunited`，Author `sclliang`。作者及版权仅保存在文件元数据，不在程序界面显示。

- 主程序 FileDescription：`CPU, Memory and GPU Performance Desktop Overlay`
- 安装器 FileDescription：`CpuOverlay Installer with PawnIO Driver Setup`
- OriginalFilename 分别为 `CpuOverlay.exe`、`CpuOverlay-Setup.exe`。
- `Copyright (c) 2026 sixunited. Author: sclliang`
- VERSIONINFO：English (United States)，数字版本与字符串版本一致。

参考：[PawnIO 模块接口](https://github.com/namazso/PawnIO.Modules/wiki/Using-PawnIO-Modules)、[AMDFamily17 模块源码](https://github.com/namazso/PawnIO.Modules/blob/0.2.11/AMDFamily17.p)、[AMD Family 17h 寄存器参考](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/56255_OSRR.pdf)、[Linux k10temp 温度范围与型号偏移](https://github.com/torvalds/linux/blob/master/drivers/hwmon/k10temp.c)、[WinMonitor 所用 LHM 家族分派](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/3f2b4baa6d1d8d20795e0bc3311cfe819b1567c8/LibreHardwareMonitorLib/Hardware/Cpu/CpuGroup.cs)、[Intel Core Ultra Series 3](https://www.intel.com/content/www/us/en/content-details/871380/intel-core-ultra-series-3-processors-quick-reference-guide.html)、[Intel Core Series 3 / Wildcat Lake](https://edc.intel.com/content/www/us/en/design/platforms/core-processor-series-3-datasheet-volume-1-of-2/001/introduction/)、[Windows Energy Meter](https://learn.microsoft.com/en-us/windows-hardware/drivers/powermeter/energy-meter-interface)、[PawnIO 官方安装器](https://github.com/namazso/PawnIO.Setup)、[Windows GPU 统计口径](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/)、[Windows 服务的桌面隔离](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services)。
