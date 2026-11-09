# 内置游戏显示

[返回项目首页](../README.md) · 适用于 1.12.0.0

CpuOverlay 自带 64 位游戏内叠加模块，提供 D3D11、D3D12、OpenGL 和 Vulkan 四个后端。无需安装或启动 RTSS。首次实现已用独立图形探针验证基础绘制和进程接入，实际游戏兼容性按后续游戏使用逐款验证。

## 使用

1. 将整个便携目录解压，或使用安装器安装；保留 `bin/game` 中的 DLL、Vulkan 清单和许可文件。
2. 打开设置，在“游戏内显示（内置）”中启用所需接口。默认四项全部启用。
3. 先启动 CpuOverlay，再启动游戏。Vulkan 的 Layer 在创建实例时加载，因此已运行的 Vulkan 游戏需要重新启动。
4. 位置、显示器、字体、粗体/斜体、颜色、描边、不透明度、横纵排列及指标名称均在原有设置中调整。`Alt+F1` 或自定义快捷键同时控制桌面和游戏内显示。

仅在采集到前台程序的有效帧、窗口位于所选显示器、且游戏内显示已启用时尝试接入。自动排除浏览器、资源管理器、桌面合成器、开发工具和本程序等常见桌面程序。首版游戏模块支持 Windows x64 进程，32 位目标不会被加载模块。

确认模块近期实际绘制后，主程序隐藏同屏桌面浮窗，避免重叠；切回桌面或接入失败时继续显示桌面浮窗。拖动定位仍通过现有桌面定位模式完成，游戏内以同一锚点、逻辑像素偏移和 DPI 换算。游戏拒绝接入时保留桌面路径，不尝试绕过保护机制。

## 架构

主程序继续采集硬件和 PresentMon 帧数据，GDI+ 生成带预乘透明度的 BGRA 位图。通过当前会话、当前用户私有的共享内存与命名互斥锁传输。协议采用固定宽度字段、版本号、图像大小检查、画面代数、目标 PID、心跳、显隐与接口掩码。渲染回调以非阻塞方式读快照，主程序退出或心跳超过五秒后停止绘制。

- **D3D11**：在 DXGI Present/Present1 前上传纹理并绘制，保存恢复被修改的图形状态；ResizeBuffers 前释放相关缓存。
- **D3D12**：跟踪创建交换链时的 Direct 命令队列；对已运行目标保留已观测队列回退。每个后台缓冲区具有独立命令分配器、上传资源和 fence，显式执行资源状态转换。
- **OpenGL**：在 SwapBuffers 前绘制。现代上下文使用 GLSL 与 VAO；兼容旧上下文使用固定功能路径，并恢复渲染状态。
- **Vulkan**：通过自带 Layer 跟踪实例、设备、Win32 surface、交换链及队列。为每张交换链图像分配渲染资源、fence 和完成信号量；在呈现之前等待游戏的渲染信号、提交叠加绘制，再将呈现等待切换到本模块信号。

游戏模块在目标进程中保持加载；隐藏或退出 CpuOverlay 后回调处于无输出状态。GPU 资源在交换链重建/设备生命周期中释放，避免在 DLL 退出、显卡驱动已卸载后调用驱动。更新模块前需先关闭加载旧 DLL 的游戏。

Vulkan 的当前用户注册只操作本程序的清单项，不修改其他 Layer。退出、关闭 Vulkan 后端及卸载时清理；移动程序后重新启动会移除记录的旧清单位置。保护队列、仅呈现且无图形能力的队列或缺少颜色附件用途的交换链不会被修改。

## 诊断与基础验证

设置底部显示等待游戏、当前接口、64 位限制或拒绝接入状态。隐藏控制窗口标题包含 `game_enabled`、`game_active`、`game_pid`、`game_api`、`game_frames`、`game_error`、`game_render_error`、`vulkan_error`，用于后续兼容性排查。

```powershell
cmake --build build-native --config Release --target CpuOverlayGameProbe
.\build-native\Release\CpuOverlayGameProbe.exe D3D11 .\package\bin\game\CpuOverlayGame64.dll
.\build-native\Release\CpuOverlayGameProbe.exe D3D12 .\package\bin\game\CpuOverlayGame64.dll
.\build-native\Release\CpuOverlayGameProbe.exe OpenGL .\package\bin\game\CpuOverlayGame64.dll
.\build-native\Release\CpuOverlayGameProbe.exe Vulkan .\package\bin\game\CpuOverlayGame64.dll
.\build-native\Release\CpuOverlayGameProbe.exe Remote .\package\bin\game\CpuOverlayGame64.dll
```

图形探针使用独立测试通道，不读取或覆盖用户监控配置，不安装驱动，也不向真实游戏加载模块。Vulkan 探针在自身进程中显式启用随包清单。前三个 API 的探针回读已绘制像素，Vulkan 在重新获取已呈现图像后回读像素；Remote 路径验证主程序工作线程向自建子进程加载模块并完成绘制。

四个接口具备基础绘制路径，不代表所有游戏、HDR、插帧、多队列、多交换链及其他叠加层组合均已完成兼容验证。
