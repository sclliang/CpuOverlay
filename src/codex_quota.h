#pragma once
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <optional>
#include <string>

enum class CodexQuotaState { Loading, Ready, NotInstalled, LoginRequired, Unavailable, Stale };
struct CodexQuotaWindow {
    std::optional<double> remainingPercent;
    int durationMinutes = 0;
    int64_t resetsAt = 0; // Unix seconds; zero means the server did not provide it.
};
struct CodexQuotaSample {
    CodexQuotaWindow primary, secondary;
    CodexQuotaState state = CodexQuotaState::Loading;
    ULONGLONG updatedMs = 0;
    DWORD error = ERROR_SUCCESS;
    std::wstring plan;
};

inline int64_t CodexUnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline const wchar_t* CodexQuotaStateText(CodexQuotaState state) {
    switch (state) {
    case CodexQuotaState::Loading: return L"读取中";
    case CodexQuotaState::Ready: return L"已更新";
    case CodexQuotaState::NotInstalled: return L"未安装 Codex";
    case CodexQuotaState::LoginRequired: return L"需登录 ChatGPT";
    case CodexQuotaState::Stale: return L"未更新";
    default: return L"读取失败";
    }
}
inline std::wstring CodexQuotaLabel(const CodexQuotaWindow& window, bool secondary) {
    const int minutes = window.durationMinutes;
    const auto duration = minutes <= 0 ? std::wstring(secondary ? L"7d" : L"5h") :
        minutes % 1440 == 0 ? std::to_wstring(minutes / 1440) + L"d" :
        minutes % 60 == 0 ? std::to_wstring(minutes / 60) + L"h" : std::to_wstring(minutes) + L"m";
    return L"Codex " + duration;
}
inline std::wstring CodexQuotaValue(const CodexQuotaWindow& window, CodexQuotaState state, int64_t now = CodexUnixNow()) {
    if (window.remainingPercent && (state == CodexQuotaState::Ready || state == CodexQuotaState::Stale)) {
        // A completed window is unknown until the service observes the new cycle.
        if (window.resetsAt && window.resetsAt <= now) return L"--%(待更新)";
        wchar_t text[32]{};
        swprintf_s(text, L"%.0f%%", *window.remainingPercent);
        return std::wstring(text) + (state == CodexQuotaState::Stale ? L"(未更新)" : L"");
    }
    if (state == CodexQuotaState::Ready) return L"--%(无额度数据)";
    return std::wstring(L"--%(") + CodexQuotaStateText(state) + L")";
}

// Only the account/rateLimits/read result is accepted, never session token counts.
CodexQuotaSample ParseCodexQuota(const std::string& resultJson);
std::wstring ResolveCodexExecutable();
CodexQuotaSample QueryCodexQuota(const std::wstring& executable = {}, HANDLE cancelled = nullptr, DWORD timeoutMs = 12000);

class CodexQuotaMonitor {
public:
    CodexQuotaMonitor();
    ~CodexQuotaMonitor();
    CodexQuotaMonitor(const CodexQuotaMonitor&) = delete;
    CodexQuotaMonitor& operator=(const CodexQuotaMonitor&) = delete;
    void SetActive(bool active);
    CodexQuotaSample Snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
