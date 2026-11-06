# PawnIO：Intel 与 AMD 传感器模块

本目录的 `IntelMSR.bin`、`AMDFamily0F.bin`、`AMDFamily10.bin` 和 `AMDFamily17.bin` 均为 PawnIO.Modules **0.2.11 官方签名模块，未修改**。

- [上游项目](https://github.com/namazso/PawnIO.Modules) · [固定发布版本](https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11) · [完整对应源码](https://github.com/namazso/PawnIO.Modules/archive/refs/tags/0.2.11.zip)
- 对应源码：本目录内同名 `.p` 文件，保持上游原样。
- 许可证：LGPL-2.1-or-later，原文见 `COPYING`。

## 固定文件校验值

| 文件 | SHA-256 |
| --- | --- |
| IntelMSR.bin | `d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f` |
| AMDFamily17.bin | `dae74615761b78bdf064dfb3e136252ddcc6fc727d88f14738d0e5800d427a91` |
| AMDFamily17.p | `8a1808a2e51e184f96d17362991ae900c65a1a23135a68c256ec6759e8297bb9` |
| AMDFamily0F.bin | `a6e11619e87a97820705a6523714f22d676ce44f902631833d4429b89d509d55` |
| AMDFamily0F.p | `15df0da167f19bca8642729e18c0b0005f91de391abe79ca56afb249a9c49aa4` |
| AMDFamily10.bin | `6443080b2968474ffbc38aa4356cc56f9664349fa4b917afdb33027d6bb50525` |
| AMDFamily10.p | `caa05c100bf8ebea9a3de1205b574bb793ef2b682630396c063799dd11d99763` |
| 官方压缩包 release_0_2_11.zip | `43608cb89bc84247fef1368a139013f7d043e17db6d6c8dfc9b46bf0905a81f4` |

官方压缩包的 SHA-256 已与 GitHub 发布资产摘要核对。

## CpuOverlay 的调用范围

- `ioctl_read_msr`：Intel / AMD Zen 能量计数和 Intel DTS。
- `ioctl_read_smn`：AMD Family 17h–1Ah 温度。
- `ioctl_get_thermtrip`：AMD K8 / Family 0Fh。
- `ioctl_read_miscctl` / `ioctl_read_smu`：AMD Family 10h、11h、12h、14h、15h、16h。

每次 PCI 选择与读取事务均持有 `Global\Access_PCI` 互斥锁。旧 AMD 温度换算参考 [Linux k10temp](https://github.com/torvalds/linux/blob/master/drivers/hwmon/k10temp.c) 和 [LHM Amd0FCpu](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/master/LibreHardwareMonitorLib/Hardware/Cpu/Amd0FCpu.cs)。不调用 AMDFamily10 的频率校准和 C-state 例程，原因见[上游冻结问题](https://github.com/namazso/PawnIO.Modules/issues/32)。CpuOverlay 不写入 MSR，也不修改电压、功耗上限等硬件设置。

程序根据 CPU 厂商与家族选择 EXE 旁 `bin/pawnio` 内的模块，优先使用外置文件。文件缺失时使用 EXE 中内嵌的同一官方签名模块；外置文件无效或不可访问时报告错误。

## 驱动与权限

驱动及 `PawnIOLib.dll` 由 [PawnIO 官方签名安装器](https://pawnio.eu/) 安装。CpuOverlay 从登记的 PawnIO 安装目录动态加载 DLL。驱动安装和硬件访问需要管理员权限。[驱动源码](https://github.com/namazso/PawnIO)。
