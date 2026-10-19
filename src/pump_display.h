#pragma once
#include "pump_protocol.h"
#include <memory>

struct PumpDisplayStatus {
    bool connected = false, active = false;
    uint64_t sent = 0, lastSentMs = 0;
    unsigned long error = 0;
    std::optional<double> temperatureC;
};
class PumpDisplay {
public:
    PumpDisplay();
    ~PumpDisplay();
    PumpDisplay(const PumpDisplay&) = delete;
    PumpDisplay& operator=(const PumpDisplay&) = delete;
    void SetActive(bool active);
    void Update(std::optional<double> temperature);
    PumpDisplayStatus Snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
