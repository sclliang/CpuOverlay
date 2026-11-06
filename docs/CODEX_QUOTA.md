# Codex 余量监控

[返回项目首页](../README.md) · 适用于 1.10.0.0

在设置中独立勾选“Codex 短期余量 (5h)”和“Codex 长期余量 (7d)”，应用后与 CPU 等指标一起显示。支持横向、纵向、排序、字体和间距设置。显示的是账户剩余百分比：`100 − usedPercent`，不是 Token 数或 API 余额。

## 本机准备与显示含义

1. 安装 Codex 桌面应用或原生 Windows Codex CLI，并在其中登录 ChatGPT。
2. 打开 CpuOverlay 设置，勾选需要的 Codex 余量项，点击应用。
3. 从托盘“Codex 余量详情…”查看本地重置时间、方案和最后成功更新的时间。

通常短期周期为 5 小时，长期周期为 7 天；悬浮窗标签为 `Codex 5h` / `Codex 7d`，不附加“余量”文字。标签使用服务器实际返回的时长，例如接口返回 15 分钟时，显示 `Codex 15m`。仅返回一个长期周期时，将其放在长期显示项，短期保持“无额度数据”。不会把缺失的周期、登录失败或网络故障当成 0% / 100%。

| 显示 | 含义 |
| --- | --- |
| `82%` | 上次成功查询的剩余额度 |
| `0%` | 服务确实返回已用 100% |
| `--%(读取中)` | 等待首次查询 |
| `--%(未安装 Codex)` | 未找到可运行的原生 Codex EXE |
| `--%(需登录 ChatGPT)` | 本机尚未登录、凭据失效，或仅使用 API key 登录 |
| `--%(无额度数据)` | 服务未返回这个周期的额度 |
| `42%(未更新)` | 临时查询失败或超过 3 分钟未成功更新，显示最后已知值 |
| `--%(待更新)` | 已知周期已到重置时间，等待重新查询 |
| `--%(读取失败)` | 没有可用缓存，查询失败或返回数据无效 |

每 60 秒查询一次。隐藏悬浮窗、关闭两个 Codex 项或休眠时取消正在执行的查询并暂停；重新显示或恢复后立即查询。硬件刷新设置不改变额度查询频率。预览区数字为示例；“桌面预览”使用真实查询数据。

## 数据来源与实现

参考 [codex-quota-widget](https://github.com/stupdada/codex-quota-widget) 的本机 Codex 查询方式，使用原生 C++ / Win32 管道启动 `codex.exe app-server --listen stdio://`，完成 `initialize` / `initialized` 握手，通过 `account/read` 检查登录，再调用 `account/rateLimits/read`。协议见 [OpenAI Docs：Codex App Server](https://learn.chatgpt.com/docs/app-server#6-rate-limits-chatgpt)。

优先读取 `rateLimitsByLimitId.codex`，兼容旧版 `rateLimits`。只接受 Codex 账户额度桶，不用其他模型的额度代替；按返回周期长度归入短期 / 长期项。重置时间使用 Unix 秒数，详情中转为本地时间。

可执行文件依次从 `CODEX_CLI_PATH`、Codex 桌面应用的 `%LOCALAPPDATA%\OpenAI\Codex\bin`（含版本子目录）、默认 npm 安装目录内的原生 EXE，以及 PATH 中的绝对目录查找。`CODEX_CLI_PATH` 必须指向现有的绝对 EXE 路径。不运行 `.cmd` 或 shell，不从当前工作目录自动加载程序。

查询使用独立线程，不阻塞界面、CPU 采样或水泵刷新。子进程无控制台窗口，限制继承句柄，使用 Job Object 管理进程树；单次查询超时 12 秒，输出上限 1 MiB，关闭、暂停及错误路径均清理子进程。登录和凭据刷新交给本机 Codex；本程序不读取凭据文件，不保存访问令牌、账户标识或用量历史。诊断只输出余量和状态。

## 诊断与验证

```powershell
.\package\CpuOverlay.exe --codex-diagnostics D:\quota.json
cmake --build build --config Release --target CpuOverlayCodexTests CpuOverlaySettingsTests
ctest --test-dir build -C Release --output-on-failure
```

额度诊断无需管理员权限，不影响正在运行的实例。返回 0 表示已获取额度，3 表示未安装、未登录或额度不可用，2 表示输出文件错误。

自动测试使用本地模拟 app-server，覆盖新旧格式、余量换算、真实零值、缺失或错误字段、单周周期、周期归位、重置过期、连续 JSON 解析、分片管道、通知、登录失败、子进程退出、输出上限、超时、取消、缓存标记和进程清理。设置测试覆盖 V1–V5 迁移、原 15 项顺序、新 17 项顺序及显示开关持久化。
