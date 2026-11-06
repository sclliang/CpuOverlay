#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>

namespace cpuoverlay {

// Measures application presents using their QPC timestamps, independently for
// each process and swap chain. ETW batch delivery does not define frame time.
class FrameRateEstimator final {
public:
    struct Statistics {
        std::optional<double> fps, onePercentLow;
        std::size_t intervalCount = 0;
    };
    void AddPresent(std::uint32_t processId, std::uint64_t sourceId,
        double timestampSeconds);
    void AddPresent(std::uint32_t processId, std::uint64_t sourceId,
        double timestampSeconds, double receivedSeconds);
    void Update(double nowSeconds);

    // Returns the busiest fresh swap chain of this process only.
    [[nodiscard]] std::optional<double> Estimate(
        std::uint32_t processId, double nowSeconds) const;
    [[nodiscard]] Statistics EstimateStatistics(std::uint32_t processId, double nowSeconds) const;
    void Clear() noexcept;

private:
    struct ChainState {
        std::deque<double> timestamps;
        double lastProgressReceivedSeconds{};
    };
    using SwapChains = std::unordered_map<std::uint64_t, ChainState>;
    std::unordered_map<std::uint32_t, SwapChains> processes_;
};

} // namespace cpuoverlay
