#pragma once
#include "battery_protocol.h"
#include <memory>
#include <string>

enum class BatteryState { Unavailable, Fresh, Stale, Disconnected };
enum class BatteryDevice { Unknown, LogitechHidpp, VgnV98Pro, RazerWireless,
    MchoseKeyboard, RogKeyboard, CompxMouse, MchoseK7Mouse, MchoseA5Mouse, TaiduMouse };
struct BatterySample {
    std::optional<double> percent;
    bool charging = false;
    BatteryState state = BatteryState::Unavailable;
    uint64_t updatedMs = 0;
    unsigned long error = 0;
    BatteryDevice device = BatteryDevice::Unknown;
    std::wstring model;
    uint16_t vendorId = 0, productId = 0;
};
struct PeripheralSample { BatterySample keyboard, mouse; };

// Last good values are deliberately retained on timeout/disconnect, never
// substituted with zero. Timestamps are monotonic and only valid this session.
inline void UpdateBattery(BatterySample& s, const std::optional<BatteryProtocol::Reading>& reading,
    bool present, uint64_t now, unsigned long error) {
    s.error = error;
    if (reading) {
        s.percent = reading->percent; s.charging = reading->charging;
        s.updatedMs = now; s.state = BatteryState::Fresh;
    } else {
        s.charging = false;
        s.state = !present ? BatteryState::Disconnected : (s.percent ? BatteryState::Stale : BatteryState::Unavailable);
    }
}
inline BatterySample AgeBattery(BatterySample s, uint64_t now) {
    if (s.state == BatteryState::Fresh && now - s.updatedMs > 35000) {
        s.state = BatteryState::Stale; s.charging = false;
    }
    return s;
}
inline const char* BatteryStateName(BatteryState state) {
    switch (state) {
    case BatteryState::Fresh: return "fresh";
    case BatteryState::Stale: return "stale";
    case BatteryState::Disconnected: return "disconnected";
    default: return "unavailable";
    }
}
inline const char* BatteryDeviceName(BatteryDevice device) {
    switch (device) {
    case BatteryDevice::LogitechHidpp: return "Logitech HID++";
    case BatteryDevice::VgnV98Pro: return "VGN V98Pro V4 (391D:2A04)";
    case BatteryDevice::RazerWireless: return "Razer wireless mouse";
    case BatteryDevice::MchoseKeyboard: return "MCHOSE keyboard (3554:FA09)";
    case BatteryDevice::RogKeyboard: return "ROG OMNI keyboard (0B05:1ACE)";
    case BatteryDevice::CompxMouse: return "IMECOO P1 / ATK A9 Air";
    case BatteryDevice::MchoseK7Mouse: return "MCHOSE K7 V2 Ultra+";
    case BatteryDevice::MchoseA5Mouse: return "MCHOSE A5 mouse";
    case BatteryDevice::TaiduMouse: return "TAIDU TSG809";
    default: return "unknown";
    }
}
inline std::wstring BatteryModelName(const BatterySample& sample) {
    if (!sample.model.empty()) return sample.model;
    const std::string name = BatteryDeviceName(sample.device);
    return std::wstring(name.begin(), name.end());
}
class PeripheralBatteryMonitor {
public:
    PeripheralBatteryMonitor();
    ~PeripheralBatteryMonitor();
    PeripheralBatteryMonitor(const PeripheralBatteryMonitor&) = delete;
    PeripheralBatteryMonitor& operator=(const PeripheralBatteryMonitor&) = delete;
    void SetActive(bool active);
    PeripheralSample Snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
