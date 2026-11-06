#include "frame_rate_estimator.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

int gFailures = 0;
void Check(const bool condition, const std::string_view expression, const int line) {
    if (!condition) {
        ++gFailures;
        std::cerr << "FAILED at line " << line << ": " << expression << '\n';
    }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)

void AddFrames(cpuoverlay::FrameRateEstimator& estimator, const std::uint32_t processId,
    const double start, const double rate, const double duration = 0.5,
    const std::uint64_t sourceId = 1U) {
    const int count = static_cast<int>(std::round(rate * duration));
    for (int index = 0; index < count; ++index) {
        estimator.AddPresent(processId, sourceId, start + static_cast<double>(index) / rate);
    }
}

void CheckRate(const cpuoverlay::FrameRateEstimator& estimator,
    const std::uint32_t processId, const double now, const double expected) {
    const auto rate = estimator.Estimate(processId, now);
    CHECK(rate.has_value());
    if (rate.has_value()) CHECK(std::abs(*rate - expected) < 0.01);
}

void TestForegroundProcessIsolation() {
    cpuoverlay::FrameRateEstimator estimator;
    AddFrames(estimator, 10U, 100.0, 60.0);
    AddFrames(estimator, 11U, 100.0, 161.0, 1.0); // busy background app / compositor
    estimator.Update(101.0);
    CheckRate(estimator, 10U, 101.0, 60.0);
    CheckRate(estimator, 11U, 101.0, 161.0);
    CHECK(!estimator.Estimate(12U, 101.0).has_value()); // static foreground
    CHECK(!estimator.Estimate(0U, 101.0).has_value());
}

void TestSwapChainsAreNotAddedTogether() {
    cpuoverlay::FrameRateEstimator estimator;
    AddFrames(estimator, 20U, 200.0, 60.0, 1.0, 1U);
    AddFrames(estimator, 20U, 200.0, 60.0, 1.0, 2U);
    AddFrames(estimator, 20U, 200.0, 30.0, 1.0, 3U);
    estimator.Update(201.0);
    CheckRate(estimator, 20U, 201.0, 60.0); // not 150 FPS
    AddFrames(estimator, 20U, 203.0, 30.0, 1.0, 3U);
    estimator.Update(204.0);
    CheckRate(estimator, 20U, 204.0, 30.0); // retired chains no longer win
}

void TestBatchDeliveryAndSlowRefresh() {
    cpuoverlay::FrameRateEstimator estimator;
    AddFrames(estimator, 30U, 300.0, 60.0, 2.0);
    estimator.Update(302.0);
    CheckRate(estimator, 30U, 302.0, 60.0); // first drain is not divided by 500ms
    AddFrames(estimator, 30U, 302.0, 60.0, 4.0);
    estimator.Update(306.0);
    CheckRate(estimator, 30U, 306.0, 60.0); // no clamped wall-clock denominator
    estimator.Update(306.5);
    CheckRate(estimator, 30U, 306.5, 60.0); // ETW delivery latency does not change rate
}

void TestOutOfOrderAndDuplicateEvents() {
    cpuoverlay::FrameRateEstimator estimator;
    for (int index = 59; index >= 0; --index) {
        const double timestamp = 400.0 + static_cast<double>(index) / 60.0;
        estimator.AddPresent(40U, 1U, timestamp);
        estimator.AddPresent(40U, 1U, timestamp);
    }
    estimator.Update(401.0);
    CheckRate(estimator, 40U, 401.0, 60.0);
}

void TestExpiryAndRestart() {
    cpuoverlay::FrameRateEstimator estimator;
    AddFrames(estimator, 50U, 500.0, 156.0, 1.0);
    CheckRate(estimator, 50U, 501.0, 156.0);
    CHECK(!estimator.Estimate(50U, 504.0).has_value()); // even without Update
    estimator.Update(504.0);
    CHECK(!estimator.Estimate(50U, 504.0).has_value());
    AddFrames(estimator, 50U, 504.0, 60.0, 1.0);
    estimator.Update(505.0);
    CheckRate(estimator, 50U, 505.0, 60.0);
    estimator.Clear();
    CHECK(!estimator.Estimate(50U, 504.0).has_value());
}

void TestDelayedContinuousDelivery() {
    cpuoverlay::FrameRateEstimator estimator;
    // Reproduce observed ETW batches with a newest-frame age above two seconds.
    // The old wall-clock window erased these samples on every Update.
    for (int batch = 0; batch < 30; ++batch) {
        const double start = 600.0 + batch;
        const double received = start + 3.4;
        for (int frame = 0; frame < 60; ++frame) {
            estimator.AddPresent(70U, 1U, start + frame / 60.0, received);
        }
        estimator.Update(received);
        CheckRate(estimator, 70U, received, 60.0);
        estimator.Update(received + 0.5); // UI poll before the next delivery
        CheckRate(estimator, 70U, received + 0.5, 60.0);
        CHECK(!estimator.Estimate(71U, received).has_value());
    }
    // A short gap retains the same process' measured result, then expires.
    CheckRate(estimator, 70U, 634.0, 60.0);
    CHECK(!estimator.Estimate(70U, 635.5).has_value());
}

void TestReplayCannotRefreshLiveness() {
    cpuoverlay::FrameRateEstimator estimator;
    for (int frame = 0; frame < 60; ++frame) {
        estimator.AddPresent(80U, 1U, 700.0 + frame / 60.0, 703.0);
    }
    CheckRate(estimator, 80U, 703.0, 60.0);
    for (int frame = 0; frame < 60; ++frame) {
        estimator.AddPresent(80U, 1U, 700.0 + frame / 60.0, 705.0);
    }
    CHECK(!estimator.Estimate(80U, 706.1).has_value());
    estimator.Update(706.1);
    estimator.AddPresent(80U, 1U, 700.9, 706.2); // late out-of-order duplicate
    CHECK(!estimator.Estimate(80U, 706.2).has_value());
    estimator.Update(707.0);
    for (int frame = 0; frame < 60; ++frame) {
        estimator.AddPresent(80U, 1U, 700.0 + frame / 60.0, 710.0);
    }
    CHECK(!estimator.Estimate(80U, 710.0).has_value()); // ancient backlog rejected
}

void TestInvalidAndInsufficientSamples() {
    cpuoverlay::FrameRateEstimator estimator;
    estimator.AddPresent(0U, 1U, 1.0);
    estimator.AddPresent(60U, 1U, -1.0);
    estimator.AddPresent(60U, 1U, std::numeric_limits<double>::quiet_NaN());
    estimator.AddPresent(60U, 1U, std::numeric_limits<double>::infinity());
    estimator.Update(1.5);
    CHECK(!estimator.Estimate(60U, 1.5).has_value());
    estimator.AddPresent(60U, 1U, 2.0);
    estimator.AddPresent(60U, 1U, 2.001);
    estimator.AddPresent(60U, 1U, 2.002);
    CHECK(!estimator.Estimate(60U, 2.01).has_value());
    CHECK(!estimator.Estimate(60U, -1.0).has_value());
    CHECK(!estimator.Estimate(60U, std::numeric_limits<double>::quiet_NaN()).has_value());
    AddFrames(estimator, 61U, 10.0, 60.0);
    CHECK(!estimator.Estimate(61U, 5.0).has_value()); // future events
}

} // namespace

int main() {
    {
        cpuoverlay::FrameRateEstimator estimator;
        double time = 1000;
        estimator.AddPresent(55, 1, time);
        for (int i = 1; i <= 99; ++i) { time += 1.0 / 120; estimator.AddPresent(55, 1, time); }
        CHECK(!estimator.EstimateStatistics(55, time).onePercentLow);
        time += 1.0 / 120; estimator.AddPresent(55, 1, time);
        auto statistics = estimator.EstimateStatistics(55, time);
        CHECK(statistics.onePercentLow && std::abs(*statistics.onePercentLow - 120) < .001);
        CHECK(statistics.intervalCount == 100);
        estimator.Clear(); time = 2000; estimator.AddPresent(55, 1, time);
        for (int i = 1; i <= 200; ++i) { time += i % 100 == 0 ? .1 : 1.0 / 60; estimator.AddPresent(55, 1, time); }
        statistics = estimator.EstimateStatistics(55, time);
        CHECK(statistics.onePercentLow && std::abs(*statistics.onePercentLow - 10) < .001);
        CHECK(statistics.intervalCount == 200);
        CHECK(!estimator.EstimateStatistics(55, time + 3.001).onePercentLow);
        for (int i = 0; i <= 240; ++i) estimator.AddPresent(55, 2, time - 1 + i / 240.0, time);
        statistics = estimator.EstimateStatistics(55, time);
        CHECK(statistics.onePercentLow && std::abs(*statistics.onePercentLow - 240) < .001);
        for (int i = 0; i < 1200; ++i) { time += 1.0 / 60; estimator.AddPresent(55, 1, time); }
        statistics = estimator.EstimateStatistics(55, time);
        CHECK(statistics.onePercentLow && std::abs(*statistics.onePercentLow - 60) < .001);
    }
    TestForegroundProcessIsolation();
    TestSwapChainsAreNotAddedTogether();
    TestBatchDeliveryAndSlowRefresh();
    TestOutOfOrderAndDuplicateEvents();
    TestExpiryAndRestart();
    TestDelayedContinuousDelivery();
    TestReplayCannotRefreshLiveness();
    TestInvalidAndInsufficientSamples();
    if (gFailures != 0) {
        std::cerr << gFailures << " frame-rate estimator assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All frame-rate estimator tests passed.\n";
    return 0;
}
