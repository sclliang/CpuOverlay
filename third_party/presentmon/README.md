# PresentMon 1.9.2：帧事件采集子集

本目录从 WinMonitor 集成的 PresentMon 代码引入。固定上游版本为 **v1.9.2**，提交 [`9e3ffa42bae83007f4bad5d1f1ce5b34fdf3bd54`](https://github.com/GameTechDev/PresentMon/tree/9e3ffa42bae83007f4bad5d1f1ce5b34fdf3bd54)，使用 MIT 许可证，原文见 `LICENSE.txt`；发布时保存为 `bin/presentmon/LICENSE.txt`。

## 集成内容

仅保留 `PMTraceConsumer` 所需的 PresentData 事件消费、元数据解码、ETW 事件描述和 GPU 关联代码。CpuOverlay 调整了 CMake 目标名称，并将自有实时 ETW 会话前缀设为 `CpuOverlayFps_`。

- `PresentData/TraceSession.*`：基于上游实现，移除 WinMR 提供程序支持。
- `PresentMonRealtime.*`：管理实时 ETW 会话与消费线程，提供完成的 Present 样本，筛选当前前台进程，并清理失活的 CpuOverlay 自有会话；不停止 WinMonitor 或其他程序的会话。
- `DebugStubs.cpp`：替代未使用的详细跟踪输出。Debug 构建保留分析断言，禁用未使用的文本转储，使 v1.9.2 代码适配当前严格 MSVC 编译规则。

命令行应用、CSV 输出、控制台界面、ETL 文件处理流程和 WinMR 消费代码未集成。

FPS 和 1% Low 的统计规则、生命周期及验证见[图形监控说明](../../docs/GRAPHICS.md)。
