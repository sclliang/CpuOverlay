#include "peripheral_battery.h"
#include <iostream>
#include <cmath>

int main() {
    int failed = 0, checks = 0;
    auto check = [&](bool ok, const char* message) { ++checks; if (!ok) { ++failed; std::cerr << message << '\n'; } };
    // Captured receiver replies, not synthetic values echoed from the request.
    std::array<uint8_t, 91> mouse{};
    mouse[1] = 2; mouse[2] = 0x1f; mouse[6] = 2; mouse[7] = 7; mouse[8] = 0x80; mouse[10] = 0x7c; mouse[89] = 0xf9;
    auto m = BatteryProtocol::ParseRazer(mouse.data(), mouse.size());
    check(m && std::abs(m->percent - 48.62745) < .00001, "captured mouse battery scales 0..255 to percent");
    const auto request = BatteryProtocol::RazerRequest();
    check(request[89] == 0x85 && !BatteryProtocol::ParseRazer(request.data(), request.size()), "request echo is never accepted as a response");
    check(!BatteryProtocol::ParseRazer(mouse.data(), 90), "short mouse response rejected");
    mouse[10] ^= 1;
    check(!BatteryProtocol::ParseRazer(mouse.data(), mouse.size()), "corrupt mouse checksum rejected");
    mouse[10] ^= 1; mouse[1] = 1;
    check(!BatteryProtocol::ParseRazer(mouse.data(), mouse.size()), "busy response rejected");
    mouse[1] = 2; mouse[2] = 0x3f;
    check(!BatteryProtocol::ParseRazer(mouse.data(), mouse.size()), "wrong transaction rejected");

    std::array<uint8_t, 64> keyboard{};
    keyboard[0] = 9; keyboard[1] = 0x87; keyboard[4] = 1; keyboard[6] = 2; keyboard[7] = 14; keyboard[63] = 0x5e;
    auto k = BatteryProtocol::ParseVgn(keyboard.data(), keyboard.size());
    check(k && k->percent == 14 && !k->charging, "captured VGN battery reply");
    auto checksum = [&] { keyboard[63] = 0; unsigned total = 0; for (auto b : keyboard) total += b; keyboard[63] = static_cast<uint8_t>(255 - (total & 255)); };
    keyboard[8] = 0x10; checksum();
    k = BatteryProtocol::ParseVgn(keyboard.data(), keyboard.size());
    check(k && k->charging, "charging status parsed separately from battery percent");
    keyboard[7] = 101; checksum();
    check(!BatteryProtocol::ParseVgn(keyboard.data(), keyboard.size()), "out-of-range keyboard percent rejected even with valid checksum");
    keyboard[7] = 0; checksum();
    k = BatteryProtocol::ParseVgn(keyboard.data(), keyboard.size());
    check(k && k->percent == 0, "real zero percent is a valid measurement");
    keyboard[1] = 0xfe; checksum();
    check(!BatteryProtocol::ParseVgn(keyboard.data(), keyboard.size()), "unrelated event is not a battery response");
    keyboard[1] = 0x87; keyboard[4] = 2; checksum();
    check(!BatteryProtocol::ParseVgn(keyboard.data(), keyboard.size()), "wrong packet sequence rejected");
    keyboard[4] = 1; checksum(); keyboard[63] ^= 1;
    check(!BatteryProtocol::ParseVgn(keyboard.data(), keyboard.size()), "corrupt VGN checksum rejected");
    check(!BatteryProtocol::ParseVgn(keyboard.data(), 63), "short VGN response rejected");
    check(BatteryProtocol::VgnRequest()[63] == 0x6e, "VGN read-only request matches hardware capture");

    BatterySample cached;
    UpdateBattery(cached, {}, true, 10, 1460);
    check(!cached.percent && cached.state == BatteryState::Unavailable, "first timeout is unknown, not zero");
    UpdateBattery(cached, BatteryProtocol::Reading{14, true}, true, 100, 0);
    check(cached.percent == 14 && cached.state == BatteryState::Fresh && cached.charging, "successful reading becomes fresh");
    UpdateBattery(cached, {}, true, 200, 1460);
    check(cached.percent == 14 && cached.state == BatteryState::Stale && !cached.charging && cached.updatedMs == 100, "timeout preserves last successful value and timestamp without stale charging flag");
    UpdateBattery(cached, {}, false, 300, 1167);
    check(cached.percent == 14 && cached.state == BatteryState::Disconnected, "disconnect is distinct from sleep or timeout");
    UpdateBattery(cached, BatteryProtocol::Reading{0, false}, true, 400, 0);
    check(cached.percent == 0 && cached.state == BatteryState::Fresh && cached.error == 0, "reconnect replaces stale reading including true zero");
    check(AgeBattery(cached, 35400).state == BatteryState::Fresh && AgeBattery(cached, 35401).state == BatteryState::Stale, "hidden/resume cache ages without erasing the last value");
    std::cout << checks << " checks, " << failed << " failures\n";
    return failed ? 1 : 0;
}
