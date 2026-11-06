# LibreHardwareMonitor：GPU 传感器桥接库

CpuOverlay 复用 WinMonitor 中固定构建的 x64 NativeAOT 桥接库，安装为 `bin/gpu/LhmCpuSensorBridge.dll`。程序使用绝对路径加载，并将依赖查找限制在系统目录；运行时无需安装 .NET。

CpuOverlay 只消费桥接库的 GPU 传感器记录。该构建内部会枚举 CPU / GPU，但主程序的 CPU 指标继续使用自身的 PawnIO 后端。CpuOverlay 使用官方 PawnIO 安装路径，未引入 WinMonitor 私有驱动服务的创建和清理流程。

## 固定构建来源

| 项目 | 固定信息 |
| --- | --- |
| LibreHardwareMonitor 源码 | [提交 3f2b4ba](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/tree/3f2b4baa6d1d8d20795e0bc3311cfe819b1567c8) |
| 桥接源码来源 | WatchHWInfo 提交 `b49fc4d49b40b6979acf4e195c0e5f2c13d151c8` |
| 输出大小 | 5,204,992 字节 |
| SHA-256 | `696D0C1A178013D88450093E61648C2FF399E4D0941F9B97B18165EA9AD35E61` |
| 保留的桥接源码和构建记录 | [bridge-source](bridge-source/) · [构建与复现说明](bridge-source/SLIM-BRIDGE-REPRODUCE.md) |

历史文件名沿用原桥接资源约定。CMake 和发布验证均检查固定 SHA-256。

原始构建在 LibreHardwareMonitor 的 `Hardware/Cpu/IntelCpu.cs` 中将 CPUID 型号 `0xD5`（Wildcat Lake / Core Series 3）加入 Panther Lake 分派，以注册数字温度传感器。该改动影响桥接内部 CPU 枚举；CpuOverlay 自身的 CPU 后端不依赖此分派。

## 许可证

- LibreHardwareMonitor：MPL-2.0，原文见 `LICENSE-MPL-2.0`。
- 桥接内嵌的 PawnIO.Modules：LGPL-2.1，原文见 `LICENSE-PawnIO-Modules-LGPL-2.1`。
- NativeAOT 及构建依赖的详细清单见[构建记录](bridge-source/SLIM-BRIDGE-REPRODUCE.md)。

以上许可证随 DLL 一起分发。桥接库本身不安装或管理 PawnIO。
