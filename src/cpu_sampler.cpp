#include "cpu_sampler.h"
#include "sensor_math.h"
#include <pdhmsg.h>
#include <powrprof.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <map>
#include <string>

namespace {
// The Windows SDK omits PROCESSOR_POWER_INFORMATION; layout is documented by Microsoft.
struct ProcessorPowerInfo {
    ULONG number, maxMhz, currentMhz, mhzLimit, maxIdleState, currentIdleState;
};
ULONGLONG FileTimeValue(const FILETIME& time) {
    return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
std::optional<double> ReportedFrequencyMhz() {
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (!count || count > 65536) return {};
    std::vector<ProcessorPowerInfo> info(count);
    if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, info.data(),
        static_cast<ULONG>(info.size() * sizeof(ProcessorPowerInfo))) != 0) return {};
    double sum = 0;
    unsigned valid = 0;
    for (const auto& cpu : info) {
        if (cpu.currentMhz > 0 && cpu.currentMhz < 20000) {
            sum += cpu.currentMhz;
            ++valid;
        }
    }
    return valid ? std::optional<double>(sum / valid) : std::nullopt;
}
}

CpuSampler::CpuSampler() { OpenCounters(); }
CpuSampler::~CpuSampler() { if (query_) PdhCloseQuery(query_); }

void CpuSampler::OpenCounters() {
    if (query_) PdhCloseQuery(query_);
    query_ = nullptr;
    usage_ = performance_ = frequency_ = gpu_ = packagePower_ = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) return;
    auto add = [this](const wchar_t* path, PDH_HCOUNTER& counter) {
        if (PdhAddEnglishCounterW(query_, path, 0, &counter) != ERROR_SUCCESS) counter = nullptr;
    };
    add(L"\\Processor Information(_Total)\\% Processor Time", usage_);
    if (!usage_) add(L"\\Processor(_Total)\\% Processor Time", usage_);
    add(L"\\Processor Information(_Total)\\% Processor Performance", performance_);
    add(L"\\Processor Information(_Total)\\Processor Frequency", frequency_);
    add(L"\\GPU Engine(*)\\Utilization Percentage", gpu_);
    // Windows EMI exports average power in milliwatts (umpoext counter help).
    // Match package 0 exactly: no GPU/DRAM/PSys channels or multi-socket sum.
    add(L"\\Energy Meter(rapl_package0_pkg)\\Power", packagePower_);
}

std::optional<double> CpuSampler::ReadGpuUsage(unsigned& engineCount) const {
    engineCount = 0;
    if (!gpu_) return {};
    DWORD bytes = 0, count = 0;
    const DWORD format = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
    if (PdhGetFormattedCounterArrayW(gpu_, format, &bytes, &count, nullptr) != PDH_MORE_DATA || !bytes || bytes > 64 * 1024 * 1024)
        return {};
    std::vector<BYTE> buffer(bytes);
    auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(gpu_, format, &bytes, &count, items) != ERROR_SUCCESS) return {};
    std::map<std::wstring, double> engines;
    for (DWORD i = 0; i < count; ++i) {
        const auto& value = items[i].FmtValue;
        if (!items[i].szName || (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA) ||
            !std::isfinite(value.doubleValue) || value.doubleValue < 0) continue;
        const std::wstring name(items[i].szName);
        const auto luid = name.find(L"luid_");
        const auto type = name.find(L"_engtype_");
        if (luid == std::wstring::npos || type == std::wstring::npos || type <= luid) continue;
        // Sum processes on the SAME physical engine; never add different engines or GPUs together.
        engines[name.substr(luid, type - luid)] += value.doubleValue;
    }
    if (engines.empty()) return {};
    double busiest = 0;
    for (const auto& entry : engines) busiest = std::max(busiest, entry.second);
    engineCount = static_cast<unsigned>(engines.size());
    return std::clamp(busiest, 0.0, 100.0);
}

std::optional<double> CpuSampler::Read(PDH_HCOUNTER counter) const {
    if (!counter) return {};
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &value) != ERROR_SUCCESS)
        return {};
    if ((value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA) || !std::isfinite(value.doubleValue))
        return {};
    return value.doubleValue;
}

std::optional<double> CpuSampler::ReadSystemUsage() {
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) { haveTimes_ = false; return {}; }
    const auto nowIdle = FileTimeValue(idle);
    const auto nowTotal = FileTimeValue(kernel) + FileTimeValue(user);
    std::optional<double> result;
    if (haveTimes_ && nowTotal > total_ && nowIdle >= idle_) {
        const auto elapsed = nowTotal - total_;
        const auto idleDelta = nowIdle - idle_;
        if (idleDelta <= elapsed) result = 100.0 * static_cast<double>(elapsed - idleDelta) / elapsed;
    }
    idle_ = nowIdle;
    total_ = nowTotal;
    haveTimes_ = true;
    // GetSystemTimes only covers the calling processor group on >64 processor machines.
    if (GetActiveProcessorGroupCount() > 1) return {};
    return result;
}

void CpuSampler::Prime() {
    if (!query_) OpenCounters();
    if (query_) PdhCollectQueryData(query_);
    haveTimes_ = false;
    ReadSystemUsage();
    pawn_.Prime();
}

CpuSample CpuSampler::Collect() {
    CpuSample sample;
    sample.hardware = pawn_.Collect();
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys && memory.ullAvailPhys <= memory.ullTotalPhys) {
        sample.memoryTotalBytes = memory.ullTotalPhys;
        sample.memoryUsedBytes = memory.ullTotalPhys - memory.ullAvailPhys;
        sample.memoryPercent = 100.0 * static_cast<double>(sample.memoryUsedBytes) / static_cast<double>(sample.memoryTotalBytes);
    }
    auto fallbackUsage = ReadSystemUsage();
    const bool collected = query_ && PdhCollectQueryData(query_) == ERROR_SUCCESS;
    if (collected) {
        sample.energyMeterMilliwatts = Read(packagePower_);
        if (!sample.hardware.packageWatts && sample.energyMeterMilliwatts) {
            sample.hardware.packageWatts = SensorMath::EnergyMeterWatts(*sample.energyMeterMilliwatts);
            if (sample.hardware.packageWatts) sample.hardware.powerSource = "windows_energy_meter_package0";
            // Keep pawnio_status / HRESULT intact so diagnostics still explain
            // why the original backend needed a fallback.
        }
        sample.gpuPercent = ReadGpuUsage(sample.gpuEngineCount);
        sample.usagePercent = Read(usage_);
        if (sample.usagePercent && *sample.usagePercent >= 0) {
            sample.usagePercent = std::clamp(*sample.usagePercent, 0.0, 100.0);
            sample.usageSource = "processor_time";
        } else sample.usagePercent.reset();

        const auto referenceMhz = Read(frequency_);
        const auto performance = Read(performance_);
        // Processor Frequency is the reference/P-state frequency on many modern CPUs.
        // The performance ratio accounts for turbo. This remains an OS estimate,
        // not a measurement of every core's instantaneous effective clock.
        if (referenceMhz && performance && *referenceMhz > 0 && *performance > 0) {
            const double ghz = *referenceMhz * *performance / 100000.0;
            if (ghz > 0 && ghz < 20) {
                sample.frequencyGhz = ghz;
                sample.frequencySource = "performance_estimate";
            }
        }
    }
    if (!sample.usagePercent && fallbackUsage) {
        sample.usagePercent = fallbackUsage;
        sample.usageSource = "system_times";
    }
    if (!sample.frequencyGhz) {
        if (const auto mhz = ReportedFrequencyMhz()) {
            sample.frequencyGhz = *mhz / 1000.0;
            sample.frequencySource = "power_reported";
        }
    }
    if (!collected || !Read(usage_) || !Read(performance_)) {
        // Recover if performance counters become available later, without a tight retry loop.
        if (++failures_ >= 30) { failures_ = 0; OpenCounters(); Prime(); }
    } else failures_ = 0;
    return sample;
}
