#pragma once
#include <cmath>
#include <cwctype>
#include <string_view>

namespace GpuSensors {
inline bool Contains(std::wstring_view value, std::wstring_view needle) {
    if (needle.empty() || needle.size() > value.size()) return false;
    for (size_t i = 0; i + needle.size() <= value.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() && std::towlower(value[i + j]) == std::towlower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}
inline int Priority(int hardware, int type, double value, std::wstring_view name, int wantedType) {
    if (hardware < 4 || hardware > 6 || type != wantedType || !std::isfinite(value)) return -1;
    if (type == 4) {
        if (value < 1 || value > 150) return -1;
        // Prefer core/package sensors. Memory and VR temperatures are not GPU core temperature.
        if (name.size() == 8 && Contains(name, L"GPU Core")) return 500;
        if (Contains(name, L"GPU Package")) return 450;
        if (Contains(name, L"Hot Spot")) return 400;
        if (Contains(name, L"Edge")) return 350;
        return -1;
    }
    if (type == 3) {
        if (value <= 0 || value > 100000) return -1;
        if (Contains(name, L"Memory") || Contains(name, L"SoC")) return -1;
        return Contains(name, L"Core") ? 500 : Contains(name, L"Graphics") ? 400 : -1;
    }
    if (type == 2) {
        if (value < 0 || value > 5000) return -1;
        if (Contains(name, L"GPU Package")) return 500;
        if (Contains(name, L"GPU Board")) return 450;
        if (Contains(name, L"GPU Total")) return 400;
        return name.size() == 8 && Contains(name, L"GPU Core") ? 350 : -1;
    }
    if (type == 5 && value >= 0 && value <= 100) {
        if (Contains(name, L"GPU Core")) return 500;
        if (Contains(name, L"3D")) return 400;
    }
    return -1;
}
}
