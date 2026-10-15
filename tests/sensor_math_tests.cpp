#include "sensor_math.h"
#include <iostream>
#include <limits>

namespace {
unsigned failures = 0, checks = 0;
void Check(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
void Near(std::optional<double> actual, double expected, const char* name) {
    Check(actual && std::abs(*actual - expected) < 1e-9, name);
}
}

int main() {
    using namespace SensorMath;
    const auto strix = DecodeSignature(0x00b20f40);
    Check(strix.family == 0x1a && strix.model == 0x24, "Ryzen AI 9 HX 370 extended family/model");
    const auto zen3 = DecodeSignature(0x00a20f10);
    Check(zen3.family == 0x19 && zen3.model == 0x21, "Zen 3 extended family/model");
    const auto intel = DecodeSignature(0x00090672);
    Check(intel.family == 6 && intel.model == 0x97, "Alder Lake signature unchanged");
    Check(!SupportsAmdFamily(0x0e) && !SupportsAmdFamily(0x13) && !SupportsAmdFamily(0x1b), "unknown and reserved AMD families gated");
    Check(SupportsAmdFamily(0x17) && SupportsAmdFamily(0x19) && SupportsAmdFamily(0x1a), "Zen families supported");
    Check(SelectAmdBackend(0x0f) == AmdBackend::K8, "K8 uses its own temperature module");
    for (const auto family : {0x10u, 0x11u, 0x12u, 0x14u, 0x15u, 0x16u})
        Check(SelectAmdBackend(family) == AmdBackend::Family10, "Phenom, Turion, FX and pre-Zen APU dispatch");
    Check(AmdUsesSmuTemperature(0x15, 0x60) && AmdUsesSmuTemperature(0x15, 0x6f) &&
        AmdUsesSmuTemperature(0x15, 0x70) && AmdUsesSmuTemperature(0x15, 0x7f), "Carrizo/Bristol/Stoney use SMU temperature");
    Check(!AmdUsesSmuTemperature(0x15, 0x50) && !AmdUsesSmuTemperature(0x15, 0x80) &&
        !AmdUsesSmuTemperature(0x16, 0x60), "other legacy parts use PCI temperature");
    // Intel thermal decoding has no model allowlist. Exercise the same standard
    // DTS format with signatures of both recent and newly added client families.
    const uint32_t recentIntelModels[]{0xaa, 0xac, 0xb5, 0xbd, 0xc5, 0xc6, 0xcc, 0xd5};
    for (const auto model : recentIntelModels) {
        const auto signature = DecodeSignature(0x600 | ((model & 0xf) << 4) | ((model & 0xf0) << 12));
        Check(signature.family == 6 && signature.model == model, "Meteor/Arrow/Lunar/Panther/Wildcat model decoding");
    }
    Near(IntelTemperature(100ull << 16, (1ull << 31) | (35ull << 16)), 65, "Intel valid package/core DTS");
    Near(IntelTemperature(110ull << 16, (1ull << 31) | (20ull << 16)), 90, "Intel TjMax comes from hardware");
    Check(!IntelTemperature(100ull << 16, 35ull << 16), "Intel invalid DTS flag rejected");
    Check(!IntelTemperature(0, 1ull << 31) && !IntelTemperature(255ull << 16, 1ull << 31), "Intel invalid TjMax rejected");
    Check(!IntelTemperature(50ull << 16, (1ull << 31) | (127ull << 16)), "Intel implausible temperature rejected");

    Near(AmdLegacyTemperature(0x37000000), 55, "K10/FX/APU Tctl eighth-degree conversion");
    Near(AmdLegacyTemperature(0x68030000), 55, "legacy TJ_SEL applies minus 49 C");
    Near(AmdLegacyTemperature(0x37080000), 55, "legacy bit 19 is not Zen RANGE_SEL");
    Check(!AmdLegacyTemperature(0) && !AmdLegacyTemperature(UINT32_MAX), "legacy missing PCI controller rejected");
    Near(AmdK8Temperature(104u << 16, 0x40), 55, "K8 THERMTRIP conversion");
    Near(AmdK8Temperature(104u << 16, 0x6b), 76, "K8 AM2 65 nm correction");
    for (const auto model : {0x6cu, 0x7cu, 0xc1u})
        Near(AmdK8Temperature(104u << 16, model), 55, "K8 model exclusions from 21 C correction");
    Check(!AmdK8Temperature(0, 0x40) && !AmdK8Temperature(UINT32_MAX, 0x40) &&
        !AmdK8Temperature(250u << 16, 0x40), "K8 missing device and impossible temperature rejected");

    Near(AmdTemperature(0x37000000, 0), 55, "55 C regular range");
    Near(AmdTemperature(0x37200000, 0), 55.125, "eighth-degree temperature resolution");
    Near(AmdTemperature(0x68080000, 0), 55, "RANGE_SEL subtracts 49 C");
    Near(AmdTemperature(0x68030000, 0), 55, "TJ_SEL selects adjusted range on newer Zen");
    Near(AmdTemperature(0x680b0000, 0), 55, "both range flags subtract only once");
    Near(AmdTemperature(0x37010000, 0), 55, "TJ_SEL 1 does not select adjusted range");
    Near(AmdTemperature(0x37020000, 0), 55, "TJ_SEL 2 does not select adjusted range");
    Near(AmdTemperature(0x27080000, 0), -10, "valid negative temperature");
    Check(!AmdTemperature(0, 0) && !AmdTemperature(UINT32_MAX, 0), "absent SMN device does not report a fake temperature");
    Check(!AmdTemperature(0xffe00000, 0) && !AmdTemperature(1u << 19, 0), "implausible temperature rejected");
    Near(AmdTemperature(0x37000000, AmdTctlOffset(0x17, "AMD Ryzen 5 1600X Six-Core Processor")), 35, "Ryzen 1600X Tdie");
    Near(AmdTemperature(0x37000000, AmdTctlOffset(0x17, "AMD Ryzen 7 1700X Eight-Core Processor")), 35, "Ryzen 1700X Tdie");
    Near(AmdTemperature(0x37000000, AmdTctlOffset(0x17, "AMD Ryzen 7 1800X Eight-Core Processor")), 35, "Ryzen 1800X Tdie");
    Near(AmdTemperature(0x37000000, AmdTctlOffset(0x17, "AMD Ryzen 7 2700X Eight-Core Processor")), 45, "Ryzen 2700X Tdie");
    Near(AmdTemperature(0x68080000, AmdTctlOffset(0x17, "AMD Ryzen Threadripper 1950X 16-Core Processor")), 28, "Threadripper 1950X combines both corrections");
    Near(AmdTemperature(0x37000000, AmdTctlOffset(0x17, "AMD Ryzen Threadripper 2990WX 32-Core Processor")), 28, "Threadripper 2990WX Tdie");
    Check(AmdTctlOffset(0x17, "AMD Ryzen 7 1700 Eight-Core Processor") == 0, "non-X model has no correction");
    Check(AmdTctlOffset(0x17, "AMD Ryzen Threadripper 3970X") == 0, "later Threadripper has no legacy correction");
    Check(AmdTctlOffset(0x1a, "AMD Ryzen AI 9 HX 370 w/ Radeon 890M") == 0, "Ryzen AI has no legacy correction");
    Check(AmdTctlOffset(0x19, "AMD Ryzen 7 2700X") == 0 && AmdTctlOffset(0x17, "") == 0, "correction requires known family and name");

    const double amdUnit = EnergyUnitJoules(0xa1003);
    Near(amdUnit, 1.0 / 65536, "AMD ESU 16 uses joules");
    Near(EnergyUnitJoules(0xa0e03), 1.0 / 16384, "Intel ESU 14 unaffected");
    Near(PackagePower(1000, 2950120, amdUnit, 1), 45, "45 joules in one second");
    Near(PackagePower(1000, 2950120, amdUnit, 0.5), 90, "actual sampling interval used");
    Near(PackagePower(0xffff0000, 0x00010000, amdUnit, 1), 2, "32-bit energy counter wrap");
    Near(PackagePower(UINT32_MAX, 0, amdUnit, 1), amdUnit, "wrap includes last increment");
    Near(PackagePower(300, 300, amdUnit, 1), 0, "valid zero energy delta");
    Near(PackagePower(100, 737380, EnergyUnitJoules(0xa0e03), 1), 45, "Intel package power regression");
    Check(!PackagePower(0, 100, amdUnit, 0) && !PackagePower(0, 100, amdUnit, 0.049), "unprimed or too short interval rejected");
    Check(!PackagePower(0, 100, amdUnit, 10) && !PackagePower(0, 100, amdUnit, -1), "resume gap or backwards clock rejected");
    Check(!PackagePower(1000000, 0, amdUnit, 1), "counter reset producing impossible power rejected");
    Check(!PackagePower(0, 1, 0, 1) && !PackagePower(0, 1, -1, 1), "invalid energy units rejected");
    Check(!PackagePower(0, 100, amdUnit, std::numeric_limits<double>::quiet_NaN()), "NaN interval rejected");
    Check(!PackagePower(0, 100, std::numeric_limits<double>::infinity(), 1), "infinite energy unit rejected");
    Near(EnergyMeterWatts(45000), 45, "Windows Energy Meter milliwatts to watts");
    Near(EnergyMeterWatts(0), 0, "valid zero meter sample preserved");
    Check(!EnergyMeterWatts(-1) && !EnergyMeterWatts(2000000), "invalid meter power rejected");
    Check(!EnergyMeterWatts(std::numeric_limits<double>::quiet_NaN()) &&
        !EnergyMeterWatts(std::numeric_limits<double>::infinity()), "non-finite meter power rejected");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
