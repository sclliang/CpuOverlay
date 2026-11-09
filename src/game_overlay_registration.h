#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

// Register only this product's manifest. Never change other Vulkan layer entries.
inline LSTATUS ConfigureGameOverlayLayer(const std::filesystem::path& manifest, bool enabled) {
    constexpr auto settingsKey = L"Software\\sixunited\\CpuOverlay";
    constexpr auto marker = L"GameOverlayLayerPath";
    std::wstring previous;
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, settingsKey, marker, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS && bytes <= 65536) {
        std::vector<wchar_t> text(bytes / sizeof(wchar_t) + 1, 0);
        if (RegGetValueW(HKEY_CURRENT_USER, settingsKey, marker, RRF_RT_REG_SZ, nullptr, text.data(), &bytes) == ERROR_SUCCESS) previous = text.data();
    }
    HKEY key = nullptr;
    auto result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Khronos\\Vulkan\\ImplicitLayers", 0, nullptr, 0,
        KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return result;
    if (enabled) {
        if (!previous.empty() && _wcsicmp(previous.c_str(), manifest.c_str()) != 0 &&
            _wcsicmp(std::filesystem::path(previous).filename().c_str(), L"CpuOverlayGame64.json") == 0) RegDeleteValueW(key, previous.c_str());
        const DWORD value = 0;
        result = RegSetValueExW(key, manifest.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        if (result == ERROR_SUCCESS) RegSetKeyValueW(HKEY_CURRENT_USER, settingsKey, marker, REG_SZ, manifest.c_str(),
            static_cast<DWORD>((manifest.wstring().size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, manifest.c_str()); if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
        const bool ownPrevious = !previous.empty() && _wcsicmp(std::filesystem::path(previous).filename().c_str(), L"CpuOverlayGame64.json") == 0;
        if (ownPrevious) RegDeleteValueW(key, previous.c_str());
        if (ownPrevious) {
            HKEY settings = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, settingsKey, 0, KEY_SET_VALUE, &settings) == ERROR_SUCCESS) {
                RegDeleteValueW(settings, marker); RegCloseKey(settings);
            }
        }
    }
    RegCloseKey(key); return result;
}
