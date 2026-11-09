#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace GameOverlayDeployment {
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit File(HANDLE value = INVALID_HANDLE_VALUE) : handle(value) {}
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};
inline bool IdenticalImage(const std::filesystem::path& path, const void* data, DWORD bytes) {
    File input(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (input.handle == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(input.handle, &size) || size.QuadPart != bytes) return false;
    std::array<BYTE, 16384> buffer{}; DWORD offset = 0;
    while (offset < bytes) {
        const DWORD count = std::min<DWORD>(static_cast<DWORD>(buffer.size()), bytes - offset); DWORD read = 0;
        if (!ReadFile(input.handle, buffer.data(), count, &read, nullptr) || read != count ||
            memcmp(buffer.data(), static_cast<const BYTE*>(data) + offset, count) != 0) return false;
        offset += count;
    }
    return true;
}
// Publish a different version under a different filename. Repairing an already
// installed, loaded image with identical bytes requires no write access at all.
inline DWORD InstallImage(const std::filesystem::path& path, const void* data, DWORD bytes) {
    if (!data || !bytes) return ERROR_INVALID_DATA;
    if (IdenticalImage(path, data, bytes)) return ERROR_SUCCESS;
    const auto temporary = std::filesystem::path(path.wstring() + L".new." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()));
    {
        File output(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (output.handle == INVALID_HANDLE_VALUE) return GetLastError();
        DWORD written = 0;
        if (!WriteFile(output.handle, data, bytes, &written, nullptr) || written != bytes || !FlushFileBuffers(output.handle)) {
            const DWORD error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
            CloseHandle(output.handle); output.handle = INVALID_HANDLE_VALUE; DeleteFileW(temporary.c_str()); return error;
        }
    }
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return ERROR_SUCCESS;
    const DWORD error = GetLastError(); DeleteFileW(temporary.c_str()); return error;
}
inline bool ModuleName(const std::wstring& name) {
    if (_wcsicmp(name.c_str(), L"CpuOverlayGame64.dll") == 0) return true;
    constexpr wchar_t prefix[] = L"CpuOverlayGame64-"; constexpr size_t start = ARRAYSIZE(prefix) - 1;
    if (name.size() <= start + 4 || _wcsnicmp(name.c_str(), prefix, start) != 0 || _wcsicmp(name.c_str() + name.size() - 4, L".dll") != 0) return false;
    unsigned dots = 0, digits = 0;
    for (size_t i = start; i < name.size() - 4; ++i) {
        if (name[i] >= L'0' && name[i] <= L'9') { ++digits; continue; }
        if (name[i] != L'.' || !digits || ++dots > 3) return false;
        digits = 0;
    }
    return dots == 3 && digits != 0;
}
inline bool OwnedModule(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) return false;
    DWORD unused = 0, bytes = GetFileVersionInfoSizeW(path.c_str(), &unused);
    if (!bytes || bytes > 1024 * 1024) return false;
    std::vector<BYTE> data(bytes);
    if (!GetFileVersionInfoW(path.c_str(), 0, bytes, data.data())) return false;
    VS_FIXEDFILEINFO* fixed = nullptr; UINT size = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &size) || size < sizeof(*fixed) || fixed->dwFileType != VFT_DLL) return false;
    auto matches = [&](const wchar_t* key, const wchar_t* expected) {
        wchar_t* value = nullptr; UINT count = 0;
        return VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &count) && count && value && wcscmp(value, expected) == 0;
    };
    return matches(L"\\StringFileInfo\\040904B0\\ProductName", L"CpuOverlay") &&
        matches(L"\\StringFileInfo\\040904B0\\CompanyName", L"sixunited");
}
// Never touch unrelated files. A loaded retired module is kept during upgrade;
// uninstall can request delayed deletion without terminating its host process.
inline std::vector<std::wstring> RetireModules(const std::filesystem::path& folder, const std::wstring& keep, bool deferLocked = false) {
    std::vector<std::wstring> retained; std::error_code error;
    std::filesystem::directory_iterator it(folder, error), end;
    while (!error && it != end) {
        const auto path = it->path(); const auto name = path.filename().wstring(); it.increment(error);
        if (!ModuleName(name) || _wcsicmp(name.c_str(), keep.c_str()) == 0 || !OwnedModule(path)) continue;
        if (DeleteFileW(path.c_str())) continue;
        const DWORD reason = GetLastError();
        if (deferLocked && (reason == ERROR_ACCESS_DENIED || reason == ERROR_SHARING_VIOLATION)) {
            // Give a delayed deletion its own name so reinstalling the same
            // version before reboot cannot delete the newly installed module.
            const auto retired = std::filesystem::path(path.wstring() + L".retired." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()));
            if (MoveFileExW(path.c_str(), retired.c_str(), 0)) MoveFileExW(retired.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
        retained.push_back(name);
    }
    return retained;
}
}
