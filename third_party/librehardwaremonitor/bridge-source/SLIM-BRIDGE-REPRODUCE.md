# 精简 LhmBridge NativeAOT 构建记录

## 来源与复现范围

- 桥接源码快照：WatchHWInfo 提交 `b49fc4d49b40b6979acf4e195c0e5f2c13d151c8`。
- LibreHardwareMonitor 快照：[提交 3f2b4ba](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/tree/3f2b4baa6d1d8d20795e0bc3311cfe819b1567c8)。
- 导出的桥接 ABI 与 `Bridge.cs` 保持原样，本目录保留桥接入口源码和工程文件。

本记录说明已随仓库分发的固定 DLL 的原始构建过程。**本目录未包含完整 LibreHardwareMonitor 源码快照及所有修改后的源码文件**；独立复现需要先取得上述原始快照并应用以下改动。工程内的相对 `ProjectReference` 按原始目录布局解析；常规 CpuOverlay 构建直接使用已校验的 DLL，无需重建桥接。

## 相对原始快照的改动

1. `lhm-bridge/LhmBridge.csproj`：将 `IlcGenerateCompleteTypeMetadata` 从 `true` 改为 `false`。
2. `third_party/LibreHardwareMonitor/LibreHardwareMonitorLib/LibreHardwareMonitorLib.csproj`：移除 `DiskInfoToolkit`、`RAMSPDToolkit-NDD`、`HidSharp`、`Mono.Posix.NETStandard`、`System.IO.Ports` 包引用；排除桥接未使用的 `Hardware/Controller/**`、`Hardware/Memory/**`、`Hardware/PowerMonitor/**`、`Hardware/Psu/**`、`Hardware/Storage/**`、`Interop/PowerMonitor/**` 和 `RAMSPDToolkitDriver.cs`。
3. `Hardware/Computer.cs`：移除上述功能的导入和组创建、删除逻辑，保留公开布尔属性作为状态值。CPU / GPU 的创建、更新、枚举和清理不变。
4. `Hardware/Identifier.cs`：移除 `HidSharp` 导入及只被已排除功能使用的 `Identifier(HidDevice)` 构造函数。
5. `Hardware/OpCode.cs`：仅在定义 `LINUX` 时编译 Mono mmap/munmap 路径；`win-x64` 直接使用现有 `VirtualAlloc` / `VirtualFree`。
6. `Hardware/Cpu/IntelCpu.cs`：将型号 `0xD5`（CPUID `0x000D0651`，Wildcat Lake，例如 Core 3 304）加入 `case 0xCC` 的 Panther Lake 分支，以注册 DTS 封装 / 核心温度传感器。

## 原始构建命令

使用 .NET SDK `10.0.302` 和 Visual Studio 18 的 MSVC NativeAOT 工具链。在准备好快照和修改后的源码根目录中执行：

```powershell
dotnet publish lhm-bridge/LhmBridge.csproj -c Release -r win-x64 -o publish-slim --nologo '-clp:ErrorsOnly;Summary'
```

| 输出 | 值 |
| --- | --- |
| 文件 | `publish-slim/LhmBridge.dll`，分发名为 `LhmCpuSensorBridge.dll` |
| 大小 | 5,204,992 字节 |
| SHA-256 | `696D0C1A178013D88450093E61648C2FF399E4D0941F9B97B18165EA9AD35E61` |
| 未加入 Wildcat Lake 补丁的基线 SHA-256 | `79ECBA01148B719DA45BB2F9A45DF98BC63568963462FDDF4AA339073E197063` |

## 原始验证记录

`dumpbin /exports` 中的六个桥接导出保持不变：`lhm_open`、`lhm_refresh`、`lhm_sensor_count`、`lhm_get_sensor`、`lhm_last_error`、`lhm_close`。NativeAOT 的 `DotNetRuntimeDebugHeader` 导出也与原构建一致。

输出内的固定字符串扫描未发现 `DiskInfoToolkit`、`pci.ids`、`usb.ids`、`RAMSPDToolkit`、`BlackSharp`、`HidSharp`、`Mono.Posix.NETStandard`、`Mono.Unix` 或 `System.IO.Ports`。

原始无 PawnIO 环境的 `ctypes` 实测返回 `open=1`、`refresh=1`，枚举 47 个 CPU / GPU 传感器；示例读数包括 CPU 总负载 26.23%、NVIDIA 核心 44.125℃ / 27%、板卡功耗 26.977 W、显存结温 50℃、GPU Package 48.727 W。这是原始桥接测试记录；CPU 温度和功耗仍需 PawnIO。内存占用由主程序另行使用 `GlobalMemoryStatusEx` 获取。

## 依赖与许可证清单

| 代码或依赖 | 版本 / 许可证 |
| --- | --- |
| LibreHardwareMonitor | 上述固定提交，MPL-2.0 |
| 内嵌 PawnIO.Modules | 0.1.6，LGPL-2.1，来源为上游 `Resources/PawnIo/COPYING` |
| .NET 10 NativeAOT | MIT 及 .NET 运行时第三方声明 |
| Microsoft.Win32.Registry | 5.0.0，MIT 及第三方声明 |
| System.Management | 10.0.10，MIT 及第三方声明 |
| System.Threading.AccessControl | 10.0.10，MIT 及第三方声明 |
| Microsoft.Windows.CsWin32 生成的互操作代码 | 0.3.298，MIT / NOTICE |

还原时的传递 / 构建包还包括 `System.IO.FileSystem.AccessControl` 5.0.0、`System.CodeDom` 10.0.10、`Microsoft.DotNet.ILCompiler` 10.0.10、`Microsoft.NET.ILLink.Tasks` 10.0.10（MIT），以及 `Microsoft.Windows.SDK.Win32Docs` 0.1.42-alpha、`Microsoft.Windows.Win32Metadata` 70.0.11-preview、`Microsoft.Windows.WDK.Win32Metadata` 0.13.25-experimental（各自的构建元数据许可）。它们受 NativeAOT 裁剪，不作为单独 DLL 分发。

精简构建移除了 DiskInfoToolkit、RAMSPDToolkit-NDD 及其 BlackSharp.Core 依赖、HidSharp、Mono.Posix.NETStandard、System.IO.Ports。GPU 厂商 DLL 从已安装的显示驱动加载，不随桥接分发。

原始测试环境已清理临时手动安装的 PawnIO 2.2.0 及其新增项目，保留原有 PawnIO 2.1.0。该记录只描述历史验证环境，不代表 CpuOverlay 执行驱动卸载。
