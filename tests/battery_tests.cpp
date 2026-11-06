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

    const auto hidpp = BatteryProtocol::HidppRequest(1, 8, 1);
    check(hidpp && (*hidpp)[0] == 0x11 && (*hidpp)[1] == 1 && (*hidpp)[2] == 8 && (*hidpp)[3] == 0x1c,
        "HID++ long request uses its own software ID");
    const auto ping = BatteryProtocol::HidppRequest(6, 0, 1, {0, 0, 0x5a});
    check(ping && (*ping)[1] == 6 && (*ping)[6] == 0x5a && (*ping)[19] == 0, "HID++ ping preserves slot, cookie and zero padding");
    check(!BatteryProtocol::HidppRequest(0, 0, 0) && !BatteryProtocol::HidppRequest(7, 0, 0) &&
        !BatteryProtocol::HidppRequest(1, 0, 16) &&
        !BatteryProtocol::HidppRequest(1, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
        "HID++ rejects invalid slots, functions and oversized parameters");
    auto reply = *hidpp;
    reply[4] = 0x41; reply[5] = 8;
    check(BatteryProtocol::HidppMatches(reply.data(), reply.size(), *hidpp), "HID++ matches the requested slot, feature and function");
    check(!BatteryProtocol::HidppMatches(reply.data(), 19, *hidpp) &&
        !BatteryProtocol::HidppMatches(reply.data(), 0, *hidpp), "HID++ ignores truncated and empty frames");
    reply[3] = 0x1e;
    check(!BatteryProtocol::HidppMatches(reply.data(), reply.size(), *hidpp), "PeriPower replies do not satisfy CpuOverlay requests");
    reply = *hidpp; reply[1] = 2;
    check(!BatteryProtocol::HidppMatches(reply.data(), reply.size(), *hidpp), "HID++ ignores other paired devices");
    reply = *hidpp; reply[2] = 9;
    check(!BatteryProtocol::HidppMatches(reply.data(), reply.size(), *hidpp), "HID++ ignores other feature indices");
    reply = *hidpp; reply[0] = 0x10;
    check(BatteryProtocol::HidppMatches(reply.data(), 7, *hidpp) &&
        BatteryProtocol::HidppMatches(reply.data(), reply.size(), *hidpp), "HID++ accepts short replies with or without HID padding");
    reply[2] = 0x8f; reply[3] = (*hidpp)[2]; reply[4] = (*hidpp)[3]; reply[5] = 2;
    check(BatteryProtocol::HidppError(reply.data(), 7, *hidpp), "HID++ short protocol error is correlated to the request");
    reply[0] = 0x11; reply[2] = 0xff;
    check(BatteryProtocol::HidppError(reply.data(), reply.size(), *hidpp) &&
        !BatteryProtocol::HidppError(reply.data(), 7, *hidpp), "HID++ long errors require a complete frame");
    reply[4] ^= 1;
    check(!BatteryProtocol::HidppError(reply.data(), reply.size(), *hidpp), "HID++ ignores errors for other client IDs");

    // PeriPower captures: K75M 0x1004 v5, 41 08 00 00 = 65%; earlier 50 08 00 00 = 80%.
    std::array<uint8_t, 4> logitech{0x41, 8, 0, 0};
    auto l = BatteryProtocol::ParseLogitech(logitech.data(), logitech.size(), true);
    check(l && l->percent == 65 && !l->charging, "live K75M capture decodes to 65 percent");
    logitech[0] = 80;
    check(BatteryProtocol::ParseLogitech(logitech.data(), logitech.size(), true)->percent == 80, "earlier K75M capture decodes to 80 percent");
    logitech[0] = 0;
    l = BatteryProtocol::ParseLogitech(logitech.data(), logitech.size(), true);
    check(l && l->percent == 0, "Unified Battery retains a genuine zero percent");
    check(!BatteryProtocol::ParseLogitech(logitech.data(), logitech.size(), false), "legacy HID++ zero means unknown");
    check(!BatteryProtocol::ParseLogitech(logitech.data(), logitech.size(), true, false), "coarse levels are never converted to invented percentages");
    logitech[0] = 101;
    check(!BatteryProtocol::ParseLogitech(logitech.data(), logitech.size(), true), "HID++ rejects percentages over 100");
    logitech[0] = 65;
    check(!BatteryProtocol::ParseLogitech(logitech.data(), 3, true) &&
        !BatteryProtocol::ParseLogitech(logitech.data(), 2, false), "HID++ rejects incomplete battery parameters");
    check(BatteryProtocol::ParseLogitech(logitech.data(), 3, false)->percent == 65, "legacy 0x1000 battery fallback accepts valid percentages");
    for (uint8_t status = 0; status <= 5; ++status) {
        logitech[2] = status;
        l = BatteryProtocol::ParseLogitech(logitech.data(), logitech.size(), true);
        check(l && l->charging == (status == 1 || status == 2), "HID++ charging, full and error statuses remain distinct");
    }

    mouse = BatteryProtocol::RazerRequest(); mouse[1] = 2; mouse[10] = 0x15; mouse[89] = 0x90;
    m = BatteryProtocol::ParseRazer(mouse.data(), mouse.size());
    check(m && std::round(m->percent) == 8, "current Razer capture matches PeriPower's 8 percent");
    mouse[1] = 5;
    check(!BatteryProtocol::ParseRazer(mouse.data(), mouse.size()), "failed Razer commands never become measurements");

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
