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

enum class AmdBackend { Unsupported, K8, Family10, Zen };
constexpr AmdBackend SelectAmdBackend(uint32_t family) {
    if (family == 0x0f) return AmdBackend::K8;
    if (family >= 0x10 && family <= 0x16 && family != 0x13) return AmdBackend::Family10;
    if (family >= 0x17 && family <= 0x1a) return AmdBackend::Zen;
    return AmdBackend::Unsupported;
}
constexpr bool SupportsAmdFamily(uint32_t family) { return SelectAmdBackend(family) != AmdBackend::Unsupported; }
constexpr bool AmdUsesSmuTemperature(uint32_t family, uint32_t model) {
    return family == 0x15 && ((model & 0xf0) == 0x60 || (model & 0xf0) == 0x70);
}

inline std::optional<double> IntelTemperature(uint64_t target, uint64_t status) {
    // Standard DTS registers; intentionally independent of the Intel model ID.
    const double tjMax = static_cast<double>((target >> 16) & 0xff);
    if (!(status & (1ull << 31)) || tjMax < 50 || tjMax > 150) return {};
    const double temperature = tjMax - static_cast<double>((status >> 16) & 0x7f);
    return temperature >= -30 && temperature <= 150 ? std::optional<double>(temperature) : std::nullopt;
}

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

inline std::optional<double> AmdReportedTemperature(uint32_t raw, double tctlOffset, bool zen) {
    // THM_TCON_CUR_TMP (SMN 0x59800): CUR_TEMP[31:21], 0.125 C per step.
    // RANGE_SEL[19] or TJ_SEL[17:16] == 3 selects the -49 C range.
    // The latter is also used by newer Zen parts; apply the adjustment only once.
    if (raw == 0 || raw == UINT32_MAX) return {};
    // Bit 19 is RANGE_SEL only on Zen; do not apply it to a legacy PCI register.
    const bool adjustedRange = (zen && (raw & (1u << 19))) || (raw & (3u << 16)) == (3u << 16);
    const double temperature = static_cast<double>(raw >> 21) * 0.125 -
        (adjustedRange ? 49.0 : 0.0) - tctlOffset;
    if (!std::isfinite(temperature) || temperature < -30 || temperature > 150) return {};
    return temperature;
}

inline std::optional<double> AmdTemperature(uint32_t raw, double tctlOffset) {
    return AmdReportedTemperature(raw, tctlOffset, true);
}
inline std::optional<double> AmdLegacyTemperature(uint32_t raw) {
    return AmdReportedTemperature(raw, 0, false);
}
inline std::optional<double> AmdK8Temperature(uint32_t raw, uint32_t model) {
    if (raw == 0 || raw == UINT32_MAX) return {};
    // K8 THERMTRIP[23:16], including the AM2 65 nm sensor correction.
    const double offset = model >= 0x69 && model != 0xc1 && model != 0x6c && model != 0x7c ? 21 : 0;
    const double temperature = static_cast<double>((raw >> 16) & 0xff) - 49 + offset;
    return temperature >= -30 && temperature <= 150 ? std::optional<double>(temperature) : std::nullopt;
}

inline std::optional<double> EnergyMeterWatts(double milliwatts) {
    if (!std::isfinite(milliwatts) || milliwatts < 0 || milliwatts >= 2000000) return {};
    return milliwatts / 1000.0;
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
