#include "startup.h"
#include <shellapi.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <filesystem>
#include <string>

using Microsoft::WRL::ComPtr;
namespace {
struct Bstr {
    explicit Bstr(const wchar_t* text) : value(SysAllocString(text)) {}
    ~Bstr() { SysFreeString(value); }
    operator BSTR() const { return value; }
    BSTR value;
};
struct Variant : VARIANT {
    Variant() { VariantInit(this); }
    explicit Variant(const wchar_t* text) { VariantInit(this); vt = VT_BSTR; bstrVal = SysAllocString(text); }
    ~Variant() { VariantClear(this); }
};
std::wstring UserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    BYTE buffer[256]{};
    DWORD length = 0;
    std::wstring result;
    if (GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &length)) {
        PWSTR sid = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid, &sid)) {
            result = sid; LocalFree(sid);
        }
    }
    CloseHandle(token);
    return result;
}
class Scheduler {
public:
    Scheduler() {
        initialization_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(initialization_) && initialization_ != RPC_E_CHANGED_MODE) { status = initialization_; return; }
        status = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
        if (FAILED(status)) return;
        Variant empty;
        status = service->Connect(empty, empty, empty, empty);
        if (SUCCEEDED(status)) status = service->GetFolder(Bstr(L"\\"), &folder);
        sid = UserSid();
        if (sid.empty() && SUCCEEDED(status)) status = E_ACCESSDENIED;
        name = L"CpuOverlay-" + sid;
    }
    ~Scheduler() {
        folder.Reset(); service.Reset();
        if (SUCCEEDED(initialization_)) CoUninitialize();
    }
    HRESULT status = E_FAIL;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    std::wstring sid, name;
private:
    HRESULT initialization_ = E_FAIL;
};
DWORD ErrorCode(HRESULT result) {
    if (SUCCEEDED(result)) return ERROR_SUCCESS;
    return HRESULT_FACILITY(result) == FACILITY_WIN32 ? HRESULT_CODE(result) : static_cast<DWORD>(result);
}
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

bool LogonTaskEnabled() {
    Scheduler scheduler;
    if (FAILED(scheduler.status)) return false;
    ComPtr<IRegisteredTask> task;
    if (FAILED(scheduler.folder->GetTask(Bstr(scheduler.name.c_str()), &task))) return false;
    VARIANT_BOOL enabled = VARIANT_FALSE;
    return SUCCEEDED(task->get_Enabled(&enabled)) && enabled == VARIANT_TRUE;
}

DWORD ConfigureLogonTask(bool enabled) {
    Scheduler scheduler;
    if (FAILED(scheduler.status)) return ErrorCode(scheduler.status);
    if (!enabled) {
        const auto result = scheduler.folder->DeleteTask(Bstr(scheduler.name.c_str()), 0);
        return result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? ERROR_SUCCESS : ErrorCode(result);
    }
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) return GetLastError();
    ComPtr<ITaskDefinition> definition;
    HRESULT result = scheduler.service->NewTask(0, &definition);
    if (FAILED(result)) return ErrorCode(result);
    ComPtr<IRegistrationInfo> info;
    if (FAILED(result = definition->get_RegistrationInfo(&info))) return ErrorCode(result);
    if (FAILED(result = info->put_Author(Bstr(L"sixunited / sclliang")))) return ErrorCode(result);
    if (FAILED(result = info->put_Description(Bstr(L"Starts CpuOverlay in the current user's desktop with PawnIO sensor access.")))) return ErrorCode(result);
    ComPtr<IPrincipal> principal;
    if (FAILED(result = definition->get_Principal(&principal))) return ErrorCode(result);
    if (FAILED(result = principal->put_UserId(Bstr(scheduler.sid.c_str())))) return ErrorCode(result);
    if (FAILED(result = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN))) return ErrorCode(result);
    if (FAILED(result = principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST))) return ErrorCode(result);
    ComPtr<ITaskSettings> settings;
    if (FAILED(result = definition->get_Settings(&settings))) return ErrorCode(result);
    if (FAILED(result = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE))) return ErrorCode(result);
    if (FAILED(result = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE))) return ErrorCode(result);
    if (FAILED(result = settings->put_ExecutionTimeLimit(Bstr(L"PT0S")))) return ErrorCode(result);
    if (FAILED(result = settings->put_StartWhenAvailable(VARIANT_TRUE))) return ErrorCode(result);
    if (FAILED(result = settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW))) return ErrorCode(result);
    ComPtr<ITriggerCollection> triggers;
    if (FAILED(result = definition->get_Triggers(&triggers))) return ErrorCode(result);
    ComPtr<ITrigger> trigger;
    if (FAILED(result = triggers->Create(TASK_TRIGGER_LOGON, &trigger))) return ErrorCode(result);
    ComPtr<ILogonTrigger> logon;
    if (FAILED(result = trigger.As(&logon))) return ErrorCode(result);
    if (FAILED(result = logon->put_UserId(Bstr(scheduler.sid.c_str())))) return ErrorCode(result);
    ComPtr<IActionCollection> actions;
    if (FAILED(result = definition->get_Actions(&actions))) return ErrorCode(result);
    ComPtr<IAction> action;
    if (FAILED(result = actions->Create(TASK_ACTION_EXEC, &action))) return ErrorCode(result);
    ComPtr<IExecAction> execution;
    if (FAILED(result = action.As(&execution))) return ErrorCode(result);
    if (FAILED(result = execution->put_Path(Bstr(path)))) return ErrorCode(result);
    if (FAILED(result = execution->put_Arguments(Bstr(L"--startup")))) return ErrorCode(result);
    if (FAILED(result = execution->put_WorkingDirectory(Bstr(std::filesystem::path(path).parent_path().c_str())))) return ErrorCode(result);
    ComPtr<IRegisteredTask> registered;
    Variant user(scheduler.sid.c_str()), empty;
    result = scheduler.folder->RegisterTaskDefinition(Bstr(scheduler.name.c_str()), definition.Get(), TASK_CREATE_OR_UPDATE,
        user, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered);
    return ErrorCode(result);
}

DWORD RelaunchElevated(const wchar_t* arguments) {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) return GetLastError();
    SHELLEXECUTEINFOW request{sizeof(request)};
    request.fMask = SEE_MASK_NOCLOSEPROCESS;
    request.lpVerb = L"runas";
    request.lpFile = path;
    request.lpParameters = arguments;
    request.nShow = SW_HIDE;
    if (!ShellExecuteExW(&request)) return GetLastError();
    if (request.hProcess) CloseHandle(request.hProcess);
    return ERROR_SUCCESS;
}
