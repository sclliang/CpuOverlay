#pragma once
#include <windows.h>
#include <memory>
#include <optional>

struct FrameRateSample {
    std::optional<double> fps;
    std::optional<double> onePercentLow;
    size_t intervalCount = 0;
    DWORD processId = 0, error = 0;
    bool running = false;
};
class FrameRateMonitor {
public:
    FrameRateMonitor();
    ~FrameRateMonitor();
    // A nonzero PID is for the isolated graphics integration fixture only.
    FrameRateSample Collect(DWORD processId = 0);
    void Pause();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
