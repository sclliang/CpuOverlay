#include "gpu_sensor_selection.h"
#include <iostream>
#include <limits>

int main() {
    unsigned failures = 0, checks = 0;
    auto check = [&](bool ok, const char* label) { ++checks; if (!ok) { ++failures; std::cerr << label << '\n'; } };
    using GpuSensors::Priority;
    check(Priority(5, 4, 52, L"GPU Core", 4) > Priority(5, 4, 80, L"GPU Hot Spot", 4), "core temperature wins over hotspot");
    check(Priority(5, 4, 65, L"GPU Memory Junction", 4) < 0, "memory junction is not labeled core temperature");
    check(Priority(5, 3, 2400, L"GPU Core", 3) > 0 && Priority(5, 3, 2000, L"GPU Memory", 3) < 0, "core and memory clocks remain distinct");
    check(Priority(5, 2, 140, L"GPU Package", 2) > Priority(5, 2, 110, L"GPU Core", 2), "package power wins over a partial core rail");
    check(Priority(5, 2, 0, L"GPU Package", 2) >= 0, "a genuine zero power value remains valid");
    check(Priority(5, 2, -1, L"GPU Package", 2) < 0 && Priority(5, 4, 151, L"GPU Core", 4) < 0, "invalid sentinel and temperature range rejected");
    check(Priority(2, 4, 52, L"CPU Core", 4) < 0, "CPU telemetry never becomes GPU telemetry");
    check(Priority(6, 3, 1000, L"GPU Core", 3) >= 0 && Priority(4, 4, 40, L"GPU Core", 4) >= 0, "Intel and NVIDIA use the same validated sensor selection");
    check(Priority(5, 5, 101, L"GPU Core", 5) < 0 && Priority(5, 5, 0, L"GPU Core", 5) >= 0, "load accepts zero but rejects out-of-range values");
    check(Priority(5, 4, std::numeric_limits<double>::quiet_NaN(), L"GPU Core", 4) < 0, "nonfinite sensor values rejected");
    check(Priority(5, 3, 2400, L"GPU Core", 4) < 0, "unrelated sensor types rejected");
    std::cout << checks << " GPU checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
