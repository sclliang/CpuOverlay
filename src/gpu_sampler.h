#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

struct GpuSample {
    std::wstring name;
    std::wstring temperatureSensor, clockSensor, powerSensor;
    uint64_t luid = 0, memoryTotalBytes = 0;
    DWORD vendorId = 0, error = 0;
    std::optional<uint64_t> memoryUsedBytes;
    std::optional<double> temperatureC, clockMhz, powerWatts, loadPercent;
    const char* sensorSource = "unavailable";
};
std::wstring GpuCounterKey(uint64_t luid);
class GpuSampler {
public:
    GpuSampler();
    ~GpuSampler();
    GpuSample Collect();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
