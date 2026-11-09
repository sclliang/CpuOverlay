# 内置游戏显示依赖

这些源码在构建时编译进 CpuOverlayGame64.dll。用户无需安装 RTSS、Dear ImGui、MinHook 或 Vulkan SDK；Vulkan 运行库由显卡驱动提供。

- MinHook v1.3.4：`TsudaKageyu/minhook`，BSD 2-Clause。仅用于用户态图形接口拦截。
- Vulkan-Headers v1.3.296：`KhronosGroup/Vulkan-Headers`，许可见 `LICENSE-Vulkan-Headers.txt`。
- Dear ImGui v1.91.9b：仅复用 Vulkan 后端的两段 SPIR-V 着色器字节码，MIT 许可证见 `LICENSE-imgui.txt`，未引入 ImGui 界面。

固定标签、下载地址和源码归档 SHA-256 记录在 `pins.json`。`scripts/fetch-game-overlay.py` 可重新获取这些固定版本；正常构建及运行不联网。

产品作者、公司和版权嵌入本项目的 EXE/DLL 版本资源：Author `sclliang`；Company `sixunited`；Copyright `Copyright (c) 2026 sixunited. Author: sclliang`。第三方原始版权及许可保持不变。
