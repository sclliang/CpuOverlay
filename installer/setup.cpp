#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace {
constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\sixunited.CpuOverlay";
constexpr wchar_t kSettingsKey[] = L"Software\\sixunited\\CpuOverlay";
constexpr wchar_t kControllerClass[] = L"sixunited.CpuOverlay.Controller.1";
struct Options { fs::path target, report; bool silent = false, uninstall = false, launch = true, startup = true; };
struct Outcome { bool success = false, pawnPresent = false, pawnInstalled = false; std::wstring error; };
struct Failure { std::wstring message; };

std::wstring ErrorText(DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring result = text ? text : L"操作失败";
    if (text) LocalFree(text);
    return result + L" (" + std::to_wstring(code) + L")";
}
void Require(bool condition, const std::wstring& message) { if (!condition) throw Failure{message}; }
std::wstring Quote(const fs::path& path) { return L"\"" + path.wstring() + L"\""; }
std::wstring ReadRegistry(HKEY root, const wchar_t* key, const wchar_t* name) {
    wchar_t value[32768]{}; DWORD bytes = sizeof(value);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS) return value;
    return {};
}
void WriteRegistry(const wchar_t* keyName, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    auto status = RegCreateKeyExW(HKEY_CURRENT_USER, keyName, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    Require(status == ERROR_SUCCESS, L"无法建立安装注册信息。" + ErrorText(status));
    status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    Require(status == ERROR_SUCCESS, L"无法保存安装信息。" + ErrorText(status));
}
fs::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    Require(SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &value)), L"无法定位用户文件夹。");
    fs::path result(value); CoTaskMemFree(value); return result;
}
fs::path SelfPath() {
    wchar_t path[32768]{};
    Require(GetModuleFileNameW(nullptr, path, ARRAYSIZE(path)) > 0, L"无法定位安装程序。");
    return path;
}
bool PawnPresent() {
    const auto installed = ReadRegistry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO", L"InstallLocation");
    if (installed.empty() || !fs::exists(fs::path(installed) / L"PawnIOLib.dll")) return false;
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    SC_HANDLE service = OpenServiceW(manager, L"PawnIO", SERVICE_QUERY_STATUS);
    if (service) CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return service != nullptr;
}
fs::path DefaultTarget() {
    const auto registered = ReadRegistry(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation");
    if (!registered.empty()) return registered;
    const HWND window = FindWindowW(kControllerClass, nullptr);
    if (window) {
        DWORD id = 0; GetWindowThreadProcessId(window, &id);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
        if (process) {
            wchar_t path[32768]{}; DWORD length = ARRAYSIZE(path);
            const bool found = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
            CloseHandle(process);
            if (found && _wcsicmp(fs::path(path).filename().c_str(), L"CpuOverlay.exe") == 0) return fs::path(path).parent_path();
        }
    }
    const auto local = KnownFolder(FOLDERID_LocalAppData);
    if (fs::exists(local / L"CpuOverlay.exe")) return local;
    return local / L"CpuOverlay";
}
DWORD Run(const fs::path& executable, const std::wstring& arguments, bool wait = true) {
    std::wstring command = Quote(executable) + L" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
        executable.parent_path().c_str(), &startup, &process) != FALSE, L"无法运行 " + executable.filename().wstring() + L"：" + ErrorText(GetLastError()));
    CloseHandle(process.hThread);
    DWORD exit = 0;
    if (wait) {
        const auto status = WaitForSingleObject(process.hProcess, 180000);
        if (status != WAIT_OBJECT_0) { CloseHandle(process.hProcess); throw Failure{L"子程序仍未结束，请等待后重试。"}; }
        GetExitCodeProcess(process.hProcess, &exit);
    }
    CloseHandle(process.hProcess);
    return exit;
}
void StopOverlay() {
    const HWND window = FindWindowW(kControllerClass, nullptr);
    if (!window) return;
    DWORD id = 0; GetWindowThreadProcessId(window, &id);
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, id);
    if (!PostMessageW(window, WM_CLOSE, 0, 0)) { if (process) CloseHandle(process); throw Failure{L"无法关闭正在运行的 CpuOverlay，请从托盘退出后重试。"}; }
    if (process) {
        const auto status = WaitForSingleObject(process, 15000);
        CloseHandle(process);
        Require(status == WAIT_OBJECT_0, L"CpuOverlay 仍在退出，请稍后重新安装。");
    } else Sleep(1000);
}
void Extract(int id, const fs::path& destination) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    Require(resource != nullptr, L"安装包资源缺失。");
    const DWORD bytes = SizeofResource(nullptr, resource);
    const auto memory = LoadResource(nullptr, resource);
    const void* data = memory ? LockResource(memory) : nullptr;
    Require(data && bytes, L"无法读取安装包资源。");
    fs::create_directories(destination.parent_path());
    std::ofstream file(destination, std::ios::binary | std::ios::trunc);
    file.write(static_cast<const char*>(data), bytes);
    file.close();
    Require(file.good(), L"无法写入 " + destination.wstring());
}
class TempFolder {
public:
    TempFolder() {
        wchar_t temp[32768]{}; GetTempPathW(ARRAYSIZE(temp), temp);
        wchar_t unique[MAX_PATH]{};
        Require(GetTempFileNameW(temp, L"CPO", 0, unique) != 0, L"无法创建临时目录。");
        DeleteFileW(unique); path = unique; fs::create_directory(path);
    }
    ~TempFolder() {
        // Only files created by this installer; never recursively remove a user-selected directory.
        DeleteFileW((path / L"PawnIO_setup.exe").c_str());
        RemoveDirectoryW(path.c_str());
    }
    fs::path path;
};
bool IsCpuOverlay(const fs::path& executable) {
    DWORD unused = 0, bytes = GetFileVersionInfoSizeW(executable.c_str(), &unused);
    if (!bytes) return false;
    std::vector<BYTE> buffer(bytes);
    if (!GetFileVersionInfoW(executable.c_str(), 0, bytes, buffer.data())) return false;
    wchar_t* product = nullptr; UINT length = 0;
    return VerQueryValueW(buffer.data(), L"\\StringFileInfo\\040904B0\\ProductName", reinterpret_cast<void**>(&product), &length)
        && length && product && wcscmp(product, L"CpuOverlay") == 0;
}
void Shortcut(const fs::path& target) {
    ComPtr<IShellLinkW> link;
    Require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))), L"无法创建快捷方式。");
    link->SetPath((target / L"CpuOverlay.exe").c_str());
    link->SetArguments(L"--show"); link->SetWorkingDirectory(target.c_str());
    link->SetDescription(L"系统性能、键鼠电量监控和乔思伯温度数显 — Alt+F1 显示 / 隐藏");
    ComPtr<IPersistFile> file;
    Require(SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save((KnownFolder(FOLDERID_Programs) / L"CpuOverlay.lnk").c_str(), TRUE)), L"无法保存开始菜单快捷方式。");
}
std::string Json(const std::wstring& value) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(count, 0), result;
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), utf8.data(), count, nullptr, nullptr);
    for (const char c : utf8) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n"; else if (c == '\r') result += "\\r"; else result += c;
    }
    return result;
}
void Report(const Options& options, const Outcome& outcome) {
    const auto path = options.report.empty() ? options.target / L"bin" / L"install-report.json" : options.report;
    if (options.uninstall && options.report.empty()) return;
    std::error_code error; fs::create_directories(path.parent_path(), error);
    std::ofstream file(path);
    file << "{\n  \"version\": \"1.6.0.0\",\n  \"success\": " << (outcome.success ? "true" : "false")
        << ",\n  \"target\": \"" << Json(options.target.wstring()) << "\",\n  \"pawnio_was_present\": " << (outcome.pawnPresent ? "true" : "false")
        << ",\n  \"pawnio_installed_now\": " << (outcome.pawnInstalled ? "true" : "false")
        << ",\n  \"error\": \"" << Json(outcome.error) << "\"\n}\n";
}

void Uninstall(const Options& options) {
    const auto installed = ReadRegistry(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation");
    Require(!installed.empty() && _wcsicmp(fs::weakly_canonical(installed).c_str(), fs::weakly_canonical(options.target).c_str()) == 0,
        L"卸载路径与已登记的安装位置不一致，已停止操作。");
    StopOverlay();
    const auto main = options.target / L"CpuOverlay.exe";
    if (fs::exists(main)) { Require(IsCpuOverlay(main), L"目标文件不属于 CpuOverlay。"); Require(Run(main, L"--disable-startup") == 0, L"移除登录自启失败。"); }
    const wchar_t* files[] = {L"CpuOverlay.exe", L"bin\\pawnio\\IntelMSR.bin", L"bin\\pawnio\\IntelMSR.p", L"bin\\pawnio\\COPYING",
        L"bin\\pawnio\\AMDFamily17.bin", L"bin\\pawnio\\AMDFamily17.p",
        L"bin\\pawnio\\AMDFamily0F.bin", L"bin\\pawnio\\AMDFamily0F.p",
        L"bin\\pawnio\\AMDFamily10.bin", L"bin\\pawnio\\AMDFamily10.p",
        L"bin\\pawnio\\README.md", L"bin\\docs\\README.md", L"bin\\install-report.json"};
    for (const auto relative : files) {
        const auto path = options.target / relative;
        Require(DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND, L"无法删除程序文件：" + path.wstring());
    }
    DeleteFileW((KnownFolder(FOLDERID_Programs) / L"CpuOverlay.lnk").c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
    HKEY settings = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_SET_VALUE, &settings) == ERROR_SUCCESS) {
        RegDeleteValueW(settings, L"InstallPath"); RegCloseKey(settings);
    }
    const auto uninstaller = options.target / L"bin" / L"CpuOverlay-Setup.exe";
    if (_wcsicmp(SelfPath().c_str(), uninstaller.c_str()) == 0) MoveFileExW(uninstaller.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    else DeleteFileW(uninstaller.c_str());
    RemoveDirectoryW((options.target / L"bin" / L"pawnio").c_str());
    RemoveDirectoryW((options.target / L"bin" / L"docs").c_str());
    // PawnIO is a shared system component. Uninstalling this app never removes its driver.
}

Outcome Perform(const Options& options, HWND progress) {
    Outcome outcome;
    auto stage = [progress](int percent, const wchar_t* text) {
        if (progress) {
            SendMessageW(progress, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(text));
            SendMessageW(progress, TDM_SET_PROGRESS_BAR_POS, percent, 0);
        }
    };
    try {
        Require(!options.target.empty() && options.target.is_absolute() && options.target != options.target.root_path(), L"请选择一个有效的安装目录。");
        if (options.uninstall) { stage(20, L"正在卸载 CpuOverlay…"); Uninstall(options); outcome.success = true; return outcome; }
        stage(5, L"正在检查 PawnIO…");
        outcome.pawnPresent = PawnPresent();
        if (!outcome.pawnPresent) {
            TempFolder temporary;
            const auto installer = temporary.path / L"PawnIO_setup.exe";
            Extract(106, installer);
            stage(15, L"正在安装官方签名版 PawnIO 驱动…");
            const DWORD result = Run(installer, L"-install -silent");
            Require(result == 0, L"PawnIO 安装失败：" + ErrorText(result));
            Require(PawnPresent(), L"PawnIO 安装后仍未检测到，请重启 Windows 后重试。");
            outcome.pawnInstalled = true;
        }
        stage(45, L"正在更新程序文件…");
        const auto main = options.target / L"CpuOverlay.exe";
        if (fs::exists(main)) Require(IsCpuOverlay(main), L"目标位置存在同名的其他程序，无法覆盖。");
        StopOverlay();
        fs::create_directories(options.target / L"bin" / L"backup");
        if (fs::exists(main))
            Require(CopyFileW(main.c_str(), (options.target / L"bin" / L"backup" / L"CpuOverlay-previous.exe").c_str(), FALSE) != FALSE, L"无法备份原程序。");
        Extract(101, main);
        Extract(102, options.target / L"bin" / L"pawnio" / L"IntelMSR.bin");
        Extract(103, options.target / L"bin" / L"pawnio" / L"COPYING");
        Extract(104, options.target / L"bin" / L"pawnio" / L"IntelMSR.p");
        Extract(108, options.target / L"bin" / L"pawnio" / L"AMDFamily17.bin");
        Extract(109, options.target / L"bin" / L"pawnio" / L"AMDFamily17.p");
        Extract(110, options.target / L"bin" / L"pawnio" / L"AMDFamily0F.bin");
        Extract(111, options.target / L"bin" / L"pawnio" / L"AMDFamily0F.p");
        Extract(112, options.target / L"bin" / L"pawnio" / L"AMDFamily10.bin");
        Extract(113, options.target / L"bin" / L"pawnio" / L"AMDFamily10.p");
        Extract(105, options.target / L"bin" / L"pawnio" / L"README.md");
        Extract(107, options.target / L"bin" / L"docs" / L"README.md");
        const auto uninstall = options.target / L"bin" / L"CpuOverlay-Setup.exe";
        if (_wcsicmp(SelfPath().c_str(), uninstall.c_str()) != 0)
            Require(CopyFileW(SelfPath().c_str(), uninstall.c_str(), FALSE) != FALSE, L"无法创建卸载程序。");
        stage(75, L"正在配置快捷方式和登录自启…");
        Require(Run(main, options.startup ? L"--enable-startup" : L"--disable-startup") == 0, L"登录自启配置失败。");
        WriteRegistry(kUninstallKey, L"DisplayName", L"CpuOverlay");
        WriteRegistry(kUninstallKey, L"DisplayVersion", L"1.6.0.0");
        WriteRegistry(kUninstallKey, L"Publisher", L"sixunited");
        WriteRegistry(kUninstallKey, L"InstallLocation", options.target.wstring());
        WriteRegistry(kUninstallKey, L"DisplayIcon", main.wstring());
        WriteRegistry(kUninstallKey, L"UninstallString", Quote(uninstall) + L" --uninstall --target " + Quote(options.target));
        WriteRegistry(kUninstallKey, L"QuietUninstallString", Quote(uninstall) + L" --uninstall --silent --target " + Quote(options.target));
        WriteRegistry(kSettingsKey, L"InstallPath", main.wstring());
        Shortcut(options.target);
        stage(95, L"安装完成。");
        if (options.launch) Run(main, L"--show", false);
        outcome.success = true;
    } catch (const Failure& failure) { outcome.error = failure.message; }
      catch (const std::exception&) { outcome.error = L"文件操作失败，请确认安装目录可写且程序已退出。"; }
    return outcome;
}

bool ChooseTarget(fs::path& path) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
    dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"选择 CpuOverlay 安装目录");
    if (FAILED(dialog->Show(nullptr))) return false;
    ComPtr<IShellItem> item;
    PWSTR result = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &result))) {
        path = result; CoTaskMemFree(result); return true;
    }
    return false;
}
bool Confirm(Options& options) {
    if (options.silent) return true;
    if (options.uninstall)
        return MessageBoxW(nullptr, L"卸载 CpuOverlay？\n\n将移除程序和登录自启。共享的 PawnIO 驱动会保留。", L"CpuOverlay 卸载", MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
    for (;;) {
        TASKDIALOG_BUTTON buttons[] = {{100, L"安装"}, {101, L"更改位置…"}, {IDCANCEL, L"取消"}};
        const auto content = L"安装位置：\n" + options.target.wstring() + L"\n\nCPU 占用、频率、温度、功耗、内存、显卡占用和键鼠电量。\n后台向乔思伯水冷数显屏发送 CPU 温度。\n缺少 PawnIO 时会自动安装内含的官方签名版本。";
        TASKDIALOGCONFIG dialog{sizeof(dialog)};
        dialog.dwFlags = TDF_SIZE_TO_CONTENT | (options.startup ? TDF_VERIFICATION_FLAG_CHECKED : 0);
        dialog.pszWindowTitle = L"CpuOverlay 安装";
        dialog.pszMainInstruction = L"安装 CpuOverlay 1.6.0.0";
        dialog.pszContent = content.c_str(); dialog.pszVerificationText = L"登录 Windows 后自动运行";
        dialog.pButtons = buttons; dialog.cButtons = ARRAYSIZE(buttons); dialog.nDefaultButton = 100;
        int selected = 0; BOOL checked = FALSE;
        if (FAILED(TaskDialogIndirect(&dialog, &selected, nullptr, &checked))) return false;
        options.startup = checked != FALSE;
        if (selected == 100) return true;
        if (selected != 101) return false;
        ChooseTarget(options.target);
    }
}
struct ProgressContext { const Options* options; Outcome result; std::thread worker; };
HRESULT CALLBACK ProgressCallback(HWND window, UINT event, WPARAM, LPARAM, LONG_PTR data) {
    if (event == TDN_CREATED) {
        auto context = reinterpret_cast<ProgressContext*>(data);
        SendMessageW(window, TDM_ENABLE_BUTTON, IDOK, FALSE);
        context->worker = std::thread([context, window] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            context->result = Perform(*context->options, window);
            CoUninitialize();
            SendMessageW(window, TDM_ENABLE_BUTTON, IDOK, TRUE);
            PostMessageW(window, TDM_CLICK_BUTTON, IDOK, 0);
        });
    }
    return S_OK;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Options options;
    Outcome outcome;
    try {
        options.target = DefaultTarget();
        int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        Require(argv != nullptr, L"命令行参数无效。");
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg(argv[i]);
            if (arg == L"--silent") options.silent = true;
            else if (arg == L"--uninstall") options.uninstall = true;
            else if (arg == L"--no-launch") options.launch = false;
            else if (arg == L"--no-startup") options.startup = false;
            else if (arg == L"--target" && i + 1 < argc) options.target = argv[++i];
            else if (arg == L"--report" && i + 1 < argc) options.report = argv[++i];
            else { LocalFree(argv); throw Failure{L"无法识别的安装参数。"}; }
        }
        LocalFree(argv);
        options.target = fs::weakly_canonical(options.target);
        if (!Confirm(options)) { if (SUCCEEDED(initialized)) CoUninitialize(); return 1223; }
        if (options.silent) outcome = Perform(options, nullptr);
        else {
            ProgressContext context{&options, {}, {}};
            TASKDIALOGCONFIG dialog{sizeof(dialog)};
            dialog.pszWindowTitle = L"CpuOverlay"; dialog.pszMainInstruction = options.uninstall ? L"正在卸载…" : L"正在安装…";
            dialog.pszContent = L"请稍候。"; dialog.dwFlags = TDF_SHOW_PROGRESS_BAR;
            dialog.dwCommonButtons = TDCBF_OK_BUTTON;
            dialog.pfCallback = ProgressCallback; dialog.lpCallbackData = reinterpret_cast<LONG_PTR>(&context);
            const HRESULT shown = TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr);
            if (context.worker.joinable()) context.worker.join();
            Require(SUCCEEDED(shown), L"无法显示安装进度窗口。");
            outcome = context.result;
        }
    } catch (const Failure& failure) { outcome.error = failure.message; }
      catch (...) { outcome.error = L"安装未完成。"; }
    Report(options, outcome);
    if (!outcome.success && !options.silent) MessageBoxW(nullptr, outcome.error.c_str(), L"CpuOverlay — 操作失败", MB_OK | MB_ICONERROR);
    if (outcome.success && !options.silent)
        MessageBoxW(nullptr, options.uninstall ? L"CpuOverlay 已卸载。" : L"安装完成。\n按 Alt+F1 显示 / 隐藏监控；右键托盘图标可设置。", L"CpuOverlay", MB_OK | MB_ICONINFORMATION);
    if (SUCCEEDED(initialized)) CoUninitialize();
    return outcome.success ? 0 : 1;
}
