#include "codex_quota.h"
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <thread>

namespace {
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
const char* kResult = R"({"rateLimitsByLimitId":{"codex":{"planType":"plus","primary":{"usedPercent":17,"windowDurationMins":300,"resetsAt":4102444800},"secondary":{"usedPercent":64,"windowDurationMins":10080,"resetsAt":4102704000}},"other":{"primary":{"usedPercent":99}}},"rateLimits":{"primary":{"usedPercent":90}}})";
int FakeServer() {
    wchar_t mode[64]{}; GetEnvironmentVariableW(L"CPUOVERLAY_QUOTA_TEST_MODE", mode, ARRAYSIZE(mode));
    bool initialized = false;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.find("\"initialize\"") != std::string::npos) {
            if (std::wstring(mode) == L"timeout") { Sleep(5000); return 0; }
            if (std::wstring(mode) == L"exit") return 0;
            if (std::wstring(mode) == L"malformed") { std::cout << "bad json\n" << std::flush; continue; }
            if (std::wstring(mode) == L"oversized") { std::cout << std::string(1024 * 1024 + 100, 'x') << std::flush; continue; }
            std::cout << "{\"id\":1," << std::flush;
            Sleep(10);
            std::cout << "\"result\":{}}\n{\"method\":\"test/notification\"}\n" << std::flush;
        } else if (line.find("\"initialized\"") != std::string::npos) initialized = true;
        else if (line.find("account/read") != std::string::npos) {
            if (!initialized) return 8;
            if (std::wstring(mode) == L"login") std::cout << "{\"id\":2,\"result\":{\"account\":null}}\n" << std::flush;
            else if (std::wstring(mode) == L"api-key") std::cout << "{\"id\":2,\"result\":{\"account\":{\"type\":\"apiKey\"}}}\n" << std::flush;
            else std::cout << "{\"id\":2,\"result\":{\"account\":{\"type\":\"chatgpt\"}}}\n" << std::flush;
        } else if (line.find("account/rateLimits/read") != std::string::npos) {
            if (std::wstring(mode) == L"auth-error") std::cout << "{\"id\":3,\"error\":{\"code\":401,\"message\":\"Unauthorized\"}}\n" << std::flush;
            else std::cout << "{\"id\":99,\"result\":{}}\n{\"id\":3,\"result\":" << kResult << "}\n" << std::flush;
        } else return 9; // Fail the fixture if any mutating API is introduced.
    }
    return 0;
}
std::wstring Environment(const wchar_t* name) {
    const auto size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    const auto written = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(written); return value;
}
struct RestoreEnvironment {
    const wchar_t* name;
    std::wstring old;
    explicit RestoreEnvironment(const wchar_t* value) : name(value), old(Environment(value)) {}
    ~RestoreEnvironment() { SetEnvironmentVariableW(name, old.empty() ? nullptr : old.c_str()); }
};
bool NoChildren() {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{sizeof(entry)};
    bool clean = true;
    if (Process32FirstW(snapshot, &entry)) do {
        if (entry.th32ParentProcessID == GetCurrentProcessId()) clean = false;
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot); return clean;
}
bool WaitState(CodexQuotaMonitor& monitor, CodexQuotaState state) {
    const auto deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) {
        if (monitor.Snapshot().state == state) return true;
        Sleep(20);
    }
    return false;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring(argv[1]) == L"app-server") return FakeServer();
    auto sample = ParseCodexQuota(kResult);
    Check(sample.state == CodexQuotaState::Ready && sample.primary.remainingPercent == 83 && sample.secondary.remainingPercent == 36,
        "Codex bucket wins over legacy and unrelated limits; remaining equals 100 minus used");
    Check(sample.primary.durationMinutes == 300 && sample.secondary.durationMinutes == 10080 && sample.primary.resetsAt == 4102444800 && sample.plan == L"plus",
        "window durations, 64-bit reset times and plan survive normalization");
    sample = ParseCodexQuota(R"({"rateLimits":{"primary":{"usedPercent":0},"secondary":{"usedPercent":100}}})");
    Check(sample.state == CodexQuotaState::Ready && sample.primary.remainingPercent == 100 && sample.secondary.remainingPercent == 0,
        "legacy single-bucket servers, zero usage and exhausted quota are supported");
    sample = ParseCodexQuota(R"({"rateLimits":{"limitId":"codex_other","primary":{"usedPercent":4}}})");
    Check(sample.state == CodexQuotaState::Unavailable, "a different quota bucket is never shown as Codex");
    sample = ParseCodexQuota(R"({"rateLimitsByLimitId":{"other":{"primary":{"usedPercent":1}}}})");
    Check(sample.state == CodexQuotaState::Unavailable, "missing Codex bucket stays unknown");
    for (const auto* json : {"{}", "broken", R"({"rateLimits":{"primary":{"usedPercent":null}}})",
        R"({"rateLimits":{"primary":{"usedPercent":"20"}}})", R"({"rateLimits":{"primary":{"usedPercent":false}}})",
        R"({"rateLimits":{"primary":{"usedPercent":20,"resetsAt":"tomorrow"}}})",
        R"({"rateLimits":{"primary":{"usedPercent":20,"resetsAt":-1}}})",
        R"({"rateLimits":{"primary":{"usedPercent":20,"windowDurationMins":1.5}}})"}) {
        sample = ParseCodexQuota(json);
        Check(sample.state == CodexQuotaState::Unavailable && !sample.primary.remainingPercent, "invalid quota never becomes a fabricated remainder");
    }
    sample = ParseCodexQuota(R"({"rateLimits":{"primary":null,"secondary":{"usedPercent":35,"resetsAt":null}}})");
    Check(sample.state == CodexQuotaState::Ready && !sample.primary.remainingPercent && sample.secondary.remainingPercent == 65,
        "one available window does not invent data for the other");
    sample = ParseCodexQuota(R"({"rateLimits":{"primary":{"usedPercent":49,"windowDurationMins":10080},"secondary":null}})");
    Check(!sample.primary.remainingPercent && sample.secondary.remainingPercent == 51 && sample.secondary.durationMinutes == 10080,
        "weekly-only plans populate the long-term item without duplicate seven-day labels");
    sample = ParseCodexQuota(R"({"rateLimits":{"primary":{"usedPercent":49,"windowDurationMins":10080},"secondary":{"usedPercent":15,"windowDurationMins":300}}})");
    Check(sample.primary.remainingPercent == 85 && sample.secondary.remainingPercent == 51,
        "short and long periods stay correctly assigned when the server reverses their order");
    sample = ParseCodexQuota(R"({"rateLimits":{"primary":{"usedPercent":-1},"secondary":{"usedPercent":101}}})");
    Check(sample.primary.remainingPercent == 100 && sample.secondary.remainingPercent == 0, "percentages stay within valid display bounds");
    Check(CodexQuotaLabel({50, 15, 0}, false) == L"Codex 15m" && CodexQuotaLabel({50, 60, 0}, false) == L"Codex 1h",
        "server-provided periods are displayed accurately instead of always saying five hours");
    Check(CodexQuotaValue({0, 300, 200}, CodexQuotaState::Ready, 100) == L"0%", "exhausted is a real zero");
    Check(CodexQuotaValue({42, 300, 100}, CodexQuotaState::Ready, 100) == L"--%(待更新)", "expired windows never automatically become 100 percent");
    Check(CodexQuotaValue({42, 300, 200}, CodexQuotaState::Stale, 100) == L"42%(未更新)", "last known quota is explicitly marked stale");
    Check(CodexQuotaValue({}, CodexQuotaState::LoginRequired).find(L"需登录") != std::wstring::npos,
        "login failures have an actionable display state");

    wchar_t self[32768]{}; GetModuleFileNameW(nullptr, self, ARRAYSIZE(self));
    RestoreEnvironment mode(L"CPUOVERLAY_QUOTA_TEST_MODE"), executable(L"CODEX_CLI_PATH");
    SetEnvironmentVariableW(L"CPUOVERLAY_QUOTA_TEST_MODE", L"success");
    sample = QueryCodexQuota(self);
    Check(sample.state == CodexQuotaState::Ready && sample.primary.remainingPercent == 83,
        "real pipes handle fragmented responses, notifications and the initialize handshake");
    for (const auto* state : {L"login", L"api-key", L"auth-error"}) {
        SetEnvironmentVariableW(L"CPUOVERLAY_QUOTA_TEST_MODE", state);
        sample = QueryCodexQuota(self);
        Check(sample.state == CodexQuotaState::LoginRequired && !sample.primary.remainingPercent, "signed-out, API-key and unauthorized states clear account readings");
    }
    for (const auto* state : {L"exit", L"malformed", L"oversized", L"timeout"}) {
        SetEnvironmentVariableW(L"CPUOVERLAY_QUOTA_TEST_MODE", state);
        const auto start = GetTickCount64();
        const bool oversized = std::wstring(state) == L"oversized";
        sample = QueryCodexQuota(self, nullptr, oversized ? 12000 : 500);
        Check(sample.state == CodexQuotaState::Unavailable && GetTickCount64() - start < (oversized ? 15000 : 2500),
            "exited, malformed, oversized and unresponsive children fail within bounded time");
        if (std::wstring(state) == L"timeout") Check(sample.error == WAIT_TIMEOUT, "deadline reports timeout");
        if (std::wstring(state) == L"oversized") Check(sample.error == ERROR_BUFFER_OVERFLOW, "unbounded output is rejected");
    }
    SetEnvironmentVariableW(L"CPUOVERLAY_QUOTA_TEST_MODE", L"timeout");
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const auto start = GetTickCount64();
    std::thread interrupt([cancel] { Sleep(100); SetEvent(cancel); });
    sample = QueryCodexQuota(self, cancel, 5000);
    interrupt.join(); CloseHandle(cancel);
    Check(sample.error == ERROR_CANCELLED && GetTickCount64() - start < 2500, "hiding, suspension and exit can promptly cancel a query");
    Check(QueryCodexQuota(L"Z:\\missing\\codex.exe").state == CodexQuotaState::NotInstalled, "missing CLI is distinguished from a zero balance");
    SetEnvironmentVariableW(L"CODEX_CLI_PATH", self);
    Check(ResolveCodexExecutable() == self, "explicit native CLI override is supported");
    SetEnvironmentVariableW(L"CPUOVERLAY_QUOTA_TEST_MODE", L"success");
    {
        CodexQuotaMonitor monitor;
        monitor.SetActive(true);
        Check(WaitState(monitor, CodexQuotaState::Ready), "background monitor fetches when enabled");
        SetEnvironmentVariableW(L"CPUOVERLAY_QUOTA_TEST_MODE", L"exit");
        monitor.SetActive(false); monitor.SetActive(true);
        Check(WaitState(monitor, CodexQuotaState::Stale) && monitor.Snapshot().primary.remainingPercent == 83,
            "temporary query failure preserves last known readings with a stale marker");
        SetEnvironmentVariableW(L"CPUOVERLAY_QUOTA_TEST_MODE", L"login");
        monitor.SetActive(false); monitor.SetActive(true);
        Check(WaitState(monitor, CodexQuotaState::LoginRequired) && !monitor.Snapshot().primary.remainingPercent,
            "sign-out clears the previous account's readings");
        monitor.SetActive(false);
    }
    Check(NoChildren(), "queries, cancellations and monitor shutdown leave no child processes behind");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
