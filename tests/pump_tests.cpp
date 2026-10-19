#include "pump_protocol.h"
#include <iostream>
#include <limits>

int main() {
    int checks = 0, failed = 0;
    auto check = [&](bool ok, const char* name) { ++checks; if (!ok) { ++failed; std::cerr << name << '\n'; } };
    const auto packet = PumpProtocol::TemperatureReport(53.25);
    check(packet && packet->size() == 65 && (*packet)[0] == 0 && (*packet)[1] == 0 && (*packet)[2] == 1 && (*packet)[3] == 2,
        "unnumbered Windows HID report prefix and vendor header");
    check(packet && (*packet)[4] == 53 && (*packet)[5] == 25 && (*packet)[6] == 0, "CPU Celsius temperature field offsets");
    bool unusedZero = packet.has_value();
    if (packet) for (size_t i = 7; i < packet->size(); ++i) unusedZero = unusedZero && (*packet)[i] == 0;
    check(unusedZero, "temperature-only frame has no unrelated field values");
    const auto rounded = PumpProtocol::TemperatureReport(59.999);
    check(rounded && (*rounded)[4] == 60 && (*rounded)[5] == 0, "fraction carries without wrapping");
    check(!PumpProtocol::TemperatureReport({}), "missing sensor value is not sent as zero");
    check(!PumpProtocol::TemperatureReport(-1) && !PumpProtocol::TemperatureReport(150.01), "out-of-range sensor values not encoded");
    check(!PumpProtocol::TemperatureReport(std::numeric_limits<double>::quiet_NaN()) &&
        !PumpProtocol::TemperatureReport(std::numeric_limits<double>::infinity()), "NaN and infinity rejected");
    const auto zero = PumpProtocol::TemperatureReport(0);
    check(zero && (*zero)[4] == 0 && (*zero)[5] == 0, "real zero is distinct from missing data");
    check(PumpProtocol::Fresh(100, 3100) && !PumpProtocol::Fresh(100, 3101) && !PumpProtocol::Fresh(200, 100), "stale and invalid timestamps cannot be resent");
    using S = PumpProtocol::Sampling;
    check(PumpProtocol::SamplingMode(false, false, true) == S::TemperatureOnly, "hidden overlay still samples for connected pump");
    check(PumpProtocol::SamplingMode(false, true, true) == S::Full && PumpProtocol::SamplingMode(false, true, false) == S::Full,
        "visible overlay retains full sampling regardless of pump");
    check(PumpProtocol::SamplingMode(false, false, false) == S::Idle, "hidden overlay without pump does not poll sensors");
    check(PumpProtocol::SamplingMode(true, true, true) == S::Paused && PumpProtocol::SamplingMode(true, false, true) == S::Paused,
        "system suspend overrides both display modes");
    std::cout << checks << " checks, " << failed << " failures\n";
    return failed ? 1 : 0;
}
