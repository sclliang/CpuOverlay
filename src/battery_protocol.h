#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>

// Read-only packets for the two receivers verified on this machine. These are
// independently implemented wire formats, not bundled third-party drivers.
namespace BatteryProtocol {
struct Reading { double percent = 0; bool charging = false; };
inline std::array<uint8_t, 91> RazerRequest() {
    std::array<uint8_t, 91> b{};
    b[2] = 0x1f; b[6] = 2; b[7] = 7; b[8] = 0x80;
    for (size_t i = 3; i < 89; ++i) b[89] ^= b[i];
    return b;
}
inline std::optional<Reading> ParseRazer(const uint8_t* b, size_t size) {
    if (size != 91 || b[0] != 0 || b[1] != 2 || b[2] != 0x1f ||
        b[3] || b[4] || b[5] || b[6] != 2 || b[7] != 7 || b[8] != 0x80 || b[90]) return {};
    uint8_t crc = 0;
    for (size_t i = 3; i < 89; ++i) crc ^= b[i];
    if (crc != b[89]) return {};
    return Reading{b[10] * 100.0 / 255.0, false};
}
inline std::array<uint8_t, 64> VgnRequest() {
    std::array<uint8_t, 64> b{};
    b[0] = 9; b[1] = 0x87; b[4] = 1;
    b[63] = static_cast<uint8_t>(255 - ((b[0] + b[1] + b[4]) & 255));
    return b;
}
inline std::optional<Reading> ParseVgn(const uint8_t* b, size_t size) {
    if (size != 64 || b[0] != 9 || b[1] != 0x87 || b[2] || b[3] ||
        b[4] != 1 || b[5] || b[6] != 2 || b[7] > 100) return {};
    unsigned sum = 0;
    for (size_t i = 0; i < size; ++i) sum += b[i];
    if ((sum & 255) != 255) return {};
    return Reading{static_cast<double>(b[7]), (b[8] & 0xf0) != 0};
}
}
