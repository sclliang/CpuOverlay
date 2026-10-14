#pragma once
#include <windows.h>
#include <pdh.h>
#include <optional>

struct CpuSample {
    std::optional<double> usagePercent;
    std::optional<double> frequencyGhz;
    const char* frequencySource = "unavailable";
    const char* usageSource = "unavailable";
};

class CpuSampler {
public:
    CpuSampler();
    ~CpuSampler();
    CpuSampler(const CpuSampler&) = delete;
    CpuSampler& operator=(const CpuSampler&) = delete;
    void Prime();
    CpuSample Collect();
private:
    void OpenCounters();
    std::optional<double> Read(PDH_HCOUNTER counter) const;
    std::optional<double> ReadSystemUsage();
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER usage_ = nullptr;
    PDH_HCOUNTER performance_ = nullptr;
    PDH_HCOUNTER frequency_ = nullptr;
    ULONGLONG idle_ = 0, total_ = 0;
    bool haveTimes_ = false;
    unsigned failures_ = 0;
};
