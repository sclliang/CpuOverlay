#include "gpu_sampler.h"
#include "gpu_sensor_selection.h"
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
struct NativeSensor { double value, minimum, maximum, average; int type, hardware; };
std::wstring Wide(const char* text) {
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(static_cast<size_t>(length), 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), length);
    result.pop_back();
    return result;
}
}
std::wstring GpuCounterKey(uint64_t luid) {
    wchar_t key[48]{};
    swprintf_s(key, L"luid_0x%08x_0x%08x_", static_cast<DWORD>(luid >> 32), static_cast<DWORD>(luid));
    return key;
}
struct GpuSampler::Impl {
    HMODULE bridge = nullptr;
    int (__cdecl* open)() = nullptr;
    int (__cdecl* refresh)() = nullptr;
    int (__cdecl* count)() = nullptr;
    int (__cdecl* get)(int, NativeSensor*, char*, int, char*, int, char*, int) = nullptr;
    void (__cdecl* close)() = nullptr;
    bool attempted = false, opened = false;
    DWORD bridgeError = 0;
    PDH_HQUERY memoryQuery = nullptr;
    PDH_HCOUNTER memoryCounter = nullptr;
    ~Impl() {
        if (opened) close();
        if (bridge) FreeLibrary(bridge);
        if (memoryQuery) PdhCloseQuery(memoryQuery);
    }
    void OpenBridge() {
        if (attempted) return;
        attempted = true;
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable))) { bridgeError = GetLastError(); return; }
        const auto path = std::filesystem::path(executable).parent_path() / L"bin" / L"gpu" / L"LhmCpuSensorBridge.dll";
        bridge = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!bridge) { bridgeError = GetLastError(); return; }
        open = reinterpret_cast<decltype(open)>(GetProcAddress(bridge, "lhm_open"));
        refresh = reinterpret_cast<decltype(refresh)>(GetProcAddress(bridge, "lhm_refresh"));
        count = reinterpret_cast<decltype(count)>(GetProcAddress(bridge, "lhm_sensor_count"));
        get = reinterpret_cast<decltype(get)>(GetProcAddress(bridge, "lhm_get_sensor"));
        close = reinterpret_cast<decltype(close)>(GetProcAddress(bridge, "lhm_close"));
        if (!open || !refresh || !count || !get || !close) { bridgeError = ERROR_PROC_NOT_FOUND; return; }
        opened = open() != 0;
        if (!opened) bridgeError = ERROR_GEN_FAILURE;
    }
    void Memory(GpuSample& sample) {
        if (!memoryQuery) {
            if (PdhOpenQueryW(nullptr, 0, &memoryQuery) != ERROR_SUCCESS) return;
            if (PdhAddEnglishCounterW(memoryQuery, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &memoryCounter) != ERROR_SUCCESS)
                memoryCounter = nullptr;
        }
        if (!memoryCounter || PdhCollectQueryData(memoryQuery) != ERROR_SUCCESS) return;
        DWORD bytes = 0, items = 0;
        if (PdhGetFormattedCounterArrayW(memoryCounter, PDH_FMT_LARGE, &bytes, &items, nullptr) != PDH_MORE_DATA ||
            !bytes || bytes > 16 * 1024 * 1024) return;
        std::vector<BYTE> buffer(bytes);
        auto values = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(memoryCounter, PDH_FMT_LARGE, &bytes, &items, values) != ERROR_SUCCESS) return;
        const auto key = GpuCounterKey(sample.luid);
        uint64_t total = 0; bool found = false;
        for (DWORD i = 0; i < items; ++i) {
            const auto& value = values[i].FmtValue;
            if (!values[i].szName || (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA) || value.largeValue < 0) continue;
            std::wstring name(values[i].szName);
            std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (name.find(key) == std::wstring::npos) continue;
            total += static_cast<uint64_t>(value.largeValue); found = true;
        }
        if (found) sample.memoryUsedBytes = total;
    }
};
GpuSampler::GpuSampler() : impl_(std::make_unique<Impl>()) {}
GpuSampler::~GpuSampler() = default;
GpuSample GpuSampler::Collect() {
    GpuSample sample;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) { sample.error = ERROR_NOT_FOUND; return sample; }
    bool selected = false;
    for (UINT index = 0; index < 64; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) continue;
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        // Prefer the discrete adapter on hybrid systems; keep all metrics on the same LUID.
        if (selected && desc.DedicatedVideoMemory <= sample.memoryTotalBytes) continue;
        selected = true;
        sample.name = desc.Description; sample.vendorId = desc.VendorId;
        sample.luid = (static_cast<uint64_t>(static_cast<DWORD>(desc.AdapterLuid.HighPart)) << 32) | desc.AdapterLuid.LowPart;
        sample.memoryTotalBytes = desc.DedicatedVideoMemory;
    }
    if (!selected) { sample.error = ERROR_NOT_FOUND; return sample; }
    impl_->Memory(sample);
    impl_->OpenBridge();
    sample.error = impl_->bridgeError;
    if (!impl_->opened || !impl_->refresh()) return sample;
    const int count = impl_->count();
    if (count < 0 || count > 4096) { sample.error = ERROR_INVALID_DATA; return sample; }
    std::array<int, 4> priorities{-1, -1, -1, -1};
    std::array<std::optional<double>*, 4> destinations{&sample.temperatureC, &sample.clockMhz, &sample.powerWatts, &sample.loadPercent};
    std::array<std::wstring*, 4> labels{&sample.temperatureSensor, &sample.clockSensor, &sample.powerSensor, nullptr};
    const std::array<int, 4> types{4, 3, 2, 5};
    for (int i = 0; i < count; ++i) {
        NativeSensor sensor{};
        std::array<char, 1024> key{}, hardware{}, name{};
        if (!impl_->get(i, &sensor, key.data(), static_cast<int>(key.size()), hardware.data(), static_cast<int>(hardware.size()),
            name.data(), static_cast<int>(name.size())) || Wide(hardware.data()) != sample.name) continue;
        const auto label = Wide(name.data());
        for (size_t metric = 0; metric < types.size(); ++metric) {
            const auto priority = GpuSensors::Priority(sensor.hardware, sensor.type, sensor.value, label, types[metric]);
            if (priority > priorities[metric]) {
                *destinations[metric] = sensor.value; priorities[metric] = priority;
                if (labels[metric]) *labels[metric] = label;
            }
        }
    }
    if (sample.temperatureC || sample.clockMhz || sample.powerWatts || sample.loadPercent) sample.sensorSource = "librehardwaremonitor";
    return sample;
}
