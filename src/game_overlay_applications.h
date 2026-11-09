#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace GameOverlay {
constexpr size_t MaxExcludedApplications = 64;
inline bool ValidApplicationName(const std::wstring& name) {
    if (name.size() < 5 || name.size() >= MAX_PATH || _wcsicmp(name.c_str() + name.size() - 4, L".exe") != 0) return false;
    return name.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos &&
        std::none_of(name.begin(), name.end(), [](wchar_t c) { return c < 32; });
}
inline bool ContainsApplication(const std::vector<std::wstring>& names, const std::wstring& name) {
    return !name.empty() && std::any_of(names.begin(), names.end(), [&](const auto& other) { return _wcsicmp(other.c_str(), name.c_str()) == 0; });
}
inline std::wstring ApplicationName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::wstring path(32768, L'\0'); DWORD size = static_cast<DWORD>(path.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE;
    CloseHandle(process);
    if (!ok) return {};
    path.resize(size); const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}
inline std::vector<std::wstring> RecentApplications(const wchar_t* key = L"Software\\sixunited\\CpuOverlay") {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"RecentGameOverlayApps", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes > (MaxExcludedApplications * MAX_PATH + 2) * sizeof(wchar_t)) return {};
    std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 2, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"RecentGameOverlayApps", RRF_RT_REG_MULTI_SZ, nullptr, data.data(), &bytes) != ERROR_SUCCESS) return {};
    std::vector<std::wstring> names;
    for (const wchar_t* p = data.data(); *p && names.size() < MaxExcludedApplications; p += wcslen(p) + 1)
        if (ValidApplicationName(p) && !ContainsApplication(names, p)) names.emplace_back(p);
    return names;
}
inline void RememberApplication(const std::wstring& name) {
    if (!ValidApplicationName(name)) return;
    auto names = RecentApplications(); if (ContainsApplication(names, name)) return;
    names.insert(names.begin(), name); if (names.size() > MaxExcludedApplications) names.pop_back();
    std::vector<wchar_t> data;
    for (const auto& item : names) { data.insert(data.end(), item.begin(), item.end()); data.push_back(0); }
    data.push_back(0);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\sixunited\\CpuOverlay", L"RecentGameOverlayApps", REG_MULTI_SZ,
        data.data(), static_cast<DWORD>(data.size() * sizeof(wchar_t)));
}
inline std::vector<std::wstring> RunningApplications() {
    std::vector<std::wstring> names;
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        if (!IsWindowVisible(window) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId()) return TRUE;
        const auto name = ApplicationName(pid); auto& result = *reinterpret_cast<std::vector<std::wstring>*>(context);
        if (ValidApplicationName(name) && !ContainsApplication(result, name)) result.push_back(name);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&names));
    return names;
}
}
