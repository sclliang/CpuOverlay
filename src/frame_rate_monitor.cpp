#include "frame_rate_monitor.h"
#include "frame_rate_estimator.h"
#include "PresentMonRealtime.h"
#include <objbase.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
DWORD ForegroundProcess() {
    const auto window = GetForegroundWindow();
    if (!window || window == GetShellWindow() || window == GetDesktopWindow()) return 0;
    DWORD process = 0; GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId() ? 0 : process;
}
std::string SessionName() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    wchar_t text[40]{};
    if (!StringFromGUID2(guid, text, ARRAYSIZE(text))) return {};
    std::string name = "CpuOverlayFps_" + std::to_string(GetCurrentProcessId()) + "_";
    for (int i = 1; i <= 36; ++i) name += static_cast<char>(text[i]);
    return name;
}
}
struct FrameRateMonitor::Impl {
    presentmon::RealtimeSession session;
    cpuoverlay::FrameRateEstimator estimator;
    ULONGLONG retryAt = 0;
    DWORD error = 0, trackedProcess = 0;
    uint64_t minimumQpc = 0;
    std::mutex mutex;
    std::condition_variable changed;
    std::thread poller;
    bool stop = false;
    DWORD processOverride = 0;
    FrameRateSample snapshot;

    void StartSession() {
        session.Stop(); estimator.Clear(); trackedProcess = 0;
        (void)presentmon::RealtimeSession::CleanupStaleSessions();
        std::uint32_t code = 0;
        if (!session.Start(SessionName(), 0, &code)) { error = code; retryAt = GetTickCount64() + 30000; }
        else { error = 0; retryAt = 0; }
    }
    FrameRateSample Poll(DWORD processId) {
        FrameRateSample result;
        result.processId = processId ? processId : ForegroundProcess();
        try {
            if (!session.IsRunning() && GetTickCount64() >= retryAt) StartSession();
            result.error = error; result.running = session.IsRunning();
            if (!result.running) return result;
            const auto frequency = session.QpcFrequency();
            LARGE_INTEGER now{};
            if (!frequency || !QueryPerformanceCounter(&now)) { result.error = ERROR_INVALID_DATA; return result; }
            const double seconds = static_cast<double>(now.QuadPart) / static_cast<double>(frequency);
            if (trackedProcess != result.processId) {
                estimator.Clear(); trackedProcess = result.processId;
                minimumQpc = static_cast<uint64_t>(now.QuadPart);
            }
            std::vector<presentmon::PresentSample> samples;
            session.DrainPresentSamples(samples);
            for (const auto& sample : samples) {
                if (sample.processId != result.processId || sample.qpc < minimumQpc) continue;
                estimator.AddPresent(sample.processId, sample.swapChainAddress,
                    static_cast<double>(sample.qpc) / static_cast<double>(frequency), seconds);
            }
            estimator.Update(seconds);
            if (result.processId) {
                const auto statistics = estimator.EstimateStatistics(result.processId, seconds);
                result.fps = statistics.fps; result.onePercentLow = statistics.onePercentLow;
                result.intervalCount = statistics.intervalCount;
            }
        } catch (...) {
            result = {}; result.error = ERROR_GEN_FAILURE;
            session.Stop(); estimator.Clear(); error = ERROR_GEN_FAILURE; retryAt = GetTickCount64() + 30000;
        }
        return result;
    }
    void Run() {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stop) {
            const auto target = processOverride;
            lock.unlock();
            const auto value = Poll(target);
            lock.lock();
            if (stop) break;
            snapshot = value;
            // A slow display refresh must not discard ETW batches or alter 1% Low.
            changed.wait_for(lock, std::chrono::milliseconds(100), [this, target] { return stop || processOverride != target; });
        }
    }
};
FrameRateMonitor::FrameRateMonitor() : impl_(std::make_unique<Impl>()) {}
FrameRateMonitor::~FrameRateMonitor() { Pause(); }
void FrameRateMonitor::Pause() {
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->stop = true; }
    impl_->changed.notify_all();
    if (impl_->poller.joinable()) impl_->poller.join();
    impl_->session.Stop(); impl_->estimator.Clear();
    impl_->retryAt = 0; impl_->error = 0; impl_->trackedProcess = 0; impl_->minimumQpc = 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot = {}; impl_->processOverride = 0;
}
FrameRateSample FrameRateMonitor::Collect(DWORD processId) {
    if (!impl_->poller.joinable()) {
        impl_->stop = false; impl_->processOverride = processId;
        try {
            impl_->StartSession();
            impl_->snapshot = {}; impl_->snapshot.running = impl_->session.IsRunning(); impl_->snapshot.error = impl_->error;
            impl_->poller = std::thread(&Impl::Run, impl_.get());
        } catch (...) {
            impl_->session.Stop(); impl_->snapshot = {}; impl_->snapshot.error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    FrameRateSample result;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->processOverride = processId; result = impl_->snapshot;
    }
    impl_->changed.notify_all();
    const auto current = processId ? processId : ForegroundProcess();
    if (result.processId != current) {
        result.fps.reset(); result.onePercentLow.reset(); result.intervalCount = 0; result.processId = current;
    }
    return result;
}
