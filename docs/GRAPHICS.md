# GPU、前台 FPS / 1% Low 和游戏置顶

[返回项目首页](../README.md) · 适用于 1.12.0.0

实现参考本机 `D:\Source\WinMonitor`，GPU 在系统采样工作线程中整合，FPS 使用独立连续采集线程，UI 读取快照。信息刷新间隔可设为 200–5000 毫秒，默认 1000；PresentMon 每 100 毫秒处理帧事件，避免慢速界面刷新丢失帧历史或改变 1% Low。隐藏及系统休眠时关闭 PresentMon ETW 会话，停止完整 GPU 采样；已启用的水冷温度采样保持每秒更新。重新显示后重新建立 ETW 会话，不重放隐藏期间的事件。

## GPU

DXGI 排除软件适配器，选择独立显存容量最大的硬件显卡。在混合显卡系统中，所有利用率、温度、频率、功耗和显存必须对应同一适配器。Windows GPU Engine 按该适配器 LUID 过滤，先汇总同一物理引擎的进程负载，再取最繁忙引擎；失败时使用匹配名称的 GPU Core 负载。

温度、频率和功耗复用 WinMonitor 固定的 LibreHardwareMonitor NativeAOT 桥接构建，原始库 SHA-256 为 `696D0C1A178013D88450093E61648C2FF399E4D0941F9B97B18165EA9AD35E61`。它随 `bin/gpu` 安装，不要求 .NET。只选对应名称的 GPU 硬件传感器；核心/封装温度优先于 Hot Spot，不把显存、VR 或其他显卡的温度显示为核心温度；封装/板卡功耗优先于局部核心轨。没有有效传感器则显示 `--`，不估算。

显存总量来自 DXGI，已用量来自 Windows `GPU Adapter Memory(*)\Dedicated Usage`，按同一 LUID 过滤。悬浮窗仅显示已用量，总容量保留在诊断中。它统计适配器的所有进程用量；没有使用仅反映调用进程显存的 `QueryVideoMemoryInfo.CurrentUsage` 代替整卡用量。GB 显示按 1024³ 字节换算。桥接来源、原始源码版本和许可见 `third_party/librehardwaremonitor`。

## FPS

使用从 WinMonitor 引入的 PresentMon v1.9.2 PresentData 子集，MIT 许可证随包分发。保持系统级 ETW 相关事件采集以完成 Present 关联，最终只选前台进程，按交换链分别估计，取最繁忙的一条，不累加多个交换链、不使用后台或桌面合成器代替前台。

估计器按事件原始 QPC 时间戳，在最新帧之前两秒窗口内计算 `(样本数 - 1) / 时间跨度`，至少三帧且跨度至少 100 毫秒；连续三秒未交付向前推进的新时间戳，或最新事件绝对年龄超过六秒后失效。乱序、重复事件不会延长有效期。FPS 表示应用提交速率，可能与实际显示帧或插帧统计不同。

1% Low 对同一个前台进程、同一条选中交换链计算。保留最新帧前 10 秒、最多 32768 个时间戳，至少 100 个有效帧间隔后，将帧间隔按耗时降序排列，取 `ceil(间隔数 × 1%)` 个最慢间隔，以其平均耗时的倒数换算 FPS。它不是“平均 FPS 的 99%”或屏幕刷新率；不足样本显示 `--`，暂停或切换前台进程后清空并重新积累。切换时也丢弃切换之前的排队 ETW 事件，避免把后台提交帧带进新统计。

ETW 会话名固定为 `CpuOverlayFps_<PID>_<GUID>`，清理逻辑仅处理本程序格式的失活会话，不处理 WinMonitor 或其他工具的会话。启动失败保留 Win32 错误，30 秒后重试，避免错误循环。桌面、当前程序的设置窗口、静止或没有有效 Present 事件的前台程序显示 `--`，不显示刷新率或虚构零值。

## 游戏置顶

WinEvent 前台切换通知触发立即检查，500 毫秒定时器处理其他置顶窗口重新盖住悬浮条的情况。只有可见悬浮条被相交窗口盖住、或置顶样式丢失时才调用 `SetWindowPos(HWND_TOPMOST)`；使用 `SWP_NOACTIVATE`、`SWP_NOMOVE` 和 `SWP_NOSIZE` 保持焦点与位置。忽略不可见、最小化、cloaked 窗口；本程序自己的菜单/设置位于上方时不抢回层级。隐藏及拖动定位时不执行恢复。正常采样绘制只更新位置和像素，不周期性抢占 Z 序。

上述桌面浮窗路径适用于窗口化及无边框全屏，无法保证覆盖真正的独占全屏。1.12 新增内置 D3D11、D3D12、OpenGL、Vulkan 游戏内显示模块，复用当前透明监控画面，并在确认实际绘制后切换桌面浮窗显隐。实现、使用及验证边界见 [GAME_OVERLAY.md](GAME_OVERLAY.md)。锁屏和 UAC 安全桌面不提供覆盖。

## 验证

```powershell
cmake --build build --config Release --target CpuOverlayGpuTests CpuOverlayFrameRateTests CpuOverlayGraphicsProbe CpuOverlayTopmostProbe
ctest --test-dir build -C Release --output-on-failure
# 管理员终端：真实 D3D11 提交帧与 PresentMon 采样比较，包含 5 秒刷新和历史保留检查
.\build\Release\CpuOverlayGraphicsProbe.exe
# 运行中的 CpuOverlay 悬浮条可见时，验证连续三次遮挡后恢复且焦点/位置不变
.\build\Release\CpuOverlayTopmostProbe.exe <CpuOverlay进程ID>
.\package\CpuOverlay.exe --graphics-diagnostics D:\Source\CpuOverlay\artifacts\graphics-diagnostics.json
```

2026-11-06 已实测 Radeon RX 9070 XT 核心温度、频率、GPU Package 功耗和整卡显存；管理员完整发布验证同时通过原有 CPU 温度/功耗及原始数据换算。真实 D3D11 集成测试已测得 PresentMon 143.988 FPS、独立提交参考 144.015 FPS；置顶三轮遮挡恢复、焦点、定位锚点和穿透样式通过。图形探针也要求 1% Low 积累有效结果，最新数值保存在 `artifacts/installation-verification.json`；协议、配置、GPU 和 FPS/1% Low 的七组自动测试通过。
