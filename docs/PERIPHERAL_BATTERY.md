# 键盘和鼠标电量

[返回项目首页](../README.md) · 适用于 1.9.2.0

多接收器查询全部失败时保留上次成功设备的身份、读数和时间戳；无关接收器的失败不会清空缓存或替换设备名称。断开判断针对上次设备的接口，即使其他品牌接收器仍连接，也会显示原设备已断开。只有另一个设备成功返回电量时才切换来源。

原项目显示六项系统指标；本次追加键盘和鼠标电量，原有字体、颜色、位置、横纵排列、热键及登录启动设置沿用。

## 设备和协议证据

- 1.8.0.0 对照本机 `D:\Source\PeriPower` 的 `LogitechBatteryReader.cs`、`RazerBatteryReader.cs` 及 `Protocols.cs` 移植为原生 C++/Win32 实现。无 .NET、HIDSharp 或第三方 HID DLL 依赖。
- 罗技 HID++：VID 046D、UsagePage FF00 / Usage 02、Output 20 字节的长报告集合。依次 ping 配对槽位 1–6，验证 HID++ 2.0+ 和回显 cookie，通过 0x0005 读取名称及设备类型；类型 0/2 对应键盘/数字键盘，3/4/5 对应鼠标/触控板/轨迹球，禁止按 Bolt PID 猜成 M750M 或把键盘当鼠标。K75M 通过 Bolt 046D:C548 实测。优先查询 Unified Battery 0x1004 能力及状态，缺少该特性时尝试 0x1000。0x1004 只有声明百分比能力时才接纳百分比；0x1000 的零值表示未知。充电状态 1/2 为充电，3 为充满，4 为错误，不显示为充电。协议参考 [Logitech HID++](https://github.com/Logitech/cpg-docs)、[设备类型定义](https://github.com/pwr-Solaar/Solaar/blob/master/lib/logitech_receiver/hidpp20_constants.py) 及 [Linux HID++ 实现](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-logitech-hidpp.c)。
- CpuOverlay 的 HID++ software ID 为 0x0C，与 PeriPower/Voltide 的 0x0E、G HUB 常用的 0x0D 分开；回复同时核对槽位、特性、函数和 software ID。成功槽位及名称按键盘/鼠标分别缓存在当前运行中，失败后下次重新发现，绝不修改接收器配对。
- Razer Viper V3 HyperSpeed：VID 1532 / PID 00B8，MI_00、UsagePage 1 / Usage 2、Feature 长度 91。读电量请求为 transaction 1F / class 07 / command 80 / data size 02。响应状态必须成功，事务、命令、长度和 XOR 校验一致；arguments[1] 是 0–255 电量，换算百分比。实现参考 [OpenRazer](https://github.com/openrazer/openrazer/blob/master/driver/razerchromacommon.c) 的报文定义和 [鼠标型号分派](https://github.com/openrazer/openrazer/blob/master/driver/razermouse_driver.c)，独立实现本项目所需的只读报文。
- VGN V98Pro V4 1K：VID 391D / PID 2A04，UsagePage FF60 / Usage 61，Input/Output 长度 64，Report ID 09。查询 payload 为 87 00 00 01 00 00 加零填充和校验。63 字节 payload 的最后一字节是 `255 - ((ReportID + 其余字节和) % 256)`。回复须匹配命令、参数、分包号、数据长度及校验，电量范围 0–100，状态高半字节表示充电。
- VGN 来源为 [官方 V HUB](https://hub.vgnlab.com/) 发布的 `assets/index-CrLjahYi.js`：设备表 `14621:10756`、`bykeyboard` 的 `getBatteryStatus()` / `encode(135)`，以及 HID 报告描述符。未执行网页中的配置、恢复或固件更新逻辑，未复制整个 SDK。
- 接入前本机只读验证：鼠标响应 `... 02 1F ... 02 07 80 00 7C ... F9 00`，原始 124，约 48.6%；键盘响应 `09 87 00 00 01 00 02 0E 00 ... 5E`，14%、未充电。首次键盘未响应，人工按键唤醒后成功。此为当时读数，不是固定展示数值。

## Voltide 扩展清单

固定参考 [Voltide 提交 6f36629](https://github.com/sdparklab/Voltide/tree/6f36629b415942ad49305190604382644f7240dc)，协议移植为 C++，保留 MIT 许可证及来源说明至 `bin/voltide`。本轮只扩展键盘和鼠标，不引入耳机或手柄显示。

| 协议/设备 | 严格匹配 | 读取及校验 |
| --- | --- | --- |
| 罗技 G304、GPW3、M750M、Unifying/Bolt 的 HID++ 2.0+ 设备 | 046D，FF00:02，Input 20–64 / Output 20 | 读取配对槽位实际名称和类型，再读 0x1004 / 0x1000；旧 HID++ 1.0 不冒充支持 |
| 雷蛇无线鼠标 | 1532 的 23 个明确 PID、Feature 至少 91 字节 | PID 列表见 `RazerWirelessPid()`；毒蝰/炼狱蝰蛇/巴塞利斯蛇、Cobra/Naga/Pro Click 等相关接收器，完整事务、状态、命令和 XOR 校验；未知 PID 不探测 |
| 迈丛 K99 v2 / G75 Pro | 3554:FA09，FF02，Input/Output 20 | report 13、command 4A、长度 02、模 256 校验；拒绝查询回显 |
| 迈丛 K7 V2 Ultra+ 鼠标 | 3837:1014，MI_02、FF01:01、Input/Output 64 | 4D / 0900 查询；验证返回鼠标 3837:4026、连接状态、XOR 和零填充 |
| 迈丛 A5 V3 协议 | 3837、排除 1014，FF01、Feature 65 | 只读 bond-status / OST 查询；收到匹配 ACK 后才 GetFeature，接受普通/取反数据银行，验证 VID、非空 PID 及百分比 |
| ROG AZOTH EXTREME / OMNI | 0B05:1ACE，FF00、Input/Output 64 | 02 12 01 状态查询，读取第 6 字节，拒绝全零回显及越界百分比 |
| IMECOO P1 | 3554:FB34，FF02、Input/Output 17 | 08 04 查询，状态/长度字段与 4D 模和校验；不使用 FF04 中看似 100% 的容量常数 |
| ATK A9 Air 大师+ | 373B:1278，FF02、Input/Output 17 | 与 CompX 17 字节协议一致，优先 SetOutputReport；不匹配 1262 耳机 |
| 泰度 TSG809 | A8A5:2255，FF01:10、Input/Output 65 | 55 30 查询、AA 30 回应，验证固定头、百分比和充电标志 |

除本机 K75M 和 Viper V3 HyperSpeed 外，上表新增型号覆盖的是原项目协议/报文移植和自动测试，尚未逐型号实机验证。3554:FA09 被不同品牌共用，不据此声称支持狼蛛；ROG OMNI 共享配对接口按 AZOTH 键盘状态协议处理，不能保证同接收器下其他型号的分类。蓝牙、有线替代 PID、仅四档电量和未列入的接口不会猜测成真实百分比。每类显示首个成功匹配的设备，诊断文件记录实际名称及 VID/PID。

## 生命周期与异常

独立后台线程每 30 秒查询；失败或未接入时 5 秒后再查。UI / PDH 线程只读取快照。隐藏时停止发起新查询并取消未完成 I/O；退出时取消并收尾。所有报告进行长度与校验验证，异步无关消息不当作电量。

雷蛇每次完整查询先 SetFeature，再等待 100 毫秒后 GetFeature；忙碌、I/O 失败或无效回复时等待 160 毫秒重发完整查询，最多三次。接纳 Windows HID 返回的 90/91 字节传输计数，同时仍核对完整 91 字节缓冲区中的零 Report ID、状态、事务、命令和校验。

雷蛇事务号按型号采用 1F / 3F / FF，与 OpenRazer 的电量查询分派一致。可充电型号另用只读 07/84 命令查询充电状态，失败不丢弃已成功读取的百分比；AA 电池型号不发送该命令。07/80 的原始值始终从 0–255 换算，不沿用 Voltide 中“原始值不超过 100 就当百分比”的猜测。

每类设备发现有 18 秒预算，HID 操作均有独立超时并可取消。先采鼠标；鼠标优先尝试具体协议，避免不相关的罗技配对槽位拖延已连接雷蛇等设备。诊断最多等待 40 秒，也会在两项首次查询结束后提前返回。VGN 原路径继续保留。键盘先尝试罗技，失败再尝试其他匹配设备。

缓存只存在内存，第一次失败为未知；已成功后失败保留上次值和原时间戳，标注未更新或接收器断开。超过 35 秒未成功也标记过期。换接收器路径会清除旧缓存，重新接入后必须以真实回复更新。原始零电量是有效数值，超时绝不转成零。充电标志只显示在有效新读数上。

仅对清单中明确的 VID/PID 或标准 HID++ 能力集合启用对应只读协议，没有配对、刷写、灯光或按键配置命令，也不安装后台服务。新增鼠标协议同样拒绝短包、错误校验、请求回显、非法范围及离线哨兵。A5 的零表示离线；其他支持真零的协议仍保留真实零电量。

## 验证

2026-11-06：参考 PeriPower 及移植后的原生 EXE 均实测 K75M 65%、雷蛇原始 21/255（约 8.2%，界面 8%），两项均为 fresh，诊断退出码 0，见 `artifacts/peripower-reference.json`、`artifacts/battery-port.json`。电量协议测试 49 项检查通过，包含 HID++ 短/长报文、软件标识隔离、错误匹配、百分比能力、零值和充电错误状态。

扩展后实测 K75M 60%、Viper V3 HyperSpeed 8.2%，两项 fresh、退出码 0，见 `artifacts/battery-voltide.json`。`CpuOverlayVendorBatteryTests` 使用 Voltide 提供的 IMECOO/K99 等捕获及协议用例，覆盖 CompX/K7 的校验、A5 的两种数据银行和离线哨兵、ROG 的电量/阈值区分、泰度标志、Razer PID 白名单和低电量缩放。

`CpuOverlayBatteryTests` 继续覆盖换算、CRC、错误命令/事务、短包、忙状态、非法范围、真零、充电状态、过期/断开及恢复。设置测试覆盖 15 项渲染、显示顺序的持久化/迁移和缓存状态文字；`--battery-diagnostics` 用最终 EXE 直接查询硬件，无需 PawnIO/管理员权限。

2026-10-19 发布版实测：键盘未响应时返回未知和超时错误 1460，鼠标正常读取 48.6%；用户按键唤醒后，同一 EXE 读取键盘 20%、正在充电，鼠标 48.6%，两项均为 fresh，退出码 0。见本机构建产物 `artifacts/battery-diagnostics-awake.json`。三组自动测试全部通过；发布检查核对版本属性、载荷、运行依赖、仅系统 PATH / 非程序工作目录启动及透明黄色横排绘图。本轮未在管理员进程中重测原有 PawnIO 温度、功耗。
