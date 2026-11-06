# Voltide：键鼠电量协议参考

- [上游项目](https://github.com/sdparklab/Voltide)
- 固定提交：[`6f36629b415942ad49305190604382644f7240dc`](https://github.com/sdparklab/Voltide/tree/6f36629b415942ad49305190604382644f7240dc)，获取于 2026-11-06。
- 版权：Copyright (c) 2026 Joko and contributors。
- 许可证：上游 MIT 许可证原文保留为 `LICENSE.txt`，发布时与本文一起保存至 `bin/voltide`。

CpuOverlay 将键鼠协议报文移植到原生 C++ 文件 `src/vendor_battery_protocol.h` 和 `src/peripheral_battery.cpp`，不依赖 Voltide 的 .NET 界面或 HID 互操作层。测试中的原始捕获回复保留 Voltide 来源标注。

## 移植范围

罗技 HID++ 2.0 接收器、明确列入的 23 个雷蛇无线接收器 PID、迈丛 K99 v2 / G75 Pro（3554:FA09）、K7 V2 Ultra+（3837:1014）、A5 系列 FF01 Feature 通道、ROG OMNI / AZOTH EXTREME（0B05:1ACE）、IMECOO P1（3554:FB34）、ATK A9 Air（373B:1278）、泰度 TSG809（A8A5:2255）。原有 VGN 和源于 PeriPower 的 K75M 路径继续保留。当前不提供耳机或手柄显示项。

## 校验与差异

CpuOverlay 对已定义的响应校验和进行验证，拒绝请求回显；通过 HID++ 配对设备类型分类，使用 software ID `0x0C`。雷蛇电量始终由 0–255 换算为百分比，即使原始值低于 100 也使用同一规则。迈丛 A5 仅在收到匹配中断 ACK 后读取 Feature，离线零值与其他协议的真实零电量分开处理。

除 K75M 与 Viper V3 HyperSpeed 外，其他移植协议已通过协议及捕获报文测试，尚未在开发者本机逐型号实测。共用接收器标识不代表同品牌所有型号均兼容。具体匹配规则与边界见[外设兼容清单](../../docs/PERIPHERAL_BATTERY.md)。
