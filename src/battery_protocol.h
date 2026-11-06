#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <optional>

// Read-only packets for the receivers verified on this machine. These are
// independently implemented wire formats, not bundled third-party drivers.
namespace BatteryProtocol {
struct Reading { double percent = 0; bool charging = false; };
inline std::array<uint8_t, 91> RazerRequest(uint8_t transaction = 0x1f, uint8_t command = 0x80) {
    std::array<uint8_t, 91> b{};
    b[2] = transaction; b[6] = 2; b[7] = 7; b[8] = command;
    for (size_t i = 3; i < 89; ++i) b[89] ^= b[i];
    return b;
}
inline bool RazerReply(const uint8_t* b, size_t size, uint8_t transaction, uint8_t command) {
    if (size != 91 || b[0] != 0 || b[1] != 2 || b[2] != transaction ||
        b[3] || b[4] || b[5] || b[6] != 2 || b[7] != 7 || b[8] != command || b[90]) return false;
    uint8_t crc = 0;
    for (size_t i = 3; i < 89; ++i) crc ^= b[i];
    return crc == b[89];
}
inline std::optional<Reading> ParseRazer(const uint8_t* b, size_t size, uint8_t transaction = 0x1f) {
    if (!RazerReply(b, size, transaction, 0x80)) return {};
    return Reading{b[10] * 100.0 / 255.0, false};
}
inline std::optional<bool> ParseRazerCharging(const uint8_t* b, size_t size, uint8_t transaction) {
    if (!RazerReply(b, size, transaction, 0x84) || b[10] > 1) return {};
    return b[10] == 1;
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

using HidppReport = std::array<uint8_t, 20>;
// PeriPower/Voltide use 0x0e, G HUB uses 0x0d. Keep this client's replies separate.
constexpr uint8_t HidppSoftwareId = 0x0c;
inline std::optional<HidppReport> HidppRequest(uint8_t slot, uint8_t feature, uint8_t function,
    std::initializer_list<uint8_t> parameters = {}) {
    if (slot < 1 || slot > 6 || function > 15 || parameters.size() > 16) return {};
    HidppReport b{};
    b[0] = 0x11; b[1] = slot; b[2] = feature;
    b[3] = static_cast<uint8_t>((function << 4) | HidppSoftwareId);
    size_t offset = 4;
    for (auto parameter : parameters) b[offset++] = parameter;
    return b;
}
inline bool HidppFrame(const uint8_t* b, size_t size) {
    return size >= 7 && ((b[0] == 0x10) || (b[0] == 0x11 && size >= 20));
}
inline bool HidppMatches(const uint8_t* b, size_t size, const HidppReport& request) {
    return HidppFrame(b, size) && b[1] == request[1] && b[2] == request[2] && b[3] == request[3];
}
inline bool HidppError(const uint8_t* b, size_t size, const HidppReport& request) {
    return HidppFrame(b, size) && b[1] == request[1] && (b[2] == 0xff || b[2] == 0x8f) &&
        b[3] == request[2] && b[4] == request[3];
}
inline std::optional<Reading> ParseLogitech(const uint8_t* parameters, size_t size,
    bool unified, bool percentageSupported = true) {
    if (size < (unified ? 4u : 3u) || parameters[0] > 100 || !percentageSupported ||
        (!unified && parameters[0] == 0)) return {};
    // Unified 0x1004 status 4 means charging error, not charging.
    return Reading{static_cast<double>(parameters[0]), parameters[2] == 1 || parameters[2] == 2};
}
inline bool LogitechKind(uint8_t kind, bool mouse) {
    return mouse ? (kind == 3 || kind == 4 || kind == 5) : (kind == 0 || kind == 2);
}
}
