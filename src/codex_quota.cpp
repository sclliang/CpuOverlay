#include "codex_quota.h"
#include <winrt/Windows.Data.Json.h>
#include <roapi.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using namespace winrt::Windows::Data::Json;
namespace fs = std::filesystem;
constexpr size_t kMaxOutput = 1024 * 1024;
constexpr DWORD kRefreshMs = 60000;

struct Apartment {
    HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
    Apartment() { if (FAILED(result) && result != RPC_E_CHANGED_MODE) winrt::check_hresult(result); }
    ~Apartment() {
        if (SUCCEEDED(result)) {
            // Cached WinRT factories must not outlive the last COM apartment:
            // otherwise the next timed poll can call into an unloaded JSON DLL.
            winrt::clear_factory_cache();
            RoUninitialize();
        }
    }
};
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { Reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void Reset(HANDLE handle = nullptr) { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = handle; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
CodexQuotaSample Failure(CodexQuotaState state, DWORD error) {
    CodexQuotaSample result; result.state = state; result.error = error; return result;
}
JsonObject Object(const JsonObject& parent, const wchar_t* key) {
    const auto value = parent.TryLookup(key);
    return value && value.ValueType() == JsonValueType::Object ? value.GetObject() : JsonObject{nullptr};
}
std::optional<double> Number(const JsonObject& parent, const wchar_t* key) {
    const auto value = parent.TryLookup(key);
    if (!value || value.ValueType() != JsonValueType::Number) return {};
    const double number = value.GetNumber();
    return std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
}
std::wstring String(const JsonObject& parent, const wchar_t* key) {
    const auto value = parent.TryLookup(key);
    if (!value || value.ValueType() != JsonValueType::String) return {};
    const auto text = value.GetString();
    return text.size() <= 128 ? std::wstring(text) : std::wstring{};
}
CodexQuotaWindow Window(const JsonObject& snapshot, const wchar_t* key) {
    CodexQuotaWindow window;
    const auto object = Object(snapshot, key);
    if (!object) return window;
    const auto used = Number(object, L"usedPercent");
    if (!used) return window;
    window.remainingPercent = 100.0 - std::clamp(*used, 0.0, 100.0);
    if (const auto duration = Number(object, L"windowDurationMins")) {
        if (*duration <= 0 || *duration > 525600 || std::floor(*duration) != *duration) return {};
        window.durationMinutes = static_cast<int>(*duration);
    }
    if (const auto reset = Number(object, L"resetsAt")) {
        if (*reset <= 0 || *reset > 253402300799.0 || std::floor(*reset) != *reset) return {};
        window.resetsAt = static_cast<int64_t>(*reset);
    }
    // Null optional fields are supported; incorrectly typed fields are not.
    for (const auto field : {L"windowDurationMins", L"resetsAt"}) {
        const auto value = object.TryLookup(field);
        if (value && value.ValueType() != JsonValueType::Null && value.ValueType() != JsonValueType::Number) return {};
    }
    return window;
}
CodexQuotaSample Parse(const JsonObject& result) {
    // Prefer the explicit Codex bucket. Never substitute a model-specific bucket.
    const auto buckets = Object(result, L"rateLimitsByLimitId");
    JsonObject snapshot = buckets ? Object(buckets, L"codex") : JsonObject{nullptr};
    if (!snapshot) {
        snapshot = Object(result, L"rateLimits");
        if (snapshot) {
            const auto id = String(snapshot, L"limitId");
            if (!id.empty() && id != L"codex") snapshot = nullptr;
        }
    }
    if (!snapshot) return Failure(CodexQuotaState::Unavailable, ERROR_NOT_FOUND);
    CodexQuotaSample sample;
    sample.primary = Window(snapshot, L"primary");
    sample.secondary = Window(snapshot, L"secondary");
    if (!sample.primary.remainingPercent && !sample.secondary.remainingPercent)
        return Failure(CodexQuotaState::Unavailable, ERROR_INVALID_DATA);
    // Some plans expose only a weekly window, even in the primary field.
    // Keep the short/long display switches meaningful and avoid duplicate labels.
    if (sample.primary.remainingPercent && sample.primary.durationMinutes >= 1440 && !sample.secondary.remainingPercent) {
        sample.secondary = sample.primary; sample.primary = {};
    } else if (!sample.primary.remainingPercent && sample.secondary.remainingPercent &&
        sample.secondary.durationMinutes > 0 && sample.secondary.durationMinutes < 1440) {
        sample.primary = sample.secondary; sample.secondary = {};
    } else if (sample.primary.durationMinutes > 0 && sample.secondary.durationMinutes > 0 &&
        sample.primary.durationMinutes > sample.secondary.durationMinutes) std::swap(sample.primary, sample.secondary);
    sample.plan = String(snapshot, L"planType");
    sample.updatedMs = GetTickCount64();
    sample.state = CodexQuotaState::Ready;
    return sample;
}
std::wstring Environment(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (!length || length > 32768) return {};
    std::vector<wchar_t> text(length);
    const auto written = GetEnvironmentVariableW(name, text.data(), length);
    return written && written < length ? std::wstring(text.data(), written) : std::wstring{};
}
bool Executable(const fs::path& path) {
    std::error_code error;
    return path.is_absolute() && _wcsicmp(path.extension().c_str(), L".exe") == 0 && fs::is_regular_file(path, error);
}

struct Child {
    Handle job, process, input, output;
    std::string buffered;
    size_t received = 0;
    ~Child() {
        if (job) TerminateJobObject(job.value, ERROR_CANCELLED);
        if (process) WaitForSingleObject(process.value, 1000);
    }
};
bool StartChild(const std::wstring& executable, Child& child) {
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    Handle inputRead, outputWrite;
    if (!CreatePipe(&inputRead.value, &child.input.value, &inherit, 4096) ||
        !CreatePipe(&child.output.value, &outputWrite.value, &inherit, 4096) ||
        !SetHandleInformation(child.input.value, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(child.output.value, HANDLE_FLAG_INHERIT, 0)) return false;
    // Discard stderr: account data and CLI diagnostics never enter our logs or UI.
    Handle errors(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr));
    child.job.Reset(CreateJobObjectW(nullptr, nullptr));
    if (!errors || !child.job) return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(child.job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return false;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<BYTE> storage(bytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) return false;
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST list; ~Cleanup() { DeleteProcThreadAttributeList(list); } } cleanup{attributes};
    HANDLE inherited[]{inputRead.value, outputWrite.value, errors.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) return false;
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inputRead.value;
    startup.StartupInfo.hStdOutput = outputWrite.value;
    startup.StartupInfo.hStdError = errors.value;
    startup.lpAttributeList = attributes;
    std::wstring command = L"\"" + executable + L"\" app-server --listen stdio://";
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
        fs::path(executable).parent_path().c_str(), &startup.StartupInfo, &info)) return false;
    child.process.Reset(info.hProcess);
    Handle thread(info.hThread);
    if (!AssignProcessToJobObject(child.job.value, child.process.value)) {
        const auto error = GetLastError(); TerminateProcess(child.process.value, error); SetLastError(error); return false;
    }
    return ResumeThread(thread.value) != static_cast<DWORD>(-1);
}
bool Send(Child& child, const char* json) {
    const DWORD size = static_cast<DWORD>(std::char_traits<char>::length(json));
    DWORD written = 0;
    return WriteFile(child.input.value, json, size, &written, nullptr) && written == size;
}
bool AuthError(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    for (const auto word : {L"unauthorized", L"not logged", L"not authenticated", L"authentication", L"sign in", L"401"})
        if (text.find(word) != std::wstring::npos) return true;
    return false;
}
JsonObject Response(Child& child, int id, ULONGLONG deadline, HANDLE cancelled, CodexQuotaSample& failure) {
    while (GetTickCount64() < deadline) {
        if (cancelled && WaitForSingleObject(cancelled, 0) == WAIT_OBJECT_0) {
            failure.error = ERROR_CANCELLED; return nullptr;
        }
        size_t newline;
        while ((newline = child.buffered.find('\n')) != std::string::npos) {
            const auto line = child.buffered.substr(0, newline);
            child.buffered.erase(0, newline + 1);
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            JsonObject message{nullptr};
            if (!JsonObject::TryParse(winrt::to_hstring(line), message)) { failure.error = ERROR_INVALID_DATA; return nullptr; }
            const auto responseId = Number(message, L"id");
            if (!responseId || *responseId != id) continue;
            if (const auto error = Object(message, L"error")) {
                if (AuthError(String(error, L"message"))) failure = Failure(CodexQuotaState::LoginRequired, ERROR_LOGON_FAILURE);
                else failure.error = ERROR_INVALID_DATA;
                return nullptr;
            }
            const auto result = Object(message, L"result");
            if (!result) failure.error = ERROR_INVALID_DATA;
            return result;
        }
        DWORD available = 0;
        if (!PeekNamedPipe(child.output.value, nullptr, 0, nullptr, &available, nullptr)) { failure.error = GetLastError(); return nullptr; }
        if (available) {
            char buffer[8192]; DWORD read = 0;
            if (!ReadFile(child.output.value, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) || !read) {
                failure.error = ERROR_BROKEN_PIPE; return nullptr;
            }
            child.received += read;
            if (child.received > kMaxOutput) { failure.error = ERROR_BUFFER_OVERFLOW; return nullptr; }
            child.buffered.append(buffer, read);
        } else {
            if (WaitForSingleObject(child.process.value, 0) == WAIT_OBJECT_0) { failure.error = ERROR_PROCESS_ABORTED; return nullptr; }
            if (cancelled) WaitForSingleObject(cancelled, 25); else Sleep(25);
        }
    }
    failure.error = WAIT_TIMEOUT;
    return nullptr;
}
}

CodexQuotaSample ParseCodexQuota(const std::string& json) {
    try {
        if (json.size() > kMaxOutput) return Failure(CodexQuotaState::Unavailable, ERROR_BUFFER_OVERFLOW);
        Apartment apartment;
        return Parse(JsonObject::Parse(winrt::to_hstring(json)));
    } catch (...) { return Failure(CodexQuotaState::Unavailable, ERROR_INVALID_DATA); }
}

std::wstring ResolveCodexExecutable() {
    const auto overridePath = Environment(L"CODEX_CLI_PATH");
    if (!overridePath.empty()) return Executable(overridePath) ? overridePath : std::wstring{};
    const auto local = Environment(L"LOCALAPPDATA");
    if (!local.empty()) {
        const auto root = fs::path(local) / L"OpenAI" / L"Codex" / L"bin";
        if (Executable(root / L"codex.exe")) return (root / L"codex.exe").wstring();
        std::error_code error;
        fs::path newest;
        auto newestTime = fs::file_time_type::min();
        for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
            const auto candidate = it->path() / L"codex.exe";
            if (!Executable(candidate)) continue;
            std::error_code timeError;
            const auto time = fs::last_write_time(candidate, timeError);
            if (!timeError && time > newestTime) { newest = candidate; newestTime = time; }
        }
        if (!newest.empty()) return newest.wstring();
    }
    // Resolve native CLI binaries only; never invoke a command shell or a .cmd shim.
    const auto roaming = Environment(L"APPDATA");
    if (!roaming.empty()) {
        const auto npm = fs::path(roaming) / L"npm" / L"node_modules" / L"@openai" / L"codex";
        for (const auto& root : {npm, npm / L"node_modules" / L"@openai" / L"codex-win32-x64"}) {
            const auto candidate = root / L"vendor" / L"x86_64-pc-windows-msvc" / L"codex" / L"codex.exe";
            if (Executable(candidate)) return candidate.wstring();
        }
    }
    const auto paths = Environment(L"PATH");
    size_t start = 0;
    while (start < paths.size()) {
        const auto end = paths.find(L';', start);
        auto directory = paths.substr(start, end == std::wstring::npos ? end : end - start);
        if (directory.size() >= 2 && directory.front() == L'"' && directory.back() == L'"') directory = directory.substr(1, directory.size() - 2);
        const auto candidate = fs::path(directory) / L"codex.exe";
        std::error_code error;
        if (Executable(candidate) && !fs::equivalent(candidate.parent_path(), fs::current_path(error), error)) return candidate.wstring();
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return {};
}

CodexQuotaSample QueryCodexQuota(const std::wstring& executable, HANDLE cancelled, DWORD timeoutMs) {
    try {
        if (cancelled && WaitForSingleObject(cancelled, 0) == WAIT_OBJECT_0) return Failure(CodexQuotaState::Unavailable, ERROR_CANCELLED);
        const auto path = executable.empty() ? ResolveCodexExecutable() : executable;
        if (!Executable(path)) return Failure(CodexQuotaState::NotInstalled, ERROR_FILE_NOT_FOUND);
        Apartment apartment;
        Child child;
        if (!StartChild(path, child)) return Failure(CodexQuotaState::Unavailable, GetLastError());
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        auto failure = Failure(CodexQuotaState::Unavailable, ERROR_BROKEN_PIPE);
        if (!Send(child, "{\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"cpu-overlay\",\"title\":\"CpuOverlay\",\"version\":\"1.10.0.0\"},\"capabilities\":null}}\n")) return failure;
        if (!Response(child, 1, deadline, cancelled, failure)) return failure;
        if (!Send(child, "{\"method\":\"initialized\"}\n{\"id\":2,\"method\":\"account/read\",\"params\":{\"refreshToken\":false}}\n")) return failure;
        const auto accountResult = Response(child, 2, deadline, cancelled, failure);
        if (!accountResult) return failure;
        const auto account = Object(accountResult, L"account");
        if (!account || String(account, L"type") == L"apiKey") return Failure(CodexQuotaState::LoginRequired, ERROR_LOGON_FAILURE);
        if (!Send(child, "{\"id\":3,\"method\":\"account/rateLimits/read\"}\n")) return failure;
        const auto result = Response(child, 3, deadline, cancelled, failure);
        return result ? Parse(result) : failure;
    } catch (...) { return Failure(CodexQuotaState::Unavailable, ERROR_INVALID_DATA); }
}

struct CodexQuotaMonitor::Impl {
    Handle wake{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Handle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    mutable std::mutex mutex;
    bool active = false, stop = false;
    uint64_t generation = 0;
    ULONGLONG next = 0;
    CodexQuotaSample sample;
    std::thread worker;
    Impl() {
        if (!wake || !cancel) { sample = Failure(CodexQuotaState::Unavailable, GetLastError()); return; }
        try { worker = std::thread([this] { Run(); }); }
        catch (...) { sample = Failure(CodexQuotaState::Unavailable, ERROR_NOT_ENOUGH_MEMORY); }
    }
    ~Impl() {
        { std::lock_guard<std::mutex> lock(mutex); stop = true; }
        SetEvent(cancel.value); SetEvent(wake.value);
        if (worker.joinable()) worker.join();
    }
    void Run() {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stop) {
            const auto now = GetTickCount64();
            if (!active || now < next) {
                const DWORD delay = active ? static_cast<DWORD>(std::min<ULONGLONG>(next - now, kRefreshMs)) : INFINITE;
                lock.unlock(); WaitForSingleObject(wake.value, delay); lock.lock(); continue;
            }
            const auto current = generation;
            ResetEvent(cancel.value);
            lock.unlock();
            const auto result = QueryCodexQuota({}, cancel.value);
            lock.lock();
            if (stop || !active || current != generation) continue;
            if (result.state == CodexQuotaState::Unavailable &&
                (sample.primary.remainingPercent || sample.secondary.remainingPercent)) {
                sample.state = CodexQuotaState::Stale; sample.error = result.error;
            } else sample = result;
            next = GetTickCount64() + kRefreshMs;
        }
    }
};
CodexQuotaMonitor::CodexQuotaMonitor() : impl_(std::make_unique<Impl>()) {}
CodexQuotaMonitor::~CodexQuotaMonitor() = default;
void CodexQuotaMonitor::SetActive(bool active) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->active == active) return;
    impl_->active = active; ++impl_->generation; impl_->next = 0;
    SetEvent(impl_->cancel.value); SetEvent(impl_->wake.value);
}
CodexQuotaSample CodexQuotaMonitor::Snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto sample = impl_->sample;
    if (sample.state == CodexQuotaState::Ready && GetTickCount64() - sample.updatedMs > 3 * kRefreshMs)
        sample.state = CodexQuotaState::Stale;
    return sample;
}
