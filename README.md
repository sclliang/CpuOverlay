# CpuOverlay

Windows CPU 占用与频率悬浮监控工具。C++ / Win32 原生单 EXE，适用于 Windows 10 / 11 x64，无需安装 .NET、VC++ 运行库或第三方驱动。

```text
CPU  12.3% | FREQ  4.25 GHz
```

整行黄色、同一字号、横向排列，以 `|` 分隔。背景完全透明，文字有深色细描边；鼠标可穿透文字与整个窗口。默认右上角置顶，不抢输入焦点，不出现在任务栏或 Alt+Tab 中。

## 运行

双击 `package/CpuOverlay.exe`。第一次正常运行默认启用当前用户登录自启，程序常驻托盘，悬浮窗默认隐藏。

- **Alt+F1**：显示 / 隐藏。长按不会连续切换。
- **单击托盘图标**：显示 / 隐藏。
- **右键托盘图标**：位置（四个角）、字号（小 / 标准 / 大）、显示器、开机自启、使用说明、退出。
- 默认每秒更新一次；隐藏后暂停采样，重新显示约一秒后出现新读数。
- 多次启动只保留一个实例；再次双击 EXE 会显示已有悬浮窗。
- 若 Alt+F1 被其他软件占用，会明确提示；可通过托盘操作，释放占用后点击“重新注册 Alt+F1”。

托盘图标可能被 Windows 收在右下角 `^` 隐藏图标菜单中。鼠标穿透的悬浮窗不能直接拖动，请通过托盘菜单改变位置。

## 自动启动与卸载

这里的“开机自启”是 **当前用户登录桌面后启动**。这是用户桌面常驻程序，不是 Session 0 中的 Windows 服务；无需管理员权限。

启动项：`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\CpuOverlay`。

配置：`HKCU\Software\sixunited\CpuOverlay`，保存字号、位置、显示器和初始化标记。运行时不需要访问项目源代码、构建目录或外部资源。

首次正常运行会将当前 EXE 的绝对路径写入启动项；取消自启后不会在下一次启动时自行恢复。如果已开启自启后移动 EXE，手动运行新位置的 EXE 可更新启动路径（先退出原进程）。Windows 任务管理器中手动禁用的启动项仍需用户在任务管理器中重新启用。

卸载：在托盘取消开机自启，然后退出，最后删除 EXE。也可先运行 `CpuOverlay.exe --disable-startup` 和 `CpuOverlay.exe --exit`。注册表配置不影响系统，按需删除上述专用配置键即可。

## 数据口径与限制

- **CPU 占用**：优先使用 `Processor Information(_Total)\% Processor Time`，表示逻辑处理器平均忙碌时间占比，范围 0–100%。不可用时，单处理器组系统回退到 `GetSystemTimes`。
- **CPU 频率**：优先使用 `Processor Frequency × % Processor Performance / 100`，转换为 GHz，显示 Windows 计数器估算频率（包含性能比例变化）。不可用时回退到 `CallNtPowerInformation` 报告的各逻辑处理器频率平均值。回退值在部分硬件上可能固定。
- 频率是操作系统估算 / 报告值，不是每个核心的瞬时有效时钟；混合架构、采样时间和统计口径可导致其与任务管理器、CPU-Z 等不同。
- 无有效数据时显示 `--`，不以 0 冒充正常读数。
- 支持 DPI 缩放和显示器变化；选中的显示器断开时回退到主显示器。
- 普通桌面置顶窗口不能保证覆盖独占全屏游戏，也不会覆盖锁屏或 UAC 安全桌面。游戏可尝试无边框窗口模式。
- 按秒采样，不写历史监控日志，不连接网络。

## 构建

要求 Visual Studio 2022 或 2026 的“使用 C++ 的桌面开发”、Windows SDK、支持该 VS 版本的 CMake（VS 2026 需要 CMake 4.2+）。

```powershell
cd D:\Code\CpuOverlay
.\build.ps1
```

默认构建 Release x64，产物为 `package/CpuOverlay.exe`。采用静态 MSVC 运行库，只动态链接 Windows 系统 DLL，因此发布包无需 `bin` 文件夹。

修改源码后先退出正在运行的 EXE 再构建。图标已随源代码提供，需要重绘时执行 `scripts/make-icon.ps1`。

## 命令行

| 参数 | 用途 |
| --- | --- |
| 无参数 | 首次运行隐藏驻留；已有实例时显示悬浮窗 |
| `--show` / `--hide` / `--toggle` | 显示 / 隐藏 / 切换悬浮窗 |
| `--startup` | 登录自启使用，保持隐藏，不打扰已有实例 |
| `--exit` | 退出当前会话的已有实例 |
| `--enable-startup` / `--disable-startup` | 修改当前用户启动项后退出 |
| `--no-autostart` | 本次启动不自动写入启动项，供测试使用 |
| `--diagnostics <JSON绝对路径>` | 采集四个一秒样本并输出来源，不启动界面或修改自启 |
| `--preview <PNG绝对路径>` | 输出同一绘图路径生成的透明示例图，不启动界面或修改自启 |

诊断模式退出码：0=数据正常，2=文件或参数错误，3=至少一个数据项不可用。预览中的数字是示例数据。

## 验证

`scripts/verify-release.ps1` 检查版本字段、英文资源语言、数据采集、透明绘图及直接 DLL 依赖，并从临时工作目录、只含 Windows 系统路径的 PATH 环境运行发布 EXE。

可选桌面集成测试窗口：

```powershell
cmake --build build --config Release --target CpuOverlayProbe
.\package\CpuOverlay.exe --show --no-autostart
.\build\Release\CpuOverlayProbe.exe
```

在测试窗口按两次 Alt+F1，然后点击悬浮文字。窗口显示是否成功切换、是否维持焦点，以及下层窗口是否收到穿透点击。Alt+F4 关闭测试窗口。该测试程序不放入发布包。

## 程序属性

- 产品 / 原始文件名：CpuOverlay / CpuOverlay.exe
- 版本：1.0.0.0
- CompanyName：sixunited
- FileDescription：CPU Usage and Frequency Desktop Overlay
- Copyright (c) 2026 sixunited. Author: sclliang
- VERSIONINFO 资源语言：English (United States)

API 参考：[Windows 服务与桌面隔离](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services)、[分层窗口及鼠标穿透](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)、[全局热键](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)、[语言无关性能计数器](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhaddenglishcounterw)。
