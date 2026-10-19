#include "peripheral_battery.h"
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidclass.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <mutex>
#include <thread>
#include <vector>

namespace {
class Handle {
public:
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool Valid() const { return value && value != INVALID_HANDLE_VALUE; }
    HANDLE value;
};
struct Endpoint { std::wstring path; };
struct Discovery { std::array<Endpoint, 2> devices; DWORD error = 0; };
// Index 0 = keyboard; 1 = mouse. Match exact IDs and HID collection, not the
// generic keyboard/mouse nodes that also exist on composite receivers.
Discovery Discover(HANDLE cancel) {
    Discovery result;
    GUID guid{}; HidD_GetHidGuid(&guid);
    const HDEVINFO info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) { result.error = GetLastError(); return result; }
    for (DWORD index = 0; WaitForSingleObject(cancel, 0) != WAIT_OBJECT_0; ++index) {
        SP_DEVICE_INTERFACE_DATA iface{sizeof(iface)};
        if (!SetupDiEnumDeviceInterfaces(info, nullptr, &guid, index, &iface)) {
            const auto error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) result.error = error;
            break;
        }
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &size, nullptr);
        if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536) continue;
        std::vector<BYTE> buffer(size);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, size, nullptr, nullptr)) continue;
        std::wstring path(detail->DevicePath), lower(path);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        const bool keyboard = lower.find(L"vid_391d&pid_2a04") != std::wstring::npos;
        const bool mouse = lower.find(L"vid_1532&pid_00b8&mi_00") != std::wstring::npos;
        if (!keyboard && !mouse) continue;
        Handle file(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (!file.Valid()) { result.error = GetLastError(); continue; }
        HIDD_ATTRIBUTES attrs{sizeof(attrs)};
        PHIDP_PREPARSED_DATA data = nullptr;
        if (!HidD_GetAttributes(file.value, &attrs) || !HidD_GetPreparsedData(file.value, &data)) continue;
        HIDP_CAPS caps{}; const auto status = HidP_GetCaps(data, &caps);
        HidD_FreePreparsedData(data);
        if (status != HIDP_STATUS_SUCCESS) continue;
        if (keyboard && attrs.VendorID == 0x391d && attrs.ProductID == 0x2a04 && caps.UsagePage == 0xff60 && caps.Usage == 0x61 &&
            caps.InputReportByteLength == 64 && caps.OutputReportByteLength == 64) result.devices[0].path = path;
        if (mouse && attrs.VendorID == 0x1532 && attrs.ProductID == 0x00b8 && caps.UsagePage == 1 && caps.Usage == 2 &&
            caps.FeatureReportByteLength == 91) result.devices[1].path = path;
    }
    SetupDiDestroyDeviceInfoList(info);
    return result;
}

// Every transport operation is overlapped and bounded. Cancellation is drained
// before stack buffers/OVERLAPPED go out of scope (no detached I/O threads).
enum class Operation { Read, Write, SetFeature, GetFeature };
DWORD Exchange(HANDLE file, HANDLE cancel, Operation operation, BYTE* data, DWORD length, DWORD& transferred, DWORD timeout) {
    if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) return ERROR_OPERATION_ABORTED;
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.Valid()) return GetLastError();
    OVERLAPPED ov{}; ov.hEvent = event.value;
    BOOL started = FALSE;
    if (operation == Operation::Read) started = ReadFile(file, data, length, &transferred, &ov);
    else if (operation == Operation::Write) started = WriteFile(file, data, length, &transferred, &ov);
    else started = DeviceIoControl(file, operation == Operation::GetFeature ? IOCTL_HID_GET_FEATURE : IOCTL_HID_SET_FEATURE,
        data, length, data, length, &transferred, &ov);
    if (!started) {
        const auto error = GetLastError();
        if (error != ERROR_IO_PENDING) return error;
        const HANDLE waits[] = {cancel, event.value};
        const auto wait = WaitForMultipleObjects(2, waits, FALSE, timeout);
        if (wait != WAIT_OBJECT_0 + 1) {
            CancelIoEx(file, &ov);
            GetOverlappedResult(file, &ov, &transferred, TRUE);
            return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
        }
    }
    if (!GetOverlappedResult(file, &ov, &transferred, FALSE)) return GetLastError();
    return ERROR_SUCCESS;
}
struct QueryResult { std::optional<BatteryProtocol::Reading> reading; DWORD error = ERROR_SUCCESS; };
QueryResult Query(const Endpoint& endpoint, bool mouse, HANDLE cancel) {
    Handle file(CreateFileW(endpoint.path.c_str(), mouse ? 0 : (GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    if (!file.Valid()) return {{}, GetLastError()};
    DWORD bytes = 0;
    if (mouse) {
        auto request = BatteryProtocol::RazerRequest();
        auto error = Exchange(file.value, cancel, Operation::SetFeature, request.data(), static_cast<DWORD>(request.size()), bytes, 300);
        if (error) return {{}, error};
        for (int i = 0; i < 3; ++i) {
            if (WaitForSingleObject(cancel, 50) == WAIT_OBJECT_0) return {{}, ERROR_OPERATION_ABORTED};
            std::array<BYTE, 91> response{};
            error = Exchange(file.value, cancel, Operation::GetFeature, response.data(), static_cast<DWORD>(response.size()), bytes, 300);
            if (error) return {{}, error};
            // HID's unnumbered Feature reports exclude the zero Report ID from bytes returned.
            const size_t length = static_cast<size_t>(bytes) + (response[0] == 0 ? 1 : 0);
            if (length != response.size()) return {{}, ERROR_INVALID_DATA};
            if (response[1] == 1) continue; // device busy, bounded retry of the same query
            auto value = BatteryProtocol::ParseRazer(response.data(), length);
            return {value, static_cast<DWORD>(value ? ERROR_SUCCESS : ERROR_INVALID_DATA)};
        }
        return {{}, ERROR_TIMEOUT};
    }
    auto request = BatteryProtocol::VgnRequest();
    auto error = Exchange(file.value, cancel, Operation::Write, request.data(), static_cast<DWORD>(request.size()), bytes, 300);
    if (error || bytes != request.size()) return {{}, error ? error : ERROR_WRITE_FAULT};
    const auto deadline = GetTickCount64() + 600;
    while (GetTickCount64() < deadline) {
        std::array<BYTE, 64> response{};
        const DWORD remaining = static_cast<DWORD>(deadline - GetTickCount64());
        if (remaining > 600) break;
        error = Exchange(file.value, cancel, Operation::Read, response.data(), static_cast<DWORD>(response.size()), bytes, remaining);
        if (error) return {{}, error};
        if (auto value = BatteryProtocol::ParseVgn(response.data(), bytes)) return {value, ERROR_SUCCESS};
        // Ignore unrelated asynchronous reports, but never display their bytes as a battery level.
    }
    return {{}, ERROR_TIMEOUT};
}
}

struct PeripheralBatteryMonitor::Impl {
    mutable std::mutex mutex;
    std::condition_variable changed;
    Handle cancel{CreateEventW(nullptr, TRUE, TRUE, nullptr)};
    std::thread thread;
    bool active = false, stop = false;
    unsigned generation = 0;
    PeripheralSample sample;
    std::array<std::wstring, 2> identities;
    std::array<ULONGLONG, 2> due{};

    ~Impl() {
        { std::lock_guard<std::mutex> lock(mutex); stop = true; SetEvent(cancel.value); }
        changed.notify_all();
        if (thread.joinable()) thread.join();
    }
    void Run() noexcept {
        try {
            std::unique_lock<std::mutex> lock(mutex);
            while (!stop) {
                changed.wait(lock, [this] { return stop || active; });
                if (stop) break;
                const auto next = std::min(due[0], due[1]), now = GetTickCount64();
                if (next > now) { changed.wait_for(lock, std::chrono::milliseconds(next - now)); continue; }
                const auto token = generation;
                const std::array<bool, 2> query = {due[0] <= now, due[1] <= now};
                ResetEvent(cancel.value);
                lock.unlock();
                const auto found = Discover(cancel.value);
                for (size_t i = 0; i < 2; ++i) {
                    if (!query[i] || WaitForSingleObject(cancel.value, 0) == WAIT_OBJECT_0) continue;
                    const bool present = !found.devices[i].path.empty();
                    const auto result = present ? Query(found.devices[i], i == 1, cancel.value) :
                        QueryResult{{}, found.error ? found.error : ERROR_DEVICE_NOT_CONNECTED};
                    const auto tick = GetTickCount64();
                    lock.lock();
                    if (!stop && active && token == generation && result.error != ERROR_OPERATION_ABORTED) {
                        auto& value = i == 0 ? sample.keyboard : sample.mouse;
                        if (present && identities[i] != found.devices[i].path) { value = {}; identities[i] = found.devices[i].path; }
                        UpdateBattery(value, result.reading, present || found.error, tick, result.error);
                        due[i] = tick + (result.reading ? 30000 : 5000);
                    }
                    lock.unlock();
                }
                lock.lock();
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            UpdateBattery(sample.keyboard, {}, true, GetTickCount64(), ERROR_GEN_FAILURE);
            UpdateBattery(sample.mouse, {}, true, GetTickCount64(), ERROR_GEN_FAILURE);
        }
    }
};
PeripheralBatteryMonitor::PeripheralBatteryMonitor() : impl_(std::make_unique<Impl>()) {}
PeripheralBatteryMonitor::~PeripheralBatteryMonitor() = default;
void PeripheralBatteryMonitor::SetActive(bool active) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->cancel.Valid()) return;
    impl_->active = active; ++impl_->generation; impl_->due = {};
    SetEvent(impl_->cancel.value); // interrupt any in-flight query from the previous generation
    if (active && !impl_->thread.joinable()) {
        try { impl_->thread = std::thread(&Impl::Run, impl_.get()); }
        catch (...) { impl_->active = false; }
    }
    impl_->changed.notify_all();
}
PeripheralSample PeripheralBatteryMonitor::Snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto now = GetTickCount64();
    return {AgeBattery(impl_->sample.keyboard, now), AgeBattery(impl_->sample.mouse, now)};
}
