#include "pawn_sensors.h"
#include <shlobj.h>
#include <intrin.h>
#include <filesystem>
#include <vector>
#include <cstring>
#include <cmath>

namespace {
// Only read-only module calls are used. No voltage, power-limit or other register writes.
constexpr uint32_t kThermalTarget = 0x1a2, kPackageThermal = 0x1b1, kCoreThermal = 0x19c;
constexpr uint32_t kRaplUnit = 0x606, kPackageEnergy = 0x611;
class CpuAffinity {
public:
    CpuAffinity() {
        GROUP_AFFINITY target{};
        target.Group = 0; target.Mask = 1;
        applied_ = SetThreadGroupAffinity(GetCurrentThread(), &target, &previous_) != FALSE;
    }
    ~CpuAffinity() { if (applied_) SetThreadGroupAffinity(GetCurrentThread(), &previous_, nullptr); }
    bool Applied() const { return applied_; }
private:
    GROUP_AFFINITY previous_{};
    bool applied_ = false;
};
template<class T> T Resolve(HMODULE library, const char* name) {
    const auto address = GetProcAddress(library, name);
    T function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}
std::filesystem::path InstallationDirectory() {
    wchar_t path[32768]{};
    DWORD bytes = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO",
        L"InstallLocation", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, path, &bytes) == ERROR_SUCCESS)
        return path;
    PWSTR programFiles = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &programFiles))) {
        result = std::filesystem::path(programFiles) / L"PawnIO";
        CoTaskMemFree(programFiles);
    }
    return result;
}
std::filesystem::path ModulePath() {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) return {};
    return std::filesystem::path(path).parent_path() / L"bin" / L"pawnio" / L"IntelMSR.bin";
}
}

PawnSensors::PawnSensors() {
    QueryPerformanceFrequency(&clockFrequency_);
    int data[4]{};
    __cpuid(data, 0);
    intel_ = data[1] == 0x756e6547 && data[3] == 0x49656e69 && data[2] == 0x6c65746e;
    const int highest = data[0];
    if (highest >= 6) { __cpuidex(data, 6, 0); packageThermal_ = (data[0] & (1 << 6)) != 0; }
    Open();
}
PawnSensors::~PawnSensors() {
    if (handle_ && close_) close_(handle_);
    if (library_) FreeLibrary(library_);
}

bool PawnSensors::Open() {
    if (handle_) return true;
    if (!intel_) { status_ = "unsupported_cpu_vendor"; error_ = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); return false; }
    if (!library_) {
        const auto directory = InstallationDirectory();
        if (directory.empty()) { status_ = "pawnio_not_installed"; return false; }
        library_ = LoadLibraryExW((directory / L"PawnIOLib.dll").c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library_) { status_ = "pawnio_library_unavailable"; error_ = HRESULT_FROM_WIN32(GetLastError()); return false; }
    }
    const auto open = Resolve<OpenFn>(library_, "pawnio_open");
    const auto load = Resolve<LoadFn>(library_, "pawnio_load");
    execute_ = Resolve<ExecuteFn>(library_, "pawnio_execute");
    close_ = Resolve<CloseFn>(library_, "pawnio_close");
    if (!open || !load || !execute_ || !close_) { status_ = "pawnio_api_unavailable"; error_ = E_NOINTERFACE; return false; }
    // Preserve the exact Windows error when the external signed module cannot be read.
    const auto path = ModulePath();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::vector<UCHAR> module;
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD openError = GetLastError();
        if (openError != ERROR_FILE_NOT_FOUND && openError != ERROR_PATH_NOT_FOUND) {
            status_ = "pawnio_module_open_failed"; error_ = HRESULT_FROM_WIN32(openError); return false;
        }
        // Keep the identical signed module in the EXE for logon contexts where the
        // external module is unavailable. An external module always takes priority.
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(201), RT_RCDATA);
        const DWORD bytes = resource ? SizeofResource(nullptr, resource) : 0;
        HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
        const auto data = loaded ? static_cast<const UCHAR*>(LockResource(loaded)) : nullptr;
        if (!data || !bytes || bytes > 1024 * 1024) {
            status_ = "pawnio_module_missing"; error_ = HRESULT_FROM_WIN32(openError); return false;
        }
        module.assign(data, data + bytes);
        moduleSource_ = "embedded_signed_module";
    } else {
        LARGE_INTEGER bytes{};
        if (!GetFileSizeEx(file, &bytes) || bytes.QuadPart <= 0 || bytes.QuadPart > 1024 * 1024) {
            CloseHandle(file); status_ = "pawnio_module_invalid"; error_ = E_INVALIDARG; return false;
        }
        module.resize(static_cast<size_t>(bytes.QuadPart));
        DWORD read = 0;
        const bool complete = ReadFile(file, module.data(), static_cast<DWORD>(module.size()), &read, nullptr) && read == module.size();
        const DWORD readError = complete ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (!complete) { status_ = "pawnio_module_read_failed"; error_ = readError ? HRESULT_FROM_WIN32(readError) : E_FAIL; return false; }
        moduleSource_ = "external_signed_module";
    }
    error_ = open(&handle_);
    if (FAILED(error_)) {
        handle_ = nullptr;
        status_ = error_ == E_ACCESSDENIED ? "administrator_required" : "pawnio_open_failed";
        return false;
    }
    error_ = load(handle_, module.data(), module.size());
    if (FAILED(error_)) {
        close_(handle_); handle_ = nullptr;
        status_ = "pawnio_module_load_failed";
        return false;
    }
    status_ = "ready";
    return true;
}

bool PawnSensors::ReadMsr(uint32_t index, uint64_t& value) {
    const ULONG64 input = index;
    ULONG64 output = 0;
    SIZE_T count = 0;
    const HRESULT result = execute_(handle_, "ioctl_read_msr", &input, 1, &output, 1, &count);
    if (FAILED(result) || count != 1) { error_ = FAILED(result) ? result : E_FAIL; return false; }
    value = output;
    return true;
}

void PawnSensors::Prime() {
    haveEnergy_ = false;
    if (!Open()) return;
    CpuAffinity affinity;
    if (!affinity.Applied()) return;
    uint64_t energy = 0;
    if (ReadMsr(kPackageEnergy, energy)) {
        energy_ = static_cast<uint32_t>(energy);
        QueryPerformanceCounter(&stamp_);
        haveEnergy_ = true;
    }
}

PawnSample PawnSensors::Collect() {
    PawnSample sample;
    sample.status = status_; sample.error = error_;
    sample.moduleSource = moduleSource_;
    if (!handle_) return sample;
    CpuAffinity affinity;
    if (!affinity.Applied()) { sample.status = "cpu_affinity_failed"; return sample; }
    error_ = S_OK;
    uint64_t target = 0, thermal = 0;
    if (ReadMsr(kThermalTarget, target)) {
        const double tjMax = static_cast<double>((target >> 16) & 0xff);
        sample.tjMaxC = tjMax;
        bool valid = packageThermal_ && ReadMsr(kPackageThermal, thermal) && (thermal & (1ull << 31));
        if (!valid) valid = ReadMsr(kCoreThermal, thermal) && (thermal & (1ull << 31));
        sample.thermalRaw = thermal;
        if (valid && tjMax >= 50 && tjMax <= 125) {
            const double temperature = tjMax - static_cast<double>((thermal >> 16) & 0x7f);
            if (temperature >= -30 && temperature <= 150) sample.temperatureC = temperature;
        }
    }
    uint64_t unit = 0, energy = 0;
    if (ReadMsr(kRaplUnit, unit) && ReadMsr(kPackageEnergy, energy)) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        sample.energyRaw = static_cast<uint32_t>(energy);
        sample.energyUnitJoules = std::ldexp(1.0, -static_cast<int>((unit >> 8) & 0x1f));
        if (haveEnergy_ && clockFrequency_.QuadPart > 0) {
            sample.intervalSeconds = static_cast<double>(now.QuadPart - stamp_.QuadPart) / static_cast<double>(clockFrequency_.QuadPart);
            // Unsigned subtraction handles the 32-bit RAPL energy counter wrapping.
            const uint32_t delta = sample.energyRaw - energy_;
            if (sample.intervalSeconds >= 0.05 && sample.intervalSeconds < 10) {
                const double watts = static_cast<double>(delta) * sample.energyUnitJoules / sample.intervalSeconds;
                if (std::isfinite(watts) && watts >= 0 && watts < 2000) sample.packageWatts = watts;
            }
        }
        energy_ = sample.energyRaw; stamp_ = now; haveEnergy_ = true;
    } else haveEnergy_ = false;
    sample.error = error_;
    sample.status = sample.temperatureC && sample.packageWatts ? "ready" : "partial_sensor_data";
    return sample;
}
