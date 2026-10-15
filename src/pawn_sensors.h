#pragma once
#include <windows.h>
#include <optional>
#include <cstdint>

struct PawnSample {
    std::optional<double> temperatureC;
    std::optional<double> packageWatts;
    const char* status = "unavailable";
    HRESULT error = S_OK;
    const char* moduleSource = "unavailable";
    const char* sensorSource = "unavailable";
    const char* cpuVendor = "unknown";
    const char* temperatureSource = "unavailable";
    const char* powerSource = "unavailable";
    uint32_t cpuFamily = 0, cpuModel = 0;
    uint64_t thermalRaw = 0;
    uint32_t thermalSmnRaw = 0;
    uint32_t thermalLegacyRaw = 0, thermalCoreIndex = 0;
    double amdTctlOffsetC = 0;
    uint32_t energyRaw = 0;
    uint64_t energyUnitRaw = 0;
    double energyUnitJoules = 0;
    double intervalSeconds = 0;
    double tjMaxC = 0;
};

class PawnSensors {
public:
    PawnSensors();
    ~PawnSensors();
    PawnSensors(const PawnSensors&) = delete;
    PawnSensors& operator=(const PawnSensors&) = delete;
    void Prime();
    PawnSample Collect();
private:
    bool Open();
    bool ExecuteRead(const char* function, const ULONG64* input, SIZE_T size, uint64_t& value);
    bool ReadRegister(const char* function, uint32_t index, uint64_t& value);
    bool ReadMsr(uint32_t index, uint64_t& value);
    bool ReadPci(const char* function, const ULONG64* input, SIZE_T size, uint32_t& value);
    void ReadAmdTemperature(PawnSample& sample);
    using OpenFn = HRESULT(WINAPI*)(PHANDLE);
    using LoadFn = HRESULT(WINAPI*)(HANDLE, const UCHAR*, SIZE_T);
    using ExecuteFn = HRESULT(WINAPI*)(HANDLE, PCSTR, const ULONG64*, SIZE_T, PULONG64, SIZE_T, PSIZE_T);
    using CloseFn = HRESULT(WINAPI*)(HANDLE);
    HMODULE library_ = nullptr;
    HANDLE handle_ = nullptr;
    HANDLE pciMutex_ = nullptr;
    ExecuteFn execute_ = nullptr;
    CloseFn close_ = nullptr;
    const char* status_ = "unavailable";
    const char* moduleSource_ = "unavailable";
    HRESULT error_ = S_OK;
    bool intel_ = false, amd_ = false, packageThermal_ = false, haveEnergy_ = false;
    bool k8Thermal_ = false;
    uint32_t k8CoreCount_ = 1;
    uint32_t cpuFamily_ = 0, cpuModel_ = 0;
    double amdTctlOffsetC_ = 0;
    uint32_t energy_ = 0;
    LARGE_INTEGER stamp_{}, clockFrequency_{};
};
