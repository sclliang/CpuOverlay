#pragma once
#include "peripheral_battery.h"
#include <algorithm>
#include <vector>

// Adapted from Voltide, commit 6f36629b415942ad49305190604382644f7240dc (MIT).
// See third_party/voltide for the upstream license and supported receiver table.
namespace BatteryProtocol {
inline bool RazerWirelessPid(uint16_t pid) {
    constexpr uint16_t ids[] = {0x007b, 0x00c1, 0x00df, 0x00b7, 0x00c3, 0x00bf,
        0x00e6, 0x00ab, 0x00cd, 0x00d7, 0x00c5, 0x00d1, 0x00c8, 0x00d4, 0x00b0,
        0x00db, 0x00b4, 0x00b8, 0x00b9, 0x00b3, 0x009a, 0x005a, 0x0045};
    return std::find(std::begin(ids), std::end(ids), pid) != std::end(ids);
}
inline uint8_t RazerTransaction(uint16_t pid) {
    // OpenRazer's battery query uses distinct legacy transaction IDs.
    if (pid == 0x005a) return 0x3f;
    return pid == 0x007b || pid == 0x0045 ? 0xff : 0x1f;
}
inline bool RazerCanCharge(uint16_t pid) {
    return pid != 0x00b4 && pid != 0x00b8 && pid != 0x00b9 && pid != 0x00d4 && pid != 0x009a;
}
inline std::vector<uint8_t> VendorRequest(BatteryDevice device) {
    if (device == BatteryDevice::MchoseKeyboard) {
        std::vector<uint8_t> b(20); b[0] = 0x13; b[1] = 0x4a; b[2] = 1; b[19] = 0x5e; return b;
    }
    if (device == BatteryDevice::CompxMouse) {
        std::vector<uint8_t> b(17); b[0] = 8; b[1] = 4; b[16] = 0x49; return b;
    }
    if (device == BatteryDevice::RogKeyboard) {
        std::vector<uint8_t> b(64); b[0] = 2; b[1] = 0x12; b[2] = 1; return b;
    }
    if (device == BatteryDevice::MchoseK7Mouse) {
        std::vector<uint8_t> b(64); b[0] = 0x4d; b[1] = 1; b[2] = 1; b[5] = 9; b[8] = 8; return b;
    }
    if (device == BatteryDevice::TaiduMouse) {
        std::vector<uint8_t> b(65); b[1] = 0x55; b[2] = 0x30; b[3] = 0xa5; b[4] = 0x0b;
        b[5] = 0x2e; b[6] = b[7] = b[8] = 1; return b;
    }
    return {};
}
inline std::optional<Reading> ParseVendor(BatteryDevice device, const uint8_t* b, size_t size) {
    if (device == BatteryDevice::MchoseKeyboard) {
        if (size != 20 || b[0] != 0x13 || b[1] != 0x4a || b[2] != 1 || b[3] || b[4] != 2 || b[5] > 100) return {};
        unsigned sum = 0; for (size_t i = 0; i < 19; ++i) sum += b[i];
        if (static_cast<uint8_t>(sum) != b[19]) return {};
        return Reading{static_cast<double>(b[5]), (b[6] >> 4) == 1};
    }
    if (device == BatteryDevice::CompxMouse) {
        if (size != 17 || b[0] != 8 || b[1] != 4 || b[2] || b[5] != 2 || b[6] > 100 || b[7] > 1) return {};
        unsigned sum = 0; for (size_t i = 1; i < 17; ++i) sum += b[i];
        if (static_cast<uint8_t>(sum) != 0x4d) return {};
        return Reading{static_cast<double>(b[6]), b[7] == 1};
    }
    if (device == BatteryDevice::RogKeyboard) {
        if (size != 64 || b[0] != 2 || b[1] != 0x12 || b[2] != 1 || b[6] > 100) return {};
        // The query echo contains the same header and an all-zero body.
        if (std::all_of(b + 3, b + size, [](uint8_t value) { return value == 0; })) return {};
        return Reading{static_cast<double>(b[6]), false};
    }
    if (device == BatteryDevice::MchoseK7Mouse) {
        if (size != 64 || b[0] != 0x4d || b[1] != 1 || b[2] != 1 || b[3] != 0x10 || b[4] || b[5] != 9 || b[6] ||
            b[8] != 0x37 || b[9] != 0x38 || b[10] != 0x26 || b[11] != 0x40 || b[18] != 2 || b[19] > 1 || b[20] > 100) return {};
        uint8_t checksum = 0; for (size_t i = 2; i < 24; ++i) checksum ^= b[i];
        if (b[24] != checksum || std::any_of(b + 25, b + size, [](uint8_t value) { return value != 0; })) return {};
        return Reading{static_cast<double>(b[20]), b[19] == 1};
    }
    if (device == BatteryDevice::TaiduMouse) {
        if (size != 65 || b[0] || b[1] != 0xaa || b[2] != 0x30 || b[3] != 0xa5 || b[4] != 0x0b ||
            b[5] != 0x0a || b[6] != 1 || b[7] != 1 || b[8] != 1 || b[9] > 100 || b[10] > 1) return {};
        return Reading{static_cast<double>(b[9]), b[10] == 1};
    }
    return {};
}
inline std::array<uint8_t, 65> MchoseA5Request(uint8_t command) {
    std::array<uint8_t, 65> b{}; b.fill(0xff); b[0] = 0x11; b[1] = command ^ 0xff; return b;
}
inline bool MchoseA5Ack(const uint8_t* b, size_t size) {
    return size >= 3 && (b[1] ^ 0xff) == 0xe5 && (b[2] ^ 0xff) == 6;
}
inline std::optional<Reading> ParseMchoseA5(const uint8_t* b, size_t size) {
    if (size != 65 || b[0] != 0x11 || (b[1] != 6 && b[1] != 0xf9)) return {};
    std::array<uint8_t, 63> payload{};
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = b[i + 2] ^ (b[1] == 0xf9 ? 0xff : 0);
    if (payload[0] != 0x37 || payload[1] != 0x38 || (!payload[2] && !payload[3]) ||
        (payload[2] == 0xff && payload[3] == 0xff) || payload[9] == 0 || payload[9] > 100 || payload[10] > 1) return {};
    // This protocol's zero is its disconnected sentinel, unlike true zero readings in other protocols.
    return Reading{static_cast<double>(payload[9]), payload[10] == 1};
}
}
