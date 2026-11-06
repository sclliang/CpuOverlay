#include "vendor_battery_protocol.h"
#include <array>
#include <cmath>
#include <iostream>

int main() {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool value, const char* label) { ++checks; if (!value) { ++failures; std::cerr << label << '\n'; } };
    using namespace BatteryProtocol;
    check(RazerWirelessPid(0x007b) && RazerWirelessPid(0x00c1) && RazerWirelessPid(0x00b8), "known Razer wireless receivers are supported");
    check(!RazerWirelessPid(0x0555) && !RazerWirelessPid(0xffff) && !RazerWirelessPid(0x007a), "headsets and unverified Razer PIDs are excluded");
    check(RazerTransaction(0x007b) == 0xff && RazerTransaction(0x005a) == 0x3f && RazerTransaction(0x00b8) == 0x1f,
        "legacy Razer receivers use their model-specific battery transaction ID");
    auto charge = RazerRequest(0xff, 0x84); charge[1] = 2; charge[10] = 1; charge[89] ^= 1;
    check(ParseRazerCharging(charge.data(), charge.size(), 0xff) == true &&
        !ParseRazer(charge.data(), charge.size(), 0xff), "charging response is separate from the battery level response");
    check(!RazerCanCharge(0x00b8) && !RazerCanCharge(0x009a) && RazerCanCharge(0x007b), "replaceable AA mice are never queried as rechargeable models");
    check(LogitechKind(0, false) && LogitechKind(2, false) && !LogitechKind(3, false), "HID++ keyboard and numpad types are separate from mice");
    check(LogitechKind(3, true) && LogitechKind(4, true) && LogitechKind(5, true) && !LogitechKind(0, true) && !LogitechKind(7, true),
        "HID++ does not label a keyboard or receiver as a mouse");
    auto razer = RazerRequest(0x20); razer[1] = 2; razer[10] = 0x15; razer[89] = 0x90;
    const auto mouse = ParseRazer(razer.data(), razer.size(), 0x20);
    check(mouse && std::abs(mouse->percent - 8.235294) < .00001 && !ParseRazer(razer.data(), razer.size()),
        "a raw Razer value below 100 is still scaled from 255 and correlated to its transaction");

    auto keyboard = VendorRequest(BatteryDevice::MchoseKeyboard);
    check(keyboard.size() == 20 && keyboard[19] == 0x5e && !ParseVendor(BatteryDevice::MchoseKeyboard, keyboard.data(), keyboard.size()), "MCHOSE keyboard request echo is rejected");
    keyboard[4] = 2; keyboard[5] = 100; keyboard[6] = 1; keyboard[19] = 0xc5;
    auto reading = ParseVendor(BatteryDevice::MchoseKeyboard, keyboard.data(), keyboard.size());
    check(reading && reading->percent == 100 && !reading->charging, "Voltide K99 reply 13 4A 01 00 02 64 01 decodes correctly");
    keyboard[6] = 0x11; keyboard[19] = 0xd5;
    check(ParseVendor(BatteryDevice::MchoseKeyboard, keyboard.data(), keyboard.size())->charging, "MCHOSE charge flag is in the high nibble");
    keyboard[19] ^= 1;
    check(!ParseVendor(BatteryDevice::MchoseKeyboard, keyboard.data(), keyboard.size()), "MCHOSE keyboard checksum corruption rejected");

    const std::array<uint8_t, 17> imecoo{8, 4, 0, 0, 0, 2, 0x1e, 0, 0x0e, 0x5a, 0, 0, 0, 0, 0, 0, 0xc1};
    reading = ParseVendor(BatteryDevice::CompxMouse, imecoo.data(), imecoo.size());
    check(reading && reading->percent == 30 && !reading->charging, "Voltide IMECOO hardware capture decodes to 30 percent");
    auto compx = VendorRequest(BatteryDevice::CompxMouse);
    check(compx[16] == 0x49 && !ParseVendor(BatteryDevice::CompxMouse, compx.data(), compx.size()), "CompX query echo cannot become fake zero percent");
    auto compxReply = imecoo; compxReply[6] ^= 1;
    check(!ParseVendor(BatteryDevice::CompxMouse, compxReply.data(), compxReply.size()), "CompX checksum corruption rejected");
    compxReply = imecoo; compxReply[6] = 0;
    unsigned sum = 0; for (size_t i = 1; i < 16; ++i) sum += compxReply[i]; compxReply[16] = static_cast<uint8_t>(0x4d - sum);
    check(ParseVendor(BatteryDevice::CompxMouse, compxReply.data(), compxReply.size())->percent == 0, "a genuine CompX zero measurement is retained");

    auto rog = VendorRequest(BatteryDevice::RogKeyboard);
    check(!ParseVendor(BatteryDevice::RogKeyboard, rog.data(), rog.size()), "ROG all-zero query body is not a measurement");
    rog[6] = 48; rog[7] = 2; rog[8] = 1; rog[10] = 20; rog[11] = 48; rog[12] = 0xed; rog[13] = 0x0e;
    check(ParseVendor(BatteryDevice::RogKeyboard, rog.data(), rog.size())->percent == 48, "ROG reads battery byte 6 rather than low-battery threshold byte 10");
    rog[6] = 101;
    check(!ParseVendor(BatteryDevice::RogKeyboard, rog.data(), rog.size()), "ROG out-of-range response rejected");

    auto k7 = VendorRequest(BatteryDevice::MchoseK7Mouse);
    check(k7[8] == 8 && !ParseVendor(BatteryDevice::MchoseK7Mouse, k7.data(), k7.size()), "K7 0x0900 query and response remain distinct");
    k7[3] = 0x10; k7[8] = 0x37; k7[9] = 0x38; k7[10] = 0x26; k7[11] = 0x40; k7[18] = 2; k7[19] = 1; k7[20] = 87;
    auto k7Checksum = [&] { k7[24] = 0; for (size_t i = 2; i < 24; ++i) k7[24] ^= k7[i]; };
    k7Checksum(); reading = ParseVendor(BatteryDevice::MchoseK7Mouse, k7.data(), k7.size());
    check(reading && reading->percent == 87 && reading->charging, "K7 identity, connection and checksum are validated");
    k7[18] = 0; k7Checksum();
    check(!ParseVendor(BatteryDevice::MchoseK7Mouse, k7.data(), k7.size()), "K7 disconnected sentinel never becomes a reading");
    k7[18] = 2; k7[10] ^= 1; k7Checksum();
    check(!ParseVendor(BatteryDevice::MchoseK7Mouse, k7.data(), k7.size()), "K7 replies from a different product are rejected");

    auto a5 = MchoseA5Request(6);
    check(a5[0] == 0x11 && a5[1] == 0xf9 && a5[64] == 0xff, "A5 OST request has the correct inverted command and padding");
    std::array<uint8_t, 65> a5Reply{}; a5Reply[0] = 0x11; a5Reply[1] = 6;
    a5Reply[2] = 0x37; a5Reply[3] = 0x38; a5Reply[4] = 0x22; a5Reply[5] = 0x40; a5Reply[11] = 74;
    check(ParseMchoseA5(a5Reply.data(), a5Reply.size())->percent == 74, "A5 plain bank decodes the Voltide 74 percent sample");
    a5Reply[1] = 0xf9; for (size_t i = 2; i < a5Reply.size(); ++i) a5Reply[i] ^= 0xff;
    check(ParseMchoseA5(a5Reply.data(), a5Reply.size())->percent == 74, "A5 inverted bank decodes to the same value");
    a5Reply[11] = 0xff;
    check(!ParseMchoseA5(a5Reply.data(), a5Reply.size()), "A5 offline zero sentinel is not an empty battery");
    const uint8_t ack[] = {0x11, 0x1a, 0xf9};
    check(MchoseA5Ack(ack, sizeof(ack)) && !MchoseA5Ack(ack, 2), "A5 feature reads require the complete preceding OST ACK");

    auto taidu = VendorRequest(BatteryDevice::TaiduMouse);
    check(!ParseVendor(BatteryDevice::TaiduMouse, taidu.data(), taidu.size()), "TAIDU query marker 55 is never accepted as reply AA");
    taidu[1] = 0xaa; taidu[5] = 0x0a; taidu[9] = 68; taidu[10] = 1;
    reading = ParseVendor(BatteryDevice::TaiduMouse, taidu.data(), taidu.size());
    check(reading && reading->percent == 68 && reading->charging, "TAIDU captured 68 percent response and charge flag decoded");
    for (const auto device : {BatteryDevice::MchoseKeyboard, BatteryDevice::RogKeyboard, BatteryDevice::CompxMouse,
        BatteryDevice::MchoseK7Mouse, BatteryDevice::TaiduMouse}) {
        const auto request = VendorRequest(device);
        check(!ParseVendor(device, request.data(), request.size() - 1), "vendor protocol truncation is rejected");
    }
    std::cout << checks << " vendor battery checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
