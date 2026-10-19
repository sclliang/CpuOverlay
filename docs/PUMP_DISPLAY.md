# 乔思伯数显屏（1.6.0.0）

## 本机协议核对

安装的软件为 `C:\Program Files\JONSBO PC Monitor` 1.0.6。只读检查其 `resources/app.asar` 中 `main/constant/appConstant.js` 的 JONSBO 白名单及 `main/_baseClass/device_hid.js` 的发送逻辑；没有将厂商 JS、Electron、HWiNFO DLL 或 SDK 加入发布包。

本机枚举到 VID `5131` / PID `2007`，Product `FBB`，UsagePage `FF00` / Usage `01`，无 Report ID。实际 HID 描述符为：

```text
0600ff0901a101090215002600ff750895408106090215002600ff750895409106c0
```

这是 64 字节 Input / Output 的供应商定义设备。Windows 原生 WriteFile 缓冲区加上零 Report ID 前缀，共 65 字节。输出缓冲区 `[0..6]` 为 `[0, 0, 1, 2, 温度整数, 百分之一度, 0]`，最后的零表示摄氏度。其余字段清零，仅实现用户需要的 CPU 温度显示；没有泵速控制命令。温度只接受有限的 0–150℃，四舍五入至百分之一度，缺测不发送。

公开的 [JonsboLite 协议核对记录](https://github.com/danieyal/jonsbolite/blob/master/reverse-engineering/PROTOCOL.md) 也在同一 VID/PID 上确认了 Windows 65 字节输出和 200 ms 周期。本项目按实际安装的 1.0.6 报文布局独立实现，不使用其第三方程序或温度采集依赖。

## 后台生命周期

PumpDisplay 线程持有自己的 HID 写入句柄，GENERIC_WRITE / FILE_SHARE_READ 防止与另一个写入程序交错发送。每 200 ms 发送当前有效温度；单次写入超时 300 ms，支持取消并等待未完成 I/O 收尾。接口不存在或写入失败，5 秒后重试。驱动接受完整 65 字节即计为已发送；协议没有显示确认回复，所以 USB 写入成功不等于已经人工确认屏幕内容。

Application 的系统采样线程区分四种状态：可见时完整采样；隐藏且已连接屏幕时仅硬件温度/封装传感器；隐藏且无屏幕时停止采样；系统休眠时暂停。隐藏不会关闭 PumpDisplay。恢复显示仍按原行为重新预热 PDH 和键鼠查询。系统恢复会重新连接屏幕，避免沿用休眠前的温度。数据超过 3 秒失效，失效或缺失时停止发送；无法保证固件清除已显示的旧值。

默认功能没有配置项、额外窗口或成功提示，跟随 CpuOverlay 启动和退出。不会卸载、结束或修改乔思伯软件/计划任务。

## 检查入口

- `CpuOverlayPumpTests`：报文长度、字段位置、温度小数进位、非法/缺失值、真实零、有效期、隐藏/可见/无设备/休眠的采样策略。
- `--pump-diagnostics <JSON>`：只使用真实 PawnIO 温度，输出每秒累计成功报文数、连接状态、Win32 错误，并验证暂停后不再发送、恢复后继续发送。需要管理员权限读取 CPU 温度。普通 `--diagnostics` 和 `--preview` 不向屏幕写入。
- 运行时隐藏控制窗口标题提供 `overlay`、`pump`、`sent`、`temperature`、`last_sent_ms`、`error` 只读诊断，可核对悬浮窗隐藏时发送计数仍增长。

界面中原有的设置、热键、黄色横排样式和键鼠电量监控沿用。程序卸载不移除共享 PawnIO 驱动。

## 2026-10-19 本机验证

- 四组自动测试全部通过；发布验证通过版本资源、安装器载荷、系统 DLL 依赖、异目录/仅系统 PATH 启动及透明黄色绘图检查。
- `artifacts/pump-diagnostics.json`：真实 CPU 温度 37–47℃；前 5 秒成功发送 24 帧，暂停后不增长，恢复后累计 29 帧；Win32 错误为 0，诊断退出码为 0。
- 已安装到原位置 `C:\Users\sclli\AppData\Local\CpuOverlay.exe`，版本 1.6.0.0；与 `package/CpuOverlay.exe` SHA-256 一致。原有自启保留，乔思伯软件及其任务未修改。
- `artifacts/pump-runtime.json`：实际隐藏窗口状态下，3 秒内发送计数从 403 增至 418，温度由 33℃ 更新为 35℃；应用的暂停/恢复通知处理验证通过，显示配置字节未改变。这里模拟应用收到电源通知，没有让整台电脑真正睡眠或重启。
- 用户已确认：水泵数显屏正常显示 CPU 温度并持续更新。未进行物理拔插和完整重启测试。
