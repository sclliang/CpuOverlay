#pragma once
#include "battery_protocol.h"
#include <memory>
#include <string>

enum class BatteryState { Unavailable, Fresh, Stale, Disconnected };
struct BatterySample {
    std::optional<double> percent;
    bool charging = false;
    BatteryState state = BatteryState::Unavailable;
    uint64_t updatedMs = 0;
    unsigned long error = 0;
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
