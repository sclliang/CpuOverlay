#include "pump_display.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
class Handle {
public:
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { Reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool Valid() const { return value && value != INVALID_HANDLE_VALUE; }
    void Reset(HANDLE h = INVALID_HANDLE_VALUE) { if (Valid()) CloseHandle(value); value = h; }
    HANDLE value;
};
DWORD Connect(Handle& output, HANDLE cancel) {
    GUID guid{}; HidD_GetHidGuid(&guid);
    const auto devices = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD error = ERROR_DEVICE_NOT_CONNECTED;
    for (DWORD index = 0; WaitForSingleObject(cancel, 0) != WAIT_OBJECT_0; ++index) {
        SP_DEVICE_INTERFACE_DATA iface{sizeof(iface)};
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, index, &iface)) break;
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &iface, nullptr, 0, &size, nullptr);
        if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536) continue;
        std::vector<BYTE> buffer(size);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &iface, detail, size, nullptr, nullptr)) continue;
        std::wstring path(detail->DevicePath), lower(path);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        if (lower.find(L"vid_5131&pid_2007") == std::wstring::npos) continue;
        Handle metadata(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (!metadata.Valid()) { error = GetLastError(); continue; }
        HIDD_ATTRIBUTES attrs{sizeof(attrs)};
        PHIDP_PREPARSED_DATA data = nullptr;
        if (!HidD_GetAttributes(metadata.value, &attrs) || !HidD_GetPreparsedData(metadata.value, &data)) continue;
        HIDP_CAPS caps{}; const auto status = HidP_GetCaps(data, &caps);
        HidD_FreePreparsedData(data);
        if (status != HIDP_STATUS_SUCCESS || attrs.VendorID != 0x5131 || attrs.ProductID != 0x2007 ||
            caps.UsagePage != 0xff00 || caps.Usage != 1 || caps.OutputReportByteLength != 65) continue;
        // One writer at a time: do not alternate frames with the vendor app.
        output.Reset(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        error = output.Valid() ? ERROR_SUCCESS : GetLastError();
        if (output.Valid()) break;
    }
    SetupDiDestroyDeviceInfoList(devices);
    return error;
}
DWORD WriteReport(HANDLE device, HANDLE cancel, const PumpProtocol::Report& report) {
    if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) return ERROR_OPERATION_ABORTED;
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.Valid()) return GetLastError();
    OVERLAPPED ov{}; ov.hEvent = event.value;
    DWORD bytes = 0;
    if (!WriteFile(device, report.data(), static_cast<DWORD>(report.size()), &bytes, &ov)) {
        const auto error = GetLastError();
        if (error != ERROR_IO_PENDING) return error;
        const HANDLE waits[] = {cancel, event.value};
        const auto wait = WaitForMultipleObjects(2, waits, FALSE, 300);
        if (wait != WAIT_OBJECT_0 + 1) {
            CancelIoEx(device, &ov);
            GetOverlappedResult(device, &ov, &bytes, TRUE); // drain before destroying buffers
            return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
        }
    }
    if (!GetOverlappedResult(device, &ov, &bytes, FALSE)) return GetLastError();
    return bytes == report.size() ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
}
}

struct PumpDisplay::Impl {
    mutable std::mutex mutex;
    std::condition_variable changed;
    Handle cancel{CreateEventW(nullptr, TRUE, TRUE, nullptr)};
    std::thread thread;
    bool active = false, stop = false;
    unsigned generation = 0;
    std::optional<PumpProtocol::Report> report;
    uint64_t updated = 0;
    PumpDisplayStatus status;
    ~Impl() {
        { std::lock_guard<std::mutex> lock(mutex); stop = true; SetEvent(cancel.value); }
        changed.notify_all();
        if (thread.joinable()) thread.join();
    }
    void Run() noexcept {
        try {
            Handle device;
            uint64_t reconnectAt = 0;
            std::unique_lock<std::mutex> lock(mutex);
            while (!stop) {
                if (!active) {
                    device.Reset(); status.connected = false; reconnectAt = 0;
                    changed.wait(lock, [this] { return stop || active; });
                    continue;
                }
                const auto token = generation;
                ResetEvent(cancel.value);
                if (!device.Valid() && GetTickCount64() >= reconnectAt) {
                    lock.unlock();
                    const auto error = Connect(device, cancel.value);
                    lock.lock();
                    if (stop || !active || generation != token) { device.Reset(); continue; }
                    status.error = error; status.connected = device.Valid();
                    reconnectAt = GetTickCount64() + 5000;
                }
                if (device.Valid() && report && PumpProtocol::Fresh(updated, GetTickCount64())) {
                    const auto frame = *report;
                    lock.unlock();
                    const auto error = WriteReport(device.value, cancel.value, frame);
                    lock.lock();
                    if (stop || !active || generation != token) { device.Reset(); status.connected = false; continue; }
                    status.error = error;
                    if (!error) {
                        ++status.sent; status.lastSentMs = GetTickCount64();
                        status.temperatureC = frame[4] + frame[5] / 100.0;
                    } else {
                        device.Reset(); status.connected = false; reconnectAt = GetTickCount64() + 5000;
                    }
                }
                changed.wait_for(lock, std::chrono::milliseconds(200), [this, token] { return stop || generation != token; });
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            status.connected = false; status.error = ERROR_GEN_FAILURE;
        }
    }
};
PumpDisplay::PumpDisplay() : impl_(std::make_unique<Impl>()) {}
PumpDisplay::~PumpDisplay() = default;
void PumpDisplay::SetActive(bool active) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->active == active) return;
    if (!impl_->cancel.Valid()) { impl_->status.error = ERROR_INVALID_HANDLE; return; }
    impl_->active = active; impl_->status.active = active; ++impl_->generation;
    impl_->report.reset();
    if (!active) impl_->status.connected = false;
    SetEvent(impl_->cancel.value);
    if (active && !impl_->thread.joinable()) {
        try { impl_->thread = std::thread(&Impl::Run, impl_.get()); }
        catch (...) { impl_->active = false; impl_->status.active = false; impl_->status.error = ERROR_NOT_ENOUGH_MEMORY; }
    }
    impl_->changed.notify_all();
}
void PumpDisplay::Update(std::optional<double> temperature) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->report = PumpProtocol::TemperatureReport(temperature);
    impl_->updated = GetTickCount64();
}
PumpDisplayStatus PumpDisplay::Snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->status;
}
