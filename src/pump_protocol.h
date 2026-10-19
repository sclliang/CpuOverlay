#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace PumpProtocol {
// Windows HID report-ID prefix (0), followed by the 64-byte JONSBO 5131:2007
// display payload. Only CPU temperature in Celsius is populated. This is a
// display report, not a command to control pump/fan speed or firmware.
using Report = std::array<uint8_t, 65>;
inline std::optional<Report> TemperatureReport(std::optional<double> celsius) {
    if (!celsius || !std::isfinite(*celsius) || *celsius < 0 || *celsius > 150) return {};
    const auto hundredths = static_cast<unsigned>(std::lround(*celsius * 100));
    Report result{};
    result[2] = 1; result[3] = 2;
    result[4] = static_cast<uint8_t>(hundredths / 100);
    result[5] = static_cast<uint8_t>(hundredths % 100);
    return result;
}
inline bool Fresh(uint64_t updated, uint64_t now) { return now >= updated && now - updated <= 3000; }
enum class Sampling { Paused, Idle, TemperatureOnly, Full };
inline Sampling SamplingMode(bool suspended, bool overlayVisible, bool panelConnected) {
    if (suspended) return Sampling::Paused;
    if (overlayVisible) return Sampling::Full;
    return panelConnected ? Sampling::TemperatureOnly : Sampling::Idle;
}
}
