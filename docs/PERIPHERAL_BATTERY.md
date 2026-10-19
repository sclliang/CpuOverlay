# 键盘和鼠标电量（1.5.0.0）

原项目显示六项系统指标；本次追加键盘和鼠标电量，原有字体、颜色、位置、横纵排列、热键及登录启动设置沿用。

## 设备和协议证据

- Razer Viper V3 HyperSpeed：VID 1532 / PID 00B8，MI_00、UsagePage 1 / Usage 2、Feature 长度 91。读电量请求为 transaction 1F / class 07 / command 80 / data size 02。响应状态必须成功，事务、命令、长度和 XOR 校验一致；arguments[1] 是 0–255 电量，换算百分比。实现参考 [OpenRazer](https://github.com/openrazer/openrazer/blob/master/driver/razerchromacommon.c) 的报文定义和 [鼠标型号分派](https://github.com/openrazer/openrazer/blob/master/driver/razermouse_driver.c)，独立实现本项目所需的只读报文。
- VGN V98Pro V4 1K：VID 391D / PID 2A04，UsagePage FF60 / Usage 61，Input/Output 长度 64，Report ID 09。查询 payload 为 87 00 00 01 00 00 加零填充和校验。63 字节 payload 的最后一字节是 `255 - ((ReportID + 其余字节和) % 256)`。回复须匹配命令、参数、分包号、数据长度及校验，电量范围 0–100，状态高半字节表示充电。
- VGN 来源为 [官方 V HUB](https://hub.vgnlab.com/) 发布的 `assets/index-CrLjahYi.js`：设备表 `14621:10756`、`bykeyboard` 的 `getBatteryStatus()` / `encode(135)`，以及 HID 报告描述符。未执行网页中的配置、恢复或固件更新逻辑，未复制整个 SDK。
- 接入前本机只读验证：鼠标响应 `... 02 1F ... 02 07 80 00 7C ... F9 00`，原始 124，约 48.6%；键盘响应 `09 87 00 00 01 00 02 0E 00 ... 5E`，14%、未充电。首次键盘未响应，人工按键唤醒后成功。此为当时读数，不是固定展示数值。

## 生命周期与异常

独立后台线程每 30 秒查询；失败或未接入时 5 秒后再查。UI / PDH 线程只读取快照。隐藏时停止发起新查询并取消未完成 I/O；退出时取消并收尾。所有报告进行长度与校验验证，异步无关消息不当作电量。

缓存只存在内存，第一次失败为未知；已成功后失败保留上次值和原时间戳，标注未更新或接收器断开。超过 35 秒未成功也标记过期。换接收器路径会清除旧缓存，重新接入后必须以真实回复更新。原始零电量是有效数值，超时绝不转成零。充电标志只显示在有效新读数上。

只有上述精确 PID 和集合启用查询，不扫描其他设备的私有命令。没有配对、刷写、灯光或按键配置命令，也不安装后台服务。不同型号/有线/蓝牙接口需要独立适配。

## 验证

`CpuOverlayBatteryTests` 使用接入前捕获的报文作为数据，覆盖换算、CRC、错误命令/事务、短包、忙状态、非法范围、真零、充电状态、过期/断开及恢复。设置测试覆盖八项渲染及缓存状态文字；`--battery-diagnostics` 用最终 EXE 直接查询硬件，可在无需 PawnIO/管理员权限的终端运行。

2026-10-19 发布版实测：键盘未响应时返回未知和超时错误 1460，鼠标正常读取 48.6%；用户按键唤醒后，同一 EXE 读取键盘 20%、正在充电，鼠标 48.6%，两项均为 fresh，退出码 0。见本机构建产物 `artifacts/battery-diagnostics-awake.json`。三组自动测试全部通过；发布检查核对版本属性、载荷、运行依赖、仅系统 PATH / 非程序工作目录启动及透明黄色横排绘图。本轮未在管理员进程中重测原有 PawnIO 温度、功耗。
