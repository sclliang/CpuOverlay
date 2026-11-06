#include "PresentMonRealtime.h"

#include "PresentData/PresentMonTraceConsumer.hpp"
#include "PresentData/TraceSession.hpp"

#include <windows.h>
#include <evntrace.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>

namespace presentmon {
namespace {

constexpr wchar_t kSessionPrefix[] = L"CpuOverlayFps_";
constexpr std::size_t kGuidLength = 36U;
constexpr std::size_t kMaximumTraceSessions = 128U;

struct TraceProperties : EVENT_TRACE_PROPERTIES {
    wchar_t loggerName[1024]{};
};

void SetError(std::uint32_t* destination, const std::uint32_t value) noexcept {
    if (destination != nullptr) {
        *destination = value;
    }
}

bool IsHexDigit(const wchar_t value) noexcept {
    return (value >= L'0' && value <= L'9') ||
        (value >= L'a' && value <= L'f') ||
        (value >= L'A' && value <= L'F');
}

bool IsGuidText(const std::wstring_view value) noexcept {
    if (value.size() != kGuidLength) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const bool separator = index == 8U || index == 13U || index == 18U || index == 23U;
        if ((separator && value[index] != L'-') || (!separator && !IsHexDigit(value[index]))) {
            return false;
        }
    }
    return true;
}

bool TryParseSessionOwner(
    const std::wstring_view sessionName,
    std::uint32_t& processId) noexcept {
    constexpr std::wstring_view prefix(kSessionPrefix);
    processId = 0;
    if (!sessionName.starts_with(prefix)) {
        return false;
    }

    const std::wstring_view suffix = sessionName.substr(prefix.size());
    const std::size_t separator = suffix.find(L'_');
    if (separator == std::wstring_view::npos || separator == 0U ||
        !IsGuidText(suffix.substr(separator + 1U))) {
        return false;
    }

    const std::wstring_view pidText = suffix.substr(0U, separator);
    std::uint64_t parsed = 0;
    for (const wchar_t value : pidText) {
        if (value < L'0' || value > L'9') {
            return false;
        }
        parsed = parsed * 10U + static_cast<unsigned>(value - L'0');
        if (parsed > (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }
    }
    if (parsed == 0U) {
        return false;
    }
    processId = static_cast<std::uint32_t>(parsed);
    return true;
}

bool IsValidSessionName(const std::string_view sessionName) noexcept {
    constexpr std::string_view prefix("CpuOverlayFps_");
    if (!sessionName.starts_with(prefix)) {
        return false;
    }
    const std::string_view suffix = sessionName.substr(prefix.size());
    const std::size_t separator = suffix.find('_');
    if (separator == std::string_view::npos || separator == 0U ||
        suffix.size() - separator - 1U != kGuidLength) {
        return false;
    }
    for (const char value : suffix.substr(0U, separator)) {
        if (value < '0' || value > '9') {
            return false;
        }
    }
    for (std::size_t index = 0; index < kGuidLength; ++index) {
        const char value = suffix[separator + 1U + index];
        const bool expectedSeparator =
            index == 8U || index == 13U || index == 18U || index == 23U;
        const bool hex = (value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
        if ((expectedSeparator && value != '-') || (!expectedSeparator && !hex)) {
            return false;
        }
    }
    return true;
}

bool ProcessIsActive(const std::uint32_t processId) noexcept {
    if (processId == GetCurrentProcessId()) {
        return false;
    }
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (process == nullptr) {
        const DWORD error = GetLastError();
        return error != ERROR_INVALID_PARAMETER;
    }
    const bool active = WaitForSingleObject(process, 0U) == WAIT_TIMEOUT;
    CloseHandle(process);
    return active;
}

ULONG StopNamedSession(const std::wstring_view sessionName) noexcept {
    TraceProperties properties{};
    properties.Wnode.BufferSize = sizeof(properties);
    properties.LoggerNameOffset = offsetof(TraceProperties, loggerName);
    const std::wstring ownedName(sessionName);
    return ControlTraceW(0, ownedName.c_str(), &properties, EVENT_TRACE_CONTROL_STOP);
}

} // namespace

struct RealtimeSession::Impl {
    ~Impl() {
        Stop();
    }

    bool Start(const std::string_view requestedSessionName,
        const std::uint32_t initialProcessId, std::uint32_t* errorCode) noexcept {
        std::scoped_lock lock(lifecycleMutex);
        StopLocked();
        SetError(errorCode, ERROR_SUCCESS);

        if (!IsValidSessionName(requestedSessionName)) {
            SetError(errorCode, ERROR_INVALID_NAME);
            return false;
        }

        try {
            consumer = std::make_unique<PMTraceConsumer>();
            // Collect all application presents, then select the foreground PID in
            // FrameRateMonitor. LiteMonitor's working PresentMon helper uses the
            // same broad collection model; filtering at ETW ingestion can discard
            // the correlated DXGI/DXGK events needed to complete a present.
            consumer->mFilteredProcessIds = false;
            // LiteMonitor counts every non-occluded PresentMon output row. Do
            // not require the optional display/DWM correlation events here:
            // those events can be absent for a realtime session and otherwise
            // leave valid DXGI presents incomplete indefinitely.
            consumer->mTrackDisplay = false;
            consumer->mTrackGPU = false;
            consumer->mTrackGPUVideo = false;
            consumer->mTrackInput = false;
            trackedProcessId = initialProcessId;
            sessionName.assign(requestedSessionName);

            const ULONG status = trace.Start(consumer.get(), nullptr, sessionName.c_str());
            if (status != ERROR_SUCCESS) {
                trace = TraceSession{};
                consumer.reset();
                sessionName.clear();
                trackedProcessId = 0U;
                SetError(errorCode, status);
                return false;
            }

            stopRequested.store(false, std::memory_order_release);
            running.store(true, std::memory_order_release);
            TRACEHANDLE traceHandle = trace.mTraceHandle;
            try {
                consumerThread = std::thread([this, traceHandle]() mutable {
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                    try {
                        (void)ProcessTrace(&traceHandle, 1U, nullptr, nullptr);
                    } catch (...) {
                        // A consumer allocation failure must not strand the ETW session.
                    }
                    running.store(false, std::memory_order_release);
                    if (!stopRequested.load(std::memory_order_acquire)) {
                        (void)TraceSession::StopNamedSession(sessionName.c_str());
                    }
                });
            } catch (...) {
                stopRequested.store(true, std::memory_order_release);
                running.store(false, std::memory_order_release);
                trace.Stop();
                trace = TraceSession{};
                consumer.reset();
                sessionName.clear();
                trackedProcessId = 0U;
                SetError(errorCode, ERROR_NOT_ENOUGH_MEMORY);
                return false;
            }
            return true;
        } catch (...) {
            StopLocked();
            SetError(errorCode, ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
    }

    void Stop() noexcept {
        std::scoped_lock lock(lifecycleMutex);
        StopLocked();
    }

    void StopLocked() noexcept {
        stopRequested.store(true, std::memory_order_release);
        if (trace.mTraceHandle != INVALID_PROCESSTRACE_HANDLE || trace.mSessionHandle != 0U) {
            trace.Stop();
        }
        if (consumerThread.joinable()) {
            consumerThread.join();
        }
        running.store(false, std::memory_order_release);
        trace = TraceSession{};
        consumer.reset();
        sessionName.clear();
        trackedProcessId = 0U;
    }

    void SetTrackedProcess(const std::uint32_t processId) noexcept {
        try {
            std::scoped_lock lock(lifecycleMutex);
            if (consumer == nullptr || processId == trackedProcessId) {
                return;
            }
            if (processId != 0U) {
                consumer->AddTrackedProcessForFiltering(processId);
            }
            if (trackedProcessId != 0U) {
                consumer->RemoveTrackedProcessForFiltering(trackedProcessId);
            }
            trackedProcessId = processId;
        } catch (...) {
        }
    }

    void DrainPresentSamples(std::vector<PresentSample>& samples) {
        std::scoped_lock lock(lifecycleMutex);
        samples.clear();
        if (consumer == nullptr) {
            return;
        }

        std::vector<std::shared_ptr<PresentEvent>> presents;
        consumer->DequeuePresentEvents(presents);
        samples.reserve(presents.size());
        for (const auto& present : presents) {
            if (present != nullptr && present->FinalState == PresentResult::Presented &&
                present->ProcessId != 0U && present->PresentStartTime != 0U) {
                samples.push_back(PresentSample{
                    present->ProcessId,
                    present->SwapChainAddress,
                    present->PresentStartTime,
                });
            }
        }

        std::vector<std::shared_ptr<PresentEvent>> lostPresents;
        std::vector<ProcessEvent> processEvents;
        consumer->DequeueLostPresentEvents(lostPresents);
        consumer->DequeueProcessEvents(processEvents);
    }

    std::uint64_t QpcFrequency() const noexcept {
        try {
            std::scoped_lock lock(lifecycleMutex);
            if (trace.mQpcFrequency.QuadPart > 0) {
                return static_cast<std::uint64_t>(trace.mQpcFrequency.QuadPart);
            }
        } catch (...) {
        }
        LARGE_INTEGER frequency{};
        return QueryPerformanceFrequency(&frequency) != FALSE && frequency.QuadPart > 0
            ? static_cast<std::uint64_t>(frequency.QuadPart)
            : 0U;
    }

    mutable std::mutex lifecycleMutex;
    TraceSession trace{};
    std::unique_ptr<PMTraceConsumer> consumer;
    std::thread consumerThread;
    std::atomic_bool running{false};
    std::atomic_bool stopRequested{true};
    std::uint32_t trackedProcessId{0};
    std::string sessionName;
};

RealtimeSession::RealtimeSession() noexcept = default;

RealtimeSession::~RealtimeSession() {
    Stop();
}

bool RealtimeSession::Start(const std::string_view sessionName,
    const std::uint32_t initialProcessId, std::uint32_t* errorCode) noexcept {
    if (impl_ == nullptr) {
        impl_.reset(new (std::nothrow) Impl());
        if (impl_ == nullptr) {
            SetError(errorCode, ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
    }
    return impl_->Start(sessionName, initialProcessId, errorCode);
}

void RealtimeSession::Stop() noexcept {
    if (impl_ != nullptr) {
        impl_->Stop();
    }
}

bool RealtimeSession::IsRunning() const noexcept {
    return impl_ != nullptr && impl_->running.load(std::memory_order_acquire);
}

void RealtimeSession::SetTrackedProcess(const std::uint32_t processId) noexcept {
    if (impl_ != nullptr) {
        impl_->SetTrackedProcess(processId);
    }
}

void RealtimeSession::DrainPresentSamples(std::vector<PresentSample>& samples) {
    if (impl_ == nullptr) {
        samples.clear();
        return;
    }
    impl_->DrainPresentSamples(samples);
}

std::uint64_t RealtimeSession::QpcFrequency() const noexcept {
    if (impl_ != nullptr) {
        return impl_->QpcFrequency();
    }
    LARGE_INTEGER frequency{};
    return QueryPerformanceFrequency(&frequency) != FALSE && frequency.QuadPart > 0
        ? static_cast<std::uint64_t>(frequency.QuadPart)
        : 0U;
}

std::size_t RealtimeSession::CleanupStaleSessions() noexcept {
    try {
        std::array<TraceProperties, kMaximumTraceSessions> storage{};
        std::array<EVENT_TRACE_PROPERTIES*, kMaximumTraceSessions> properties{};
        for (std::size_t index = 0; index < storage.size(); ++index) {
            storage[index].Wnode.BufferSize = sizeof(TraceProperties);
            storage[index].LoggerNameOffset = offsetof(TraceProperties, loggerName);
            properties[index] = &storage[index];
        }

        ULONG loggerCount = 0;
        const ULONG status = QueryAllTracesW(properties.data(),
            static_cast<ULONG>(properties.size()), &loggerCount);
        if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) {
            return 0U;
        }

        std::size_t stopped = 0U;
        const std::size_t count = (std::min)(
            static_cast<std::size_t>(loggerCount), storage.size());
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t length = wcsnlen_s(storage[index].loggerName,
                std::size(storage[index].loggerName));
            const std::wstring_view name(storage[index].loggerName, length);
            std::uint32_t processId = 0U;
            if (!TryParseSessionOwner(name, processId) || ProcessIsActive(processId)) {
                continue;
            }
            const ULONG stopStatus = StopNamedSession(name);
            if (stopStatus == ERROR_SUCCESS || stopStatus == ERROR_WMI_INSTANCE_NOT_FOUND) {
                ++stopped;
            }
        }
        return stopped;
    } catch (...) {
        return 0U;
    }
}

} // namespace presentmon
