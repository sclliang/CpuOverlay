# CpuOverlay

Windows 10 / 11 x64 桌面性能监控，C++ / Win32 原生实现。版本 **1.7.0.0**。

```text
处理器:12.3% | 频率:4.25GHz | 温度:55℃ | 功耗:42.1W | 内存:37.5% | 显卡:18.0% | 键盘:14% | 鼠标:49%
```

默认使用黄色粗体文字、横向排列，以 `|` 分隔。背景透明、鼠标穿透、置顶，不抢键盘焦点。初始位置为所选显示器工作区**顶部居中**，距顶部 16 个逻辑像素。可在设置中更改快捷键、排列、位置及文字外观；超过屏幕宽度或高度时等比缩小，保持内容可见。

## 安装与使用

双击 **`package/CpuOverlay-Setup.exe`**，允许 Windows 管理员权限提示，点击“安装”。离线安装包内置 CpuOverlay、传感器模块和官方签名的 PawnIO 2.2.0 安装程序。检测到未安装 PawnIO 时自动安装；已安装则直接使用。安装时可以选择目录和登录自启。

升级默认使用已登记的目录，或当前正在运行的 CpuOverlay 所在目录。安装器先关闭旧版、备份旧 EXE，再更新文件；支持从 Windows“已安装的应用”卸载。共享的 PawnIO 驱动不会随本工具卸载。

- **Alt+F1（默认，可修改）**：显示 / 隐藏；长按不会连续切换。
- **单击托盘图标**：显示 / 隐藏。
- **右键托盘图标 → 设置**：快捷键、横向 / 纵向排列、自定义坐标、拖动定位和字体外观。菜单也保留常用位置、字号、显示器及排列方向的快捷选项。
- 安装完成立即显示；以后登录启动时隐藏驻留，按已配置的快捷键显示。
- 系统指标每秒采样；键鼠电量独立线程每 30 秒查询，读取失败时每 5 秒再查询。隐藏后暂停这些显示项的完整采样，但已连接的乔思伯数显屏继续获取 CPU 温度。
- 再次启动只显示已有实例；热键被其他软件占用时有明确提示，可通过托盘操作。

托盘图标可能位于右下角 `^` 中。平时悬浮窗保持鼠标穿透；需要移动时，在设置中点击“拖动定位”，拖动桌面上的浅色监控条，松开鼠标后会把位置填回设置窗口，并恢复鼠标穿透。点击“应用”或“确定”保存；“取消”撤销尚未应用的修改。

## 自定义设置

设置窗口使用浅色背景，可通过托盘菜单或 `CpuOverlay.exe --settings` 打开。

| 设置 | 可调整内容 |
| --- | --- |
| 显示内容 | 8 个复选框独立选择 CPU 占用、频率、温度、功耗、内存、显卡、键盘电量、鼠标电量。取消勾选后文字和分隔符自动收拢；全部取消时悬浮窗透明，不影响水泵数显。 |
| 快捷键 | 在输入框中直接按键，支持 Ctrl / Alt / Shift 组合及 F1–F11；需要 Win 键时勾选“加上 Win 键”。F12 和常用输入控制键不作为全局快捷键。冲突时保留原绑定与配置。 |
| 排列 | 横向以 `|` 分隔；纵向每项占一行。所有指标继续使用同一字号。 |
| 位置 | 顶部居中、四角或自定义 X/Y；可选择显示器、跟随主屏、调整预设位置的边缘留白，或使用拖动定位。 |
| 字体 | 选择本机已安装的字体，字号 8–96 磅，常规 / 加粗以及斜体。字体缺失时回退到系统字体。 |
| 颜色 | 文字及描边分别设置，支持颜色选择器或 `#RRGGBB` 输入。 |
| 细节 | 不透明度 10–100%，间距 0–40，描边宽度 0–6（0 表示不描边）。 |

坐标相对于所选显示器**工作区左上角**，按系统 DPI 缩放调整；桌面任务栏占用的区域不计入工作区。显示器断开时回退主屏，坐标超出可见范围时自动收回。间距控制横向分隔项的留白或纵向各行之间的距离。

窗口内有外观示例，“桌面预览”使用真实采样显示当前草稿，不保存配置或切换快捷键。“应用”保存并立即生效，“确定”保存后关闭，“取消”保留上一次已应用的配置。预览或定位结束后恢复打开设置前的显示 / 隐藏状态。“恢复默认”只重置草稿，仍需应用或确定。

升级时沿用旧版的位置、显示器、字体、颜色和快捷键，新复选框默认全部勾选。新配置使用单个 `DisplaySettingsV2` 注册表值保存；兼容读取并保留旧 `DisplaySettingsV1`，避免升级丢失偏好。显示设置不会修改登录自启偏好。复选框只控制显示，不改变硬件采样或水泵温度发送。

## 乔思伯水冷数显屏

从 1.6.0.0 起，CpuOverlay 运行后自动连接本机的乔思伯数显屏（USB `5131:2007`，HID UsagePage `FF00` / Usage `01`），直接发送与悬浮窗相同来源的 CPU 温度，单位固定为摄氏度。无需启动或安装 JONSBO PC Monitor，不需要额外运行库。原乔思伯软件和它的启动项由用户自行卸载，本程序不修改它们。

- 登录后隐藏驻留时仍发送；Alt+F1 只切换悬浮窗，不停止数显屏更新。
- 温度每秒采样，显示报文每 200 毫秒发送；隐藏时只保留硬件传感器采样，不刷新 GPU / 内存计数器和键鼠电量。
- 只支持已核对的上述设备和温度显示协议；不控制水泵转速、风扇、灯光或固件。
- 设备未连接时静默等待，每 5 秒重连；其他程序占用写入接口时也等待，不终止对方进程。
- 无有效温度或样本超过 3 秒时停止发送，不用 0℃ 代替故障。屏幕可能保留最后一个数字，其清屏行为由固件决定。程序退出后停止更新；系统休眠时释放接口，恢复后重连并读取新温度。

协议核对、状态及测试说明见 [docs/PUMP_DISPLAY.md](docs/PUMP_DISPLAY.md)。

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
| 键盘 | VGN V98Pro V4 1K 的 2.4G 接收器（391D:2A04），官方 V HUB 的 HID 电量查询报文，返回百分比及充电状态。 |
| 鼠标 | Razer Viper V3 HyperSpeed 的原厂 2.4G 接收器（1532:00B8），HID Feature 电量查询，原始 0–255 换算成百分比。 |

键鼠监控目前仅适配以上两个接收器，不把其他 VGN/雷蛇型号或蓝牙接口视为兼容。首次未读到电量显示 `--%`；设备未响应时保留本次运行中最近一次成功读数并标记 `(未更新)`，接收器拔出标记 `(断开)`。有线下不支持的接口不会猜测电量。键盘睡眠可能不响应，按键唤醒后下次查询会更新；`未更新` 也可能是通信错误，不能据此断定正在休眠。有效的键盘充电状态显示 `(充电)`，失去响应后不会保留旧充电标记。读数是设备估计值，鼠标显示取整。

HID 查询与系统采样分线程，使用可取消、带超时的 I/O，不阻塞界面和 CPU 刷新。正常每 30 秒更新，超过 35 秒未有新值视为过期；隐藏时暂停查询，恢复显示后重新查询。无需安装 Python、HIDAPI DLL、V HUB 或 Synapse；只使用 Windows 自带 HID / SetupAPI。实现仅发送已验证的电量读取请求，不修改灯光、按键、DPI、配对或固件。协议来源与边界见 [docs/PERIPHERAL_BATTERY.md](docs/PERIPHERAL_BATTERY.md)。

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
cmake --build build --config Release --target CpuOverlaySensorTests CpuOverlaySettingsTests CpuOverlayBatteryTests CpuOverlayPumpTests
ctest --test-dir build -C Release --output-on-failure
# 无驱动环境下验证发布文件、依赖、绘图和操作系统指标：
.\scripts\verify-release.ps1 -SkipHardware
# 在管理员 PowerShell 中验证真实传感器：
.\scripts\verify-release.ps1
```

构建脚本首次从官方 GitHub 下载固定版本 PawnIO 安装器，并校验 SHA-256 和 Authenticode 签名；随后可复用缓存。四个模块固定为官方 0.2.11 版本，附来源、校验和及 LGPL 许可证。发布验证同时检查外置、主程序内嵌和安装器内嵌模块的一致性。无需另装安装包编译器。

完整硬件验证要求温度和功耗都可用；缺少功耗计量的旧平台可以用 `--diagnostics` 检查温度及来源，命令返回 3 表示存在缺测项。`pawnio_status` / `pawnio_hresult` 保留原后端状态；若 `power_source` 为 `windows_energy_meter_package0`，最终功耗由系统回退提供，不能再用 MSR 原始值复算。`energy_meter_milliwatts` 记录独立的系统计量读数。

验证脚本检查两个发布 EXE 的版本资源、英文语言、静态运行库依赖、Intel / AMD 外置模块和内嵌模块的哈希、安装器载荷、真实六项采样与原始 MSR / SMN 计算、透明黄色横排绘图，并用仅包含系统目录的 PATH、异于 EXE 的工作目录运行发布程序。`-SkipHardware` 会明确将温度 / 功耗实测标记为跳过，不代表硬件读取验证通过。

自动测试覆盖传感器换算、配置读写与异常数据、DPI 和屏幕边界、横纵绘制、颜色 / 透明度，以及真实 Windows 热键注册中的冲突、取消和切换。桌面集成测试可构建 `CpuOverlayProbe` 目标，在默认顶部居中布局下使用已配置快捷键检查焦点和穿透点击。测试工具不发布。

## 命令行

| 主程序参数 | 用途 |
| --- | --- |
| `--show` / `--hide` / `--toggle` | 显示 / 隐藏 / 切换 |
| `--startup` | 登录启动，隐藏驻留 |
| `--exit` | 退出已有实例 |
| `--enable-startup` / `--disable-startup` | 配置当前用户计划任务后退出，需管理员权限 |
| `--no-autostart` | 本次启动不自动更新自启配置 |
| `--diagnostics <JSON绝对路径>` | 采集四个真实样本、CPU 厂商 / Family / Model、读取来源、原始 MSR / SMN / PCI 温度、能量单位、温度偏移与系统计量毫瓦值；需从管理员进程启动才能读 PawnIO；不改自启 |
| `--battery-diagnostics <JSON绝对路径>` | 只查询两个接收器的电量，输出电量、状态、最近成功读数的时间间隔及 Win32 错误码；无需管理员权限，不改自启或现有实例 |
| `--pump-diagnostics <JSON绝对路径>` | 无窗口向数显屏发送真实 CPU 温度约 5 秒，并检查暂停/恢复；从管理员终端运行才能读 PawnIO，不能与正在占用数显屏的实例同时测试。返回 0=发送及暂停恢复成功，3=温度/连接/写入不可用，2=文件错误 |
| `--settings` | 打开设置窗口；已有实例时将设置请求发送给该实例 |
| `--preview <PNG绝对路径>` | 输出同一绘图路径生成的透明示例图；不改自启 |

普通诊断退出码：0=六项系统数据正常，2=文件/参数错误，3=至少一项系统数据不可用。附带的电量不影响普通诊断退出码，以免未连接指定外设时误判系统采样失败。单独电量诊断返回 0 表示两个电量均成功读取，3 表示至少一个未读到；同样以 2 表示文件/参数错误。预览数字是示例数据。

安装器可使用 `--silent --target "绝对目录"`；可选 `--no-launch`、`--no-startup`、`--report "JSON绝对路径"`。`--uninstall --target "登记的安装目录"` 卸载。静默模式仍需管理员权限，返回 0 成功、1 失败、1223 取消；不会绕过 UAC。已经安装的 PawnIO 不会被重复安装或降级。

## 程序属性

产品 CpuOverlay，版本 1.7.0.0，CompanyName `sixunited`，Author `sclliang`。作者及版权仅保存在文件元数据，不在程序界面显示。

- 主程序 FileDescription：`System Performance, Battery and Cooling Display Monitor`
- 安装器 FileDescription：`CpuOverlay Installer with PawnIO Driver Setup`
- OriginalFilename 分别为 `CpuOverlay.exe`、`CpuOverlay-Setup.exe`。
- `Copyright (c) 2026 sixunited. Author: sclliang`
- VERSIONINFO：English (United States)，数字版本与字符串版本一致。

参考：[PawnIO 模块接口](https://github.com/namazso/PawnIO.Modules/wiki/Using-PawnIO-Modules)、[AMDFamily17 模块源码](https://github.com/namazso/PawnIO.Modules/blob/0.2.11/AMDFamily17.p)、[AMD Family 17h 寄存器参考](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/56255_OSRR.pdf)、[Linux k10temp 温度范围与型号偏移](https://github.com/torvalds/linux/blob/master/drivers/hwmon/k10temp.c)、[WinMonitor 所用 LHM 家族分派](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/3f2b4baa6d1d8d20795e0bc3311cfe819b1567c8/LibreHardwareMonitorLib/Hardware/Cpu/CpuGroup.cs)、[Intel Core Ultra Series 3](https://www.intel.com/content/www/us/en/content-details/871380/intel-core-ultra-series-3-processors-quick-reference-guide.html)、[Intel Core Series 3 / Wildcat Lake](https://edc.intel.com/content/www/us/en/design/platforms/core-processor-series-3-datasheet-volume-1-of-2/001/introduction/)、[Windows Energy Meter](https://learn.microsoft.com/en-us/windows-hardware/drivers/powermeter/energy-meter-interface)、[PawnIO 官方安装器](https://github.com/namazso/PawnIO.Setup)、[Windows GPU 统计口径](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/)、[Windows 服务的桌面隔离](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services)。
