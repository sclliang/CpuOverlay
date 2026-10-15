#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace SensorMath {
struct CpuSignature { uint32_t family, model; };

constexpr CpuSignature DecodeSignature(uint32_t signature) {
    const uint32_t baseFamily = (signature >> 8) & 0xf;
    return {baseFamily + (baseFamily == 0xf ? (signature >> 20) & 0xff : 0),
        ((signature >> 4) & 0xf) | ((baseFamily == 0x6 || baseFamily == 0xf) ? (signature >> 12) & 0xf0 : 0)};
}

// Matches the family gate in the official AMDFamily17 module, including Zen 5.
constexpr bool SupportsAmdFamily(uint32_t family) { return family >= 0x17 && family <= 0x1a; }

inline double AmdTctlOffset(uint32_t family, std::string_view brand) {
    if (family != 0x17) return 0;
    // Model-specific Tctl-to-Tdie corrections documented by Linux k10temp:
    // https://github.com/torvalds/linux/blob/master/drivers/hwmon/k10temp.c
    if (brand.find("AMD Ryzen 5 1600X") != std::string_view::npos ||
        brand.find("AMD Ryzen 7 1700X") != std::string_view::npos ||
        brand.find("AMD Ryzen 7 1800X") != std::string_view::npos) return 20;
    if (brand.find("AMD Ryzen 7 2700X") != std::string_view::npos) return 10;
    if (brand.find("AMD Ryzen Threadripper 19") != std::string_view::npos ||
        brand.find("AMD Ryzen Threadripper 29") != std::string_view::npos) return 27;
    return 0;
}

inline std::optional<double> AmdTemperature(uint32_t raw, double tctlOffset) {
    // THM_TCON_CUR_TMP (SMN 0x59800): CUR_TEMP[31:21], 0.125 C per step.
    // RANGE_SEL[19] or TJ_SEL[17:16] == 3 selects the -49 C range.
    // The latter is also used by newer Zen parts; apply the adjustment only once.
    if (raw == 0 || raw == UINT32_MAX) return {};
    const bool adjustedRange = (raw & (1u << 19)) || (raw & (3u << 16)) == (3u << 16);
    const double temperature = static_cast<double>(raw >> 21) * 0.125 -
        (adjustedRange ? 49.0 : 0.0) - tctlOffset;
    if (!std::isfinite(temperature) || temperature < -30 || temperature > 150) return {};
    return temperature;
}

inline double EnergyUnitJoules(uint64_t raw) {
    // Intel MSR 0x606 and AMD MSR 0xC0010299 both encode ESU in [12:8].
    // AMD OSRR 56255, section 2.1.10.3: energy unit = 1 / 2^ESU joules.
    return std::ldexp(1.0, -static_cast<int>((raw >> 8) & 0x1f));
}

inline std::optional<double> PackagePower(uint32_t previous, uint32_t current, double joules, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.05 || seconds >= 10 || !std::isfinite(joules) || joules <= 0) return {};
    // Both vendors expose a wrapping 32-bit package energy counter.
    const uint32_t delta = current - previous;
    const double watts = static_cast<double>(delta) * joules / seconds;
    if (!std::isfinite(watts) || watts < 0 || watts >= 2000) return {};
    return watts;
}
}
