# 构建、发布与验证

[返回项目首页](../README.md)

## 发布文件

```text
package/
├── CpuOverlay-Setup.exe       # 离线安装器，内含以下运行文件及官方 PawnIO 安装器
├── CpuOverlay.exe             # 主程序
└── bin/
    ├── game/                  # 内置 D3D11 / D3D12 / OpenGL / Vulkan 游戏显示
    │   ├── CpuOverlayGame64.dll
    │   ├── CpuOverlayGame64.json
    │   └── LICENSE-*.txt
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

安装器单独复制到其他机器即可使用。便携包请同时保留主 EXE 和整个 `bin` 目录（含 CPU 模块、GPU 桥接库、对应源码及许可证），并预先安装 [PawnIO](https://pawnio.eu/)。程序优先从 EXE 所在目录加载外置模块；文件缺失时使用 EXE 内嵌的同一官方签名模块，保证登录任务仍可采样。损坏或拒绝访问的外置模块会报告错误，不静默忽略。`PawnIOLib.dll` 从 PawnIO 系统安装目录加载，不依赖工作目录或开发环境 PATH；MSVC 运行库静态链接，无需安装 .NET / VC++ Redistributable。

此外，`bin/gpu` 包含 GPU 桥接 DLL 及 MPL / LGPL 许可证，`bin/presentmon` 和 `bin/voltide` 分别保存相应 MIT 许可证与中文来源说明。`bin/game` 包含内置游戏显示 DLL、Vulkan Layer 清单及相应第三方许可证，无需 RTSS 或目标机器上的 Vulkan SDK。便携发布包另外附带 `README.md`、`CHANGELOG.md` 和完整 `docs` 目录。

安装目录的 `bin` 下还保存说明文档、卸载用安装器及升级备份。卸载只移除本程序登记的文件，保留备份和用户偏好。

## 构建与验证

需要 Visual Studio 2022 / 2026 C++ 桌面开发组件、Windows SDK 和匹配版本的 CMake（VS 2026 需要 CMake 4.2+）。

```powershell
Set-Location .\CpuOverlay
.\build.ps1
# 无需管理员权限或 PawnIO 的算法测试：
cmake --build build --config Release --target CpuOverlaySensorTests CpuOverlaySettingsTests CpuOverlayBatteryTests CpuOverlayPumpTests
cmake --build build --config Release --target CpuOverlayGpuTests CpuOverlayFrameRateTests
cmake --build build --config Release --target CpuOverlayVendorBatteryTests
ctest --test-dir build -C Release --output-on-failure
# 无驱动环境下验证发布文件、依赖、绘图和操作系统指标：
.\scripts\verify-release.ps1 -SkipHardware
# 在管理员 PowerShell 中验证真实传感器：
.\scripts\verify-release.ps1
```

构建脚本首次从官方 GitHub 下载固定版本 PawnIO 安装器，并校验 SHA-256 和 Authenticode 签名；随后可复用缓存。四个模块固定为官方 0.2.11 版本，附来源、校验和及 LGPL 许可证。发布验证同时检查外置、主程序内嵌和安装器内嵌模块的一致性。无需另装安装包编译器。

GPU 桥接库随 `bin/gpu` 发布，固定哈希并附 MPL/LGPL 许可证；它是 NativeAOT 构建，不需要安装 .NET。PresentMon 静态链接到主程序，MIT 许可证位于 `bin/presentmon`。便携使用时请保留整个 `bin` 目录。构建与验证脚本在找不到正式版 VS 时会自动检测 Insiders/Preview。

Voltide 协议改写为原生 C++，其 MIT 许可证及固定来源说明随 `bin/voltide` 发布，不引入 .NET 或其他 HID DLL。

游戏显示所需 MinHook、Vulkan-Headers 和 SPIR-V 着色器已按固定版本随源码保存，正常构建不联网。来源与许可见 `third_party/game_overlay/README.md`；仅重新获取这些源码时需要运行 `python scripts/fetch-game-overlay.py`。四个后端的基础像素回读及独立进程接入探针见[内置游戏显示](GAME_OVERLAY.md)，这些探针不代表实际游戏兼容性验证。

完整硬件验证要求温度和功耗都可用；缺少功耗计量的旧平台可以用 `--diagnostics` 检查温度及来源，命令返回 3 表示存在缺测项。`pawnio_status` / `pawnio_hresult` 保留原后端状态；若 `power_source` 为 `windows_energy_meter_package0`，最终功耗由系统回退提供，不能再用 MSR 原始值复算。`energy_meter_milliwatts` 记录独立的系统计量读数。

验证脚本检查发布 EXE 和游戏 DLL 的版本资源、英文语言、静态运行库依赖、Intel / AMD 外置模块和内嵌模块的哈希、含游戏模块的安装器载荷、真实六项采样与原始 MSR / SMN 计算、透明黄色横排绘图，并用仅包含系统目录的 PATH、异于 EXE 的工作目录运行发布程序。`-SkipHardware` 会明确将温度 / 功耗实测标记为跳过，不代表硬件读取验证通过。

自动测试覆盖传感器换算、配置读写与异常数据、DPI 和屏幕边界、横纵绘制、颜色 / 透明度，以及真实 Windows 热键注册中的冲突、取消和切换。设置测试还覆盖 15 项名称独立保存、V1–V7 配置迁移、四后端开关、中文 / 英文 / 标点名称、长名称与数值列对齐，并通过实际对话框验证改名、取消、恢复默认、预览和应用。桌面集成测试可构建 `CpuOverlayProbe` 目标，在默认顶部居中布局下使用已配置快捷键检查焦点和穿透点击。测试工具不发布。

## 程序属性

产品 CpuOverlay，版本 1.12.0.0，CompanyName `sixunited`，Author `sclliang`。作者及版权仅保存在文件元数据，不在程序界面显示。

- 主程序 FileDescription：`System Performance, Battery and Cooling Display Monitor`
- 安装器 FileDescription：`CpuOverlay Installer with PawnIO Driver Setup`
- OriginalFilename 分别为 `CpuOverlay.exe`、`CpuOverlay-Setup.exe`。
- `Copyright (c) 2026 sixunited. Author: sclliang`
- VERSIONINFO：English (United States)，数字版本与字符串版本一致。
