#include "peripheral_battery.h"
#include "vendor_battery_protocol.h"
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidclass.h>
#include <algorithm>
#include <chrono>
#include <cctype>
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
struct Endpoint {
    std::wstring path;
    BatteryDevice device = BatteryDevice::Unknown;
    DWORD inputLength = 0, outputLength = 0;
    DWORD featureLength = 0;
    std::wstring model;
    uint16_t vendorId = 0, productId = 0;
};
struct Discovery { std::array<std::vector<Endpoint>, 2> devices; std::array<DWORD, 2> errors{}; DWORD error = 0; };
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
        bool supportedVendor = false;
        for (const auto vendor : {L"vid_391d", L"vid_046d", L"vid_1532", L"vid_3554", L"vid_3837", L"vid_0b05", L"vid_373b", L"vid_a8a5"})
            if (lower.find(vendor) != std::wstring::npos) supportedVendor = true;
        if (!supportedVendor) continue;
        Handle file(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (!file.Valid()) continue;
        HIDD_ATTRIBUTES attrs{sizeof(attrs)};
        PHIDP_PREPARSED_DATA data = nullptr;
        if (!HidD_GetAttributes(file.value, &attrs) || !HidD_GetPreparsedData(file.value, &data)) continue;
        HIDP_CAPS caps{}; const auto status = HidP_GetCaps(data, &caps);
        HidD_FreePreparsedData(data);
        if (status != HIDP_STATUS_SUCCESS) continue;
        wchar_t product[256]{};
        HidD_GetProductString(file.value, product, sizeof(product));
        product[255] = 0;
        auto add = [&](size_t kind, BatteryDevice device, const wchar_t* fallback) {
            result.devices[kind].push_back({path, device, caps.InputReportByteLength, caps.OutputReportByteLength,
                caps.FeatureReportByteLength, product[0] ? product : fallback, attrs.VendorID, attrs.ProductID});
        };
        if (attrs.VendorID == 0x046d && caps.UsagePage == 0xff00 && caps.Usage == 2 &&
            caps.InputReportByteLength >= 20 && caps.InputReportByteLength <= 64 && caps.OutputReportByteLength == 20) {
            // The paired-device type is queried through 0x0005; a Bolt PID alone
            // does not tell us whether the receiver hosts a keyboard or mouse.
            add(0, BatteryDevice::LogitechHidpp, L"Logitech HID++ keyboard");
            add(1, BatteryDevice::LogitechHidpp, L"Logitech HID++ mouse");
        }
        if (attrs.VendorID == 0x391d && attrs.ProductID == 0x2a04 && caps.UsagePage == 0xff60 && caps.Usage == 0x61 &&
            caps.InputReportByteLength == 64 && caps.OutputReportByteLength == 64)
            add(0, BatteryDevice::VgnV98Pro, L"VGN V98Pro V4");
        if (attrs.VendorID == 0x1532 && BatteryProtocol::RazerWirelessPid(attrs.ProductID) &&
            caps.FeatureReportByteLength >= 91)
            add(1, BatteryDevice::RazerWireless, L"Razer wireless mouse");
        if (attrs.VendorID == 0x3554 && attrs.ProductID == 0xfa09 && caps.UsagePage == 0xff02 &&
            caps.InputReportByteLength == 20 && caps.OutputReportByteLength == 20)
            add(0, BatteryDevice::MchoseKeyboard, L"MCHOSE K99 v2 / G75 Pro");
        if (attrs.VendorID == 0x0b05 && attrs.ProductID == 0x1ace && caps.UsagePage == 0xff00 &&
            caps.InputReportByteLength == 64 && caps.OutputReportByteLength == 64)
            add(0, BatteryDevice::RogKeyboard, L"ROG OMNI / AZOTH EXTREME");
        if (((attrs.VendorID == 0x3554 && attrs.ProductID == 0xfb34) || (attrs.VendorID == 0x373b && attrs.ProductID == 0x1278)) &&
            caps.UsagePage == 0xff02 && caps.InputReportByteLength == 17 && caps.OutputReportByteLength == 17)
            add(1, BatteryDevice::CompxMouse, attrs.VendorID == 0x3554 ? L"IMECOO P1" : L"ATK A9 Air");
        if (attrs.VendorID == 0x3837 && attrs.ProductID == 0x1014 && caps.UsagePage == 0xff01 && caps.Usage == 1 &&
            caps.InputReportByteLength == 64 && caps.OutputReportByteLength == 64 && lower.find(L"&mi_02&") != std::wstring::npos)
            add(1, BatteryDevice::MchoseK7Mouse, L"MCHOSE K7 V2 Ultra+");
        if (attrs.VendorID == 0x3837 && attrs.ProductID != 0x1014 && caps.UsagePage == 0xff01 &&
            caps.FeatureReportByteLength == 65 && caps.InputReportByteLength >= 3 && caps.InputReportByteLength <= 65)
            add(1, BatteryDevice::MchoseA5Mouse, L"MCHOSE A5 V3");
        if (attrs.VendorID == 0xa8a5 && attrs.ProductID == 0x2255 && caps.UsagePage == 0xff01 && caps.Usage == 0x10 &&
            caps.InputReportByteLength == 65 && caps.OutputReportByteLength == 65)
            add(1, BatteryDevice::TaiduMouse, L"TAIDU TSG809");
    }
    SetupDiDestroyDeviceInfoList(info);
    std::stable_sort(result.devices[0].begin(), result.devices[0].end(),
        [](const Endpoint& a, const Endpoint& b) { return a.device < b.device; });
    std::stable_sort(result.devices[1].begin(), result.devices[1].end(), [](const Endpoint& a, const Endpoint& b) {
        if ((a.device == BatteryDevice::LogitechHidpp) != (b.device == BatteryDevice::LogitechHidpp))
            return b.device == BatteryDevice::LogitechHidpp; // Probe concrete mouse protocols before scanning generic receiver slots.
        return (a.path.find(L"&mi_00") != std::wstring::npos) > (b.path.find(L"&mi_00") != std::wstring::npos);
    });
    return result;
}

// Every transport operation is overlapped and bounded. Cancellation is drained
// before stack buffers/OVERLAPPED go out of scope (no detached I/O threads).
enum class Operation { Read, Write, SetFeature, GetFeature, SetOutput };
DWORD Exchange(HANDLE file, HANDLE cancel, Operation operation, BYTE* data, DWORD length, DWORD& transferred, DWORD timeout) {
    if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) return ERROR_OPERATION_ABORTED;
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.Valid()) return GetLastError();
    OVERLAPPED ov{}; ov.hEvent = event.value;
    BOOL started = FALSE;
    if (operation == Operation::Read) started = ReadFile(file, data, length, &transferred, &ov);
    else if (operation == Operation::Write) started = WriteFile(file, data, length, &transferred, &ov);
    else started = DeviceIoControl(file, operation == Operation::SetOutput ? IOCTL_HID_SET_OUTPUT_REPORT :
        operation == Operation::GetFeature ? IOCTL_HID_GET_FEATURE : IOCTL_HID_SET_FEATURE,
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
struct QueryResult { std::optional<BatteryProtocol::Reading> reading; DWORD error = ERROR_SUCCESS; std::wstring model; };
DWORD Remaining(ULONGLONG deadline, DWORD maximum) {
    const auto now = GetTickCount64();
    return now >= deadline ? 0 : static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, maximum));
}
struct HidppReply { std::array<BYTE, 16> parameters{}; size_t size = 0; };
struct LogitechSlot { std::wstring path; BYTE slot = 0; std::wstring name; };
class HidppSession {
public:
    HidppSession(HANDLE file, HANDLE cancel, const Endpoint& endpoint, ULONGLONG deadline)
        : file_(file), cancel_(cancel), endpoint_(endpoint), deadline_(deadline) {}
    DWORD Request(BYTE slot, BYTE feature, BYTE function, std::initializer_list<BYTE> parameters,
        HidppReply& reply, DWORD timeout = 700) {
        const auto request = BatteryProtocol::HidppRequest(slot, feature, function, parameters);
        if (!request) return ERROR_INVALID_PARAMETER;
        const auto deadline = std::min(deadline_, GetTickCount64() + timeout);
        auto remaining = Remaining(deadline, 500);
        if (!remaining) return ERROR_TIMEOUT;
        std::vector<BYTE> output(endpoint_.outputLength);
        std::copy(request->begin(), request->end(), output.begin());
        DWORD bytes = 0;
        auto error = Exchange(file_, cancel_, Operation::Write, output.data(), static_cast<DWORD>(output.size()), bytes, remaining);
        if (error || bytes != output.size()) return error ? error : ERROR_WRITE_FAULT;
        std::vector<BYTE> input(endpoint_.inputLength);
        while ((remaining = Remaining(deadline, timeout)) != 0) {
            error = Exchange(file_, cancel_, Operation::Read, input.data(), static_cast<DWORD>(input.size()), bytes, remaining);
            if (error) return error;
            if (BatteryProtocol::HidppError(input.data(), bytes, *request)) return ERROR_NOT_SUPPORTED;
            if (!BatteryProtocol::HidppMatches(input.data(), bytes, *request)) continue;
            // A short HID++ reply may be padded to the collection's maximum length.
            reply.size = input[0] == 0x11 ? 16 : 3;
            std::copy_n(input.begin() + 4, reply.size, reply.parameters.begin());
            return ERROR_SUCCESS;
        }
        return ERROR_TIMEOUT;
    }
    DWORD Name(BYTE slot, std::string& name, BYTE& kind) {
        HidppReply reply;
        auto error = Request(slot, 0, 0, {0, 5, 0}, reply);
        if (error) return error;
        const auto feature = reply.parameters[0];
        if (!feature) return ERROR_NOT_SUPPORTED;
        error = Request(slot, feature, 0, {}, reply);
        if (error) return error;
        const size_t length = reply.parameters[0];
        if (!length || length > 80) return ERROR_INVALID_DATA;
        while (name.size() < length) {
            error = Request(slot, feature, 1, {static_cast<BYTE>(name.size())}, reply);
            if (error) return error;
            name.append(reinterpret_cast<const char*>(reply.parameters.data()), std::min(length - name.size(), reply.size));
        }
        error = Request(slot, feature, 2, {}, reply);
        if (error) return error;
        kind = reply.parameters[0];
        return ERROR_SUCCESS;
    }
private:
    HANDLE file_, cancel_;
    const Endpoint& endpoint_;
    ULONGLONG deadline_;
};
QueryResult QueryLogitech(HANDLE file, HANDLE cancel, const Endpoint& endpoint, LogitechSlot& known, ULONGLONG deadline, bool mouse) {
    if (known.path != endpoint.path) { known.path = endpoint.path; known.slot = 0; }
    const auto cachedSlot = known.slot;
    HidppSession session(file, cancel, endpoint, deadline);
    DWORD error = ERROR_NOT_FOUND;
    for (BYTE slot = cachedSlot ? cachedSlot : 1; slot <= (cachedSlot ? cachedSlot : 6); ++slot) {
        if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) return {{}, ERROR_OPERATION_ABORTED};
        if (!Remaining(deadline, 1)) break;
        HidppReply reply;
        error = session.Request(slot, 0, 1, {0, 0, 0x5a}, reply, 2500);
        if (error) continue;
        if (reply.parameters[0] < 2 || reply.parameters[2] != 0x5a) { error = ERROR_INVALID_DATA; continue; }
        std::wstring model = known.name;
        if (!cachedSlot) {
            std::string name;
            BYTE kind = 0xff;
            error = session.Name(slot, name, kind);
            if (error) continue;
            if (!BatteryProtocol::LogitechKind(kind, mouse)) { error = ERROR_NOT_FOUND; continue; }
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0);
            if (length <= 0) { error = ERROR_INVALID_DATA; continue; }
            model.resize(static_cast<size_t>(length));
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), model.data(), length);
        }
        for (const auto featureId : {0x1004, 0x1000}) {
            error = session.Request(slot, 0, 0, {static_cast<BYTE>(featureId >> 8), static_cast<BYTE>(featureId), 0}, reply);
            if (error) break;
            const auto feature = reply.parameters[0];
            if (!feature) { error = ERROR_NOT_SUPPORTED; continue; }
            const bool unified = featureId == 0x1004;
            bool supportsPercent = true;
            if (unified) {
                error = session.Request(slot, feature, 0, {}, reply);
                if (error) break;
                supportsPercent = (reply.parameters[1] & 2) != 0;
            }
            error = session.Request(slot, feature, unified ? 1 : 0, {}, reply, 900);
            if (error) break;
            if (auto reading = BatteryProtocol::ParseLogitech(reply.parameters.data(), reply.size, unified, supportsPercent)) {
                known.slot = slot;
                known.name = model;
                return {reading, ERROR_SUCCESS, model};
            }
            error = supportsPercent ? ERROR_INVALID_DATA : ERROR_NOT_SUPPORTED;
        }
    }
    known.slot = 0;
    return {{}, error};
}
QueryResult QueryRazer(HANDLE file, HANDLE cancel, uint16_t productId) {
    DWORD bytes = 0;
    DWORD error = ERROR_TIMEOUT;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto transaction = BatteryProtocol::RazerTransaction(productId);
        auto request = BatteryProtocol::RazerRequest(transaction);
        error = Exchange(file, cancel, Operation::SetFeature, request.data(), static_cast<DWORD>(request.size()), bytes, 300);
        if (!error) {
            if (WaitForSingleObject(cancel, 100) == WAIT_OBJECT_0) return {{}, ERROR_OPERATION_ABORTED};
            std::array<BYTE, 91> response{};
            error = Exchange(file, cancel, Operation::GetFeature, response.data(), static_cast<DWORD>(response.size()), bytes, 300);
            if (!error) {
                // Windows HID stacks may include or exclude the zero Report ID in the transfer count.
                if (bytes == response.size() || bytes == response.size() - 1) {
                    if (auto value = BatteryProtocol::ParseRazer(response.data(), response.size(), transaction)) {
                        if (BatteryProtocol::RazerCanCharge(productId)) {
                            auto charging = BatteryProtocol::RazerRequest(transaction, 0x84);
                            const auto sent = Exchange(file, cancel, Operation::SetFeature, charging.data(), static_cast<DWORD>(charging.size()), bytes, 300);
                            if (sent == ERROR_OPERATION_ABORTED || WaitForSingleObject(cancel, sent ? 0 : 100) == WAIT_OBJECT_0)
                                return {{}, ERROR_OPERATION_ABORTED};
                            if (!sent) {
                                response = {};
                                const auto received = Exchange(file, cancel, Operation::GetFeature, response.data(), static_cast<DWORD>(response.size()), bytes, 300);
                                if (received == ERROR_OPERATION_ABORTED) return {{}, received};
                                if (!received && (bytes == 90 || bytes == 91))
                                    if (auto charge = BatteryProtocol::ParseRazerCharging(response.data(), response.size(), transaction)) value->charging = *charge;
                            }
                        }
                        return {value, ERROR_SUCCESS};
                    }
                }
                error = response[1] == 1 ? ERROR_BUSY : ERROR_INVALID_DATA;
            }
        }
        if (error == ERROR_OPERATION_ABORTED || WaitForSingleObject(cancel, attempt < 2 ? 160 : 0) == WAIT_OBJECT_0)
            return {{}, ERROR_OPERATION_ABORTED};
    }
    return {{}, error};
}
QueryResult QueryVgn(HANDLE file, HANDLE cancel) {
    DWORD bytes = 0;
    auto request = BatteryProtocol::VgnRequest();
    auto error = Exchange(file, cancel, Operation::Write, request.data(), static_cast<DWORD>(request.size()), bytes, 300);
    if (error || bytes != request.size()) return {{}, error ? error : ERROR_WRITE_FAULT};
    const auto deadline = GetTickCount64() + 600;
    while (GetTickCount64() < deadline) {
        std::array<BYTE, 64> response{};
        const auto remaining = Remaining(deadline, 600);
        if (!remaining) break;
        error = Exchange(file, cancel, Operation::Read, response.data(), static_cast<DWORD>(response.size()), bytes, remaining);
        if (error) return {{}, error};
        if (auto value = BatteryProtocol::ParseVgn(response.data(), bytes)) return {value, ERROR_SUCCESS};
        // Ignore unrelated asynchronous reports, but never display their bytes as a battery level.
    }
    return {{}, ERROR_TIMEOUT};
}
void DrainInput(HANDLE file, HANDLE cancel, const Endpoint& endpoint) {
    if (!endpoint.inputLength) return;
    std::vector<BYTE> buffer(endpoint.inputLength);
    const auto deadline = GetTickCount64() + 30;
    for (int i = 0; i < 8; ++i) {
        const auto remaining = Remaining(deadline, 30);
        if (!remaining) break;
        DWORD bytes = 0;
        if (Exchange(file, cancel, Operation::Read, buffer.data(), static_cast<DWORD>(buffer.size()), bytes, remaining)) break;
    }
}
QueryResult QueryVendor(HANDLE file, HANDLE cancel, const Endpoint& endpoint, ULONGLONG deadline) {
    DrainInput(file, cancel, endpoint);
    auto request = BatteryProtocol::VendorRequest(endpoint.device);
    if (request.empty()) return {{}, ERROR_NOT_SUPPORTED};
    std::vector<BYTE> response(endpoint.inputLength);
    DWORD error = ERROR_TIMEOUT, bytes = 0;
    for (int attempt = 0; attempt < 3 && Remaining(deadline, 1); ++attempt) {
        if (attempt && WaitForSingleObject(cancel, 120) == WAIT_OBJECT_0) return {{}, ERROR_OPERATION_ABORTED};
        const bool outputReport = endpoint.device == BatteryDevice::MchoseKeyboard ||
            (endpoint.device == BatteryDevice::CompxMouse && endpoint.vendorId == 0x373b);
        auto send = [&](Operation operation) {
            const auto timeout = Remaining(deadline, 500);
            if (!timeout) return static_cast<DWORD>(ERROR_TIMEOUT);
            auto result = Exchange(file, cancel, operation, request.data(), static_cast<DWORD>(request.size()), bytes, timeout);
            if (!result && operation == Operation::Write && bytes != request.size()) result = ERROR_WRITE_FAULT;
            return result;
        };
        error = send(outputReport ? Operation::SetOutput : Operation::Write);
        if (error && error != ERROR_OPERATION_ABORTED) error = send(outputReport ? Operation::Write : Operation::SetOutput);
        if (error == ERROR_OPERATION_ABORTED) return {{}, error};
        if (error) continue;
        const auto until = std::min(deadline, GetTickCount64() + 900);
        while (const auto remaining = Remaining(until, 900)) {
            error = Exchange(file, cancel, Operation::Read, response.data(), static_cast<DWORD>(response.size()), bytes, remaining);
            if (error) break;
            if (const auto reading = BatteryProtocol::ParseVendor(endpoint.device, response.data(), bytes)) return {reading, ERROR_SUCCESS};
            error = ERROR_INVALID_DATA; // echoes and unrelated events never become measurements
        }
        if (error == ERROR_OPERATION_ABORTED) return {{}, error};
    }
    return {{}, error};
}
QueryResult QueryMchoseA5(HANDLE file, HANDLE cancel, const Endpoint& endpoint, ULONGLONG deadline) {
    DrainInput(file, cancel, endpoint);
    auto bond = BatteryProtocol::MchoseA5Request(3); // read-only bond-status query; not a pairing command
    DWORD bytes = 0;
    auto error = Exchange(file, cancel, Operation::SetFeature, bond.data(), static_cast<DWORD>(bond.size()), bytes, 300);
    if (error) return {{}, error};
    if (WaitForSingleObject(cancel, 120) == WAIT_OBJECT_0) return {{}, ERROR_OPERATION_ABORTED};
    DrainInput(file, cancel, endpoint);
    std::vector<BYTE> notification(endpoint.inputLength);
    for (int attempt = 0; attempt < 3 && Remaining(deadline, 1); ++attempt) {
        if (attempt && WaitForSingleObject(cancel, 200) == WAIT_OBJECT_0) return {{}, ERROR_OPERATION_ABORTED};
        auto request = BatteryProtocol::MchoseA5Request(6);
        error = Exchange(file, cancel, Operation::SetFeature, request.data(), static_cast<DWORD>(request.size()), bytes, 300);
        if (error) return {{}, error};
        bool acknowledged = false;
        const auto until = std::min(deadline, GetTickCount64() + 1500);
        while (const auto remaining = Remaining(until, 1500)) {
            error = Exchange(file, cancel, Operation::Read, notification.data(), static_cast<DWORD>(notification.size()), bytes, remaining);
            if (error) break;
            if (BatteryProtocol::MchoseA5Ack(notification.data(), bytes)) { acknowledged = true; break; }
        }
        if (error == ERROR_OPERATION_ABORTED) return {{}, error};
        if (!acknowledged) { error = ERROR_TIMEOUT; continue; }
        // Reading before the interrupt ACK can expose the device's stale feature bank.
        for (int bank = 0; bank < 4; ++bank) {
            std::array<BYTE, 65> response{}; response[0] = 0x11;
            error = Exchange(file, cancel, Operation::GetFeature, response.data(), static_cast<DWORD>(response.size()), bytes, 300);
            if (error) break;
            if (bytes == 64 || bytes == 65)
                if (const auto reading = BatteryProtocol::ParseMchoseA5(response.data(), response.size())) return {reading, ERROR_SUCCESS};
            error = ERROR_INVALID_DATA;
        }
        if (error == ERROR_OPERATION_ABORTED) return {{}, error};
    }
    return {{}, error};
}
QueryResult Query(const Endpoint& endpoint, HANDLE cancel, LogitechSlot& known, ULONGLONG deadline, bool mouse) {
    const bool featureOnly = endpoint.device == BatteryDevice::RazerWireless;
    Handle file(CreateFileW(endpoint.path.c_str(), featureOnly ? 0 : (GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    if (!file.Valid()) return {{}, GetLastError()};
    if (featureOnly) return QueryRazer(file.value, cancel, endpoint.productId);
    if (endpoint.device == BatteryDevice::LogitechHidpp) return QueryLogitech(file.value, cancel, endpoint, known, deadline, mouse);
    if (endpoint.device == BatteryDevice::VgnV98Pro) return QueryVgn(file.value, cancel);
    if (endpoint.device == BatteryDevice::MchoseA5Mouse) return QueryMchoseA5(file.value, cancel, endpoint, deadline);
    return QueryVendor(file.value, cancel, endpoint, deadline);
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
    std::array<LogitechSlot, 2> logitech;

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
                const auto previousIdentities = identities;
                ResetEvent(cancel.value);
                lock.unlock();
                const auto found = Discover(cancel.value);
                // A sleeping Bolt slot must not delay the first mouse reading.
                for (const size_t i : {size_t{1}, size_t{0}}) {
                    if (!query[i] || WaitForSingleObject(cancel.value, 0) == WAIT_OBJECT_0) continue;
                    const bool present = !found.devices[i].empty();
                    const auto discoveryError = found.errors[i] ? found.errors[i] : found.error;
                    QueryResult result{{}, discoveryError ? discoveryError : ERROR_DEVICE_NOT_CONNECTED};
                    const Endpoint* selected = nullptr;
                    const auto deadline = GetTickCount64() + 18000;
                    for (const auto& endpoint : found.devices[i]) {
                        auto candidate = Query(endpoint, cancel.value, logitech[i], deadline, i == 1);
                        if (candidate.reading || candidate.error == ERROR_OPERATION_ABORTED) {
                            selected = &endpoint; result = std::move(candidate); break;
                        }
                        // A later, unrelated receiver that fails to answer must
                        // not replace the previous device or its failure status.
                        if (!selected || endpoint.path == previousIdentities[i]) {
                            selected = &endpoint; result = std::move(candidate);
                        }
                        if (!Remaining(deadline, 1)) break;
                    }
                    const auto tick = GetTickCount64();
                    lock.lock();
                    if (!stop && active && token == generation && result.error != ERROR_OPERATION_ABORTED) {
                        auto& value = i == 0 ? sample.keyboard : sample.mouse;
                        // Change identity only after a successful reading, or
                        // when identifying the first candidate of this session.
                        if (selected && (result.reading || identities[i].empty())) {
                            if (identities[i] != selected->path) { value = {}; identities[i] = selected->path; }
                            value.device = selected->device;
                            value.model = result.model.empty() ? selected->model : result.model;
                            value.vendorId = selected->vendorId; value.productId = selected->productId;
                        }
                        const bool sourcePresent = identities[i].empty() ? present :
                            std::any_of(found.devices[i].begin(), found.devices[i].end(),
                                [&](const Endpoint& endpoint) { return endpoint.path == identities[i]; });
                        UpdateBattery(value, result.reading, sourcePresent || discoveryError, tick,
                            sourcePresent || discoveryError ? result.error : ERROR_DEVICE_NOT_CONNECTED);
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
