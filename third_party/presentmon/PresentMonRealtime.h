#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace presentmon {

struct PresentSample {
    std::uint32_t processId{0};
    std::uint64_t swapChainAddress{0};
    std::uint64_t qpc{0};
};

class RealtimeSession final {
public:
    RealtimeSession() noexcept;
    ~RealtimeSession();

    RealtimeSession(const RealtimeSession&) = delete;
    RealtimeSession& operator=(const RealtimeSession&) = delete;
    RealtimeSession(RealtimeSession&&) = delete;
    RealtimeSession& operator=(RealtimeSession&&) = delete;

    [[nodiscard]] bool Start(
        std::string_view sessionName,
        std::uint32_t initialProcessId,
        std::uint32_t* errorCode = nullptr) noexcept;
    void Stop() noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;
    void SetTrackedProcess(std::uint32_t processId) noexcept;
    void DrainPresentSamples(std::vector<PresentSample>& samples);
    [[nodiscard]] std::uint64_t QpcFrequency() const noexcept;

    // Stops only inactive sessions whose names match CpuOverlay's PID/GUID format.
    [[nodiscard]] static std::size_t CleanupStaleSessions() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace presentmon
