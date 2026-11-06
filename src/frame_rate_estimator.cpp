#include "frame_rate_estimator.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <functional>
#include <numeric>
#include <vector>

namespace cpuoverlay {
namespace {

constexpr double kWindowSeconds = 2.0;
constexpr double kLowWindowSeconds = 10.0;
// ETW can deliver a new batch whose newest frame is already 2-3 seconds old.
// Freshness is measured from advancing delivery, independently of frame time.
constexpr double kDeliveryTimeoutSeconds = 3.0;
constexpr double kMaximumEventAgeSeconds = 6.0;
constexpr double kMinimumSpanSeconds = 0.1;
constexpr std::size_t kMaximumSamplesPerChain = 32768U;

bool ValidTime(const double seconds) noexcept {
    return std::isfinite(seconds) && seconds >= 0.0;
}

} // namespace

void FrameRateEstimator::AddPresent(const std::uint32_t processId,
    const std::uint64_t sourceId, const double timestampSeconds) {
    AddPresent(processId, sourceId, timestampSeconds, timestampSeconds);
}

void FrameRateEstimator::AddPresent(const std::uint32_t processId,
    const std::uint64_t sourceId, const double timestampSeconds,
    const double receivedSeconds) {
    if (processId == 0U || !ValidTime(timestampSeconds) || !ValidTime(receivedSeconds) ||
        timestampSeconds > receivedSeconds ||
        receivedSeconds - timestampSeconds > kMaximumEventAgeSeconds) return;

    auto& chain = processes_[processId][sourceId];
    auto& timestamps = chain.timestamps;
    const bool advances = timestamps.empty() || timestampSeconds > timestamps.back();
    if (!timestamps.empty() && timestampSeconds < timestamps.back() - kWindowSeconds) return;
    // Completion order can differ from submission order. Keep one sample per
    // timestamp within a chain, including when a batch is delivered twice.
    const auto position = std::lower_bound(
        timestamps.begin(), timestamps.end(), timestampSeconds);
    if (position != timestamps.end() && *position == timestampSeconds) return;
    timestamps.insert(position, timestampSeconds);
    if (advances) chain.lastProgressReceivedSeconds = receivedSeconds;
    while (timestamps.size() > kMaximumSamplesPerChain ||
        timestamps.back() - timestamps.front() > kLowWindowSeconds) {
        timestamps.pop_front();
    }
}

void FrameRateEstimator::Update(const double nowSeconds) {
    if (!ValidTime(nowSeconds)) return;
    for (auto process = processes_.begin(); process != processes_.end();) {
        auto& chains = process->second;
        for (auto chain = chains.begin(); chain != chains.end();) {
            const auto& state = chain->second;
            // Retain the timestamp watermark after the delivery timeout, until
            // the absolute age limit. Replayed duplicates must not recreate an
            // expired chain and restart its delivery timeout.
            if (state.timestamps.empty() ||
                nowSeconds - state.timestamps.back() > kMaximumEventAgeSeconds) {
                chain = chains.erase(chain);
            }
            else ++chain;
        }
        if (chains.empty()) process = processes_.erase(process);
        else ++process;
    }
}

std::optional<double> FrameRateEstimator::Estimate(
    const std::uint32_t processId, const double nowSeconds) const {
    return EstimateStatistics(processId, nowSeconds).fps;
}
FrameRateEstimator::Statistics FrameRateEstimator::EstimateStatistics(
    const std::uint32_t processId, const double nowSeconds) const {
    Statistics result;
    if (processId == 0U || !ValidTime(nowSeconds)) return result;
    const auto process = processes_.find(processId);
    if (process == processes_.end()) return result;

    const ChainState* selected = nullptr;
    for (const auto& [sourceId, chain] : process->second) {
        const auto& timestamps = chain.timestamps;
        if (timestamps.empty() || nowSeconds < chain.lastProgressReceivedSeconds ||
            nowSeconds - chain.lastProgressReceivedSeconds > kDeliveryTimeoutSeconds ||
            nowSeconds - timestamps.back() > kMaximumEventAgeSeconds) continue;
        // Anchor the rate window to the latest delivered frame, not wall time.
        // A delivery delay must not turn a valid two-second window into N/A.
        const auto first = std::lower_bound(
            timestamps.begin(), timestamps.end(), timestamps.back() - kWindowSeconds);
        const auto end = std::upper_bound(first, timestamps.end(), nowSeconds);
        const auto count = std::distance(first, end);
        if (count < 3) continue;
        const double span = *std::prev(end) - *first;
        if (span < kMinimumSpanSeconds) continue;
        const double rate = static_cast<double>(count - 1) / span;
        if (std::isfinite(rate) && rate >= 1.0 && (!result.fps || rate > *result.fps)) {
            result.fps = rate; selected = &chain;
        }
    }
    if (!selected) return result;
    const auto& timestamps = selected->timestamps;
    const auto first = std::lower_bound(timestamps.begin(), timestamps.end(), timestamps.back() - kLowWindowSeconds);
    std::vector<double> intervals;
    for (auto current = first; current != timestamps.end() && std::next(current) != timestamps.end(); ++current) {
        const auto interval = *std::next(current) - *current;
        if (std::isfinite(interval) && interval > 0) intervals.push_back(interval);
    }
    result.intervalCount = intervals.size();
    if (intervals.size() < 100) return result;
    const auto slowestCount = (intervals.size() + 99) / 100; // ceil(1% of intervals)
    std::sort(intervals.begin(), intervals.end(), std::greater<double>());
    const auto duration = std::accumulate(intervals.begin(), intervals.begin() + slowestCount, 0.0);
    if (duration > 0) result.onePercentLow = static_cast<double>(slowestCount) / duration;
    return result;
}

void FrameRateEstimator::Clear() noexcept {
    processes_.clear();
}

} // namespace cpuoverlay
