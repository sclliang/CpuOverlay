#include "pawn_sensors.h"
#include "sensor_math.h"
#include <shlobj.h>
#include <sddl.h>
#include <intrin.h>
#include <filesystem>
#include <vector>
#include <cstring>

namespace {
// Only sensor read calls are used. SMN reads select a PCI index inside the module;
// no voltage, power-limit or other hardware settings are changed.
constexpr uint32_t kThermalTarget = 0x1a2, kPackageThermal = 0x1b1, kCoreThermal = 0x19c;
constexpr uint32_t kRaplUnit = 0x606, kPackageEnergy = 0x611;
constexpr uint32_t kAmdThermal = 0x59800, kAmdPowerUnit = 0xc0010299, kAmdPackageEnergy = 0xc001029b;
class CpuAffinity {
public:
    CpuAffinity() {
        GROUP_AFFINITY target{};
        target.Group = 0; target.Mask = 1;
        applied_ = SetThreadGroupAffinity(GetCurrentThread(), &target, &previous_) != FALSE;
        if (!applied_) error_ = HRESULT_FROM_WIN32(GetLastError());
    }
    ~CpuAffinity() { if (applied_) SetThreadGroupAffinity(GetCurrentThread(), &previous_, nullptr); }
    bool Applied() const { return applied_; }
    HRESULT Error() const { return error_; }
private:
    GROUP_AFFINITY previous_{};
    bool applied_ = false;
    HRESULT error_ = S_OK;
};
class PciBusLock {
public:
    explicit PciBusLock(HANDLE mutex) : mutex_(mutex) {
        const DWORD result = WaitForSingleObject(mutex_, 10);
        owned_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        if (!owned_) error_ = HRESULT_FROM_WIN32(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    }
    ~PciBusLock() { if (owned_) ReleaseMutex(mutex_); }
    bool Owned() const { return owned_; }
    HRESULT Error() const { return error_; }
private:
    HANDLE mutex_;
    bool owned_ = false;
    HRESULT error_ = S_OK;
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
struct SensorModule { const wchar_t* filename; WORD resource; const char* source; };
SensorModule ModuleForCpu(bool amd, uint32_t family) {
    if (!amd) return {L"IntelMSR.bin", 201, "pawnio_intel_msr"};
    switch (SensorMath::SelectAmdBackend(family)) {
    case SensorMath::AmdBackend::K8: return {L"AMDFamily0F.bin", 203, "pawnio_amd_family0f"};
    case SensorMath::AmdBackend::Family10: return {L"AMDFamily10.bin", 204, "pawnio_amd_family10"};
    case SensorMath::AmdBackend::Zen: return {L"AMDFamily17.bin", 202, "pawnio_amd_family17"};
    default: return {L"", 0, "unavailable"};
    }
}
std::filesystem::path ModulePath(const wchar_t* filename) {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) return {};
    return std::filesystem::path(path).parent_path() / L"bin" / L"pawnio" / filename;
}
}

PawnSensors::PawnSensors() {
    QueryPerformanceFrequency(&clockFrequency_);
    int data[4]{};
    __cpuid(data, 0);
    intel_ = data[1] == 0x756e6547 && data[3] == 0x49656e69 && data[2] == 0x6c65746e;
    amd_ = data[1] == 0x68747541 && data[3] == 0x69746e65 && data[2] == 0x444d4163;
    const int highest = data[0];
    if (highest >= 1) {
        __cpuid(data, 1);
        const auto signature = SensorMath::DecodeSignature(static_cast<uint32_t>(data[0]));
        cpuFamily_ = signature.family; cpuModel_ = signature.model;
    }
    if (intel_ && highest >= 6) { __cpuidex(data, 6, 0); packageThermal_ = (data[0] & (1 << 6)) != 0; }
    if (amd_) {
        char brand[49]{};
        __cpuid(data, static_cast<int>(0x80000000u));
        const auto extendedMax = static_cast<uint32_t>(data[0]);
        if (extendedMax >= 0x80000004u) {
            for (uint32_t i = 0; i < 3; ++i) {
                __cpuid(data, static_cast<int>(0x80000002u + i));
                std::memcpy(brand + i * 16, data, 16);
            }
        }
        amdTctlOffsetC_ = SensorMath::AmdTctlOffset(cpuFamily_, brand);
        if (cpuFamily_ == 0x0f && extendedMax >= 0x80000007u) {
            __cpuid(data, static_cast<int>(0x80000007u));
            k8Thermal_ = (data[3] & 1) != 0;
            if (extendedMax >= 0x80000008u) {
                __cpuid(data, static_cast<int>(0x80000008u));
                k8CoreCount_ = (data[2] & 0xff) == 0 ? 1 : 2;
            }
        }
    }
    Open();
}
PawnSensors::~PawnSensors() {
    if (handle_ && close_) close_(handle_);
    if (pciMutex_) CloseHandle(pciMutex_);
    if (library_) FreeLibrary(library_);
}

bool PawnSensors::Open() {
    if (handle_) return true;
    if (!intel_ && !amd_) { status_ = "unsupported_cpu_vendor"; error_ = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); return false; }
    if (amd_ && !SensorMath::SupportsAmdFamily(cpuFamily_)) {
        status_ = "unsupported_amd_family"; error_ = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); return false;
    }
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
    const auto selected = ModuleForCpu(amd_, cpuFamily_);
    const auto path = ModulePath(selected.filename);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::vector<UCHAR> module;
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD openError = GetLastError();
        if (openError != ERROR_FILE_NOT_FOUND && openError != ERROR_PATH_NOT_FOUND) {
            status_ = "pawnio_module_open_failed"; error_ = HRESULT_FROM_WIN32(openError); return false;
        }
        // Keep the identical signed module in the EXE for logon contexts where the
        // external module is unavailable. An external module always takes priority.
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(selected.resource), RT_RCDATA);
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

bool PawnSensors::ExecuteRead(const char* function, const ULONG64* input, SIZE_T size, uint64_t& value) {
    ULONG64 output = 0;
    SIZE_T count = 0;
    const HRESULT result = execute_(handle_, function, input, size, &output, 1, &count);
    if (FAILED(result) || count != 1) { error_ = FAILED(result) ? result : E_FAIL; return false; }
    value = output;
    return true;
}

bool PawnSensors::ReadRegister(const char* function, uint32_t index, uint64_t& value) {
    const ULONG64 input = index;
    return ExecuteRead(function, &input, 1, value);
}

bool PawnSensors::ReadMsr(uint32_t index, uint64_t& value) {
    return ReadRegister("ioctl_read_msr", index, value);
}

bool PawnSensors::ReadPci(const char* function, const ULONG64* input, SIZE_T size, uint32_t& value) {
    // Hold the shared mutex across every PCI selector/read transaction, including
    // K8 core selection and legacy SMU. Coordinate with other hardware monitors.
    if (!pciMutex_) {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100001;;;WD)",
            SDDL_REVISION_1, &descriptor, nullptr)) {
            error_ = HRESULT_FROM_WIN32(GetLastError()); return false;
        }
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
        pciMutex_ = CreateMutexExW(&attributes, L"Global\\Access_PCI", 0, SYNCHRONIZE | MUTEX_MODIFY_STATE);
        const DWORD result = pciMutex_ ? ERROR_SUCCESS : GetLastError();
        LocalFree(descriptor);
        if (!pciMutex_) { error_ = HRESULT_FROM_WIN32(result); return false; }
    }
    PciBusLock lock(pciMutex_);
    if (!lock.Owned()) { error_ = lock.Error(); return false; }
    uint64_t raw = 0;
    if (!ExecuteRead(function, input, size, raw)) return false;
    value = static_cast<uint32_t>(raw);
    return true;
}

void PawnSensors::ReadAmdTemperature(PawnSample& sample) {
    switch (SensorMath::SelectAmdBackend(cpuFamily_)) {
    case SensorMath::AmdBackend::K8:
        sample.temperatureSource = "amd_k8_core_max";
        if (!k8Thermal_) { error_ = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); return; }
        for (uint32_t core = 0; core < k8CoreCount_; ++core) {
            const ULONG64 input[]{0, core}; // First physical package, up to two K8 cores.
            uint32_t raw = 0;
            if (!ReadPci("ioctl_get_thermtrip", input, ARRAYSIZE(input), raw)) continue;
            const auto temperature = SensorMath::AmdK8Temperature(raw, cpuModel_);
            if (temperature && (!sample.temperatureC || *temperature > *sample.temperatureC)) {
                sample.temperatureC = temperature; sample.thermalLegacyRaw = raw; sample.thermalCoreIndex = core;
            }
        }
        break;
    case SensorMath::AmdBackend::Family10: {
        sample.temperatureSource = "amd_legacy_tctl";
        const bool smu = SensorMath::AmdUsesSmuTemperature(cpuFamily_, cpuModel_);
        const ULONG64 input[]{smu ? 0xd8200ca4ull : 0, 0xa4};
        if (ReadPci(smu ? "ioctl_read_smu" : "ioctl_read_miscctl", input, smu ? 1 : 2, sample.thermalLegacyRaw))
            sample.temperatureC = SensorMath::AmdLegacyTemperature(sample.thermalLegacyRaw);
        // Never invoke frequency calibration or C-state IO in AMDFamily10.
        break;
    }
    case SensorMath::AmdBackend::Zen: {
        sample.temperatureSource = amdTctlOffsetC_ > 0 ? "amd_tdie" : "amd_tctl_tdie";
        const ULONG64 input = kAmdThermal;
        if (ReadPci("ioctl_read_smn", &input, 1, sample.thermalSmnRaw))
            sample.temperatureC = SensorMath::AmdTemperature(sample.thermalSmnRaw, amdTctlOffsetC_);
        break;
    }
    default: error_ = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); break;
    }
    if (!sample.temperatureC && SUCCEEDED(error_)) error_ = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

void PawnSensors::Prime() {
    haveEnergy_ = false;
    if (!Open()) return;
    if (amd_ && SensorMath::SelectAmdBackend(cpuFamily_) != SensorMath::AmdBackend::Zen) return;
    CpuAffinity affinity;
    if (!affinity.Applied()) { status_ = "cpu_affinity_failed"; error_ = affinity.Error(); return; }
    uint64_t energy = 0;
    if (ReadMsr(amd_ ? kAmdPackageEnergy : kPackageEnergy, energy)) {
        energy_ = static_cast<uint32_t>(energy);
        QueryPerformanceCounter(&stamp_);
        haveEnergy_ = true;
    }
}

PawnSample PawnSensors::Collect() {
    PawnSample sample;
    sample.status = status_; sample.error = error_;
    sample.moduleSource = moduleSource_;
    sample.cpuVendor = intel_ ? "GenuineIntel" : amd_ ? "AuthenticAMD" : "unknown";
    sample.cpuFamily = cpuFamily_; sample.cpuModel = cpuModel_;
    sample.amdTctlOffsetC = amdTctlOffsetC_;
    const bool amdEnergy = amd_ && SensorMath::SelectAmdBackend(cpuFamily_) == SensorMath::AmdBackend::Zen;
    if (intel_ || amd_) sample.sensorSource = ModuleForCpu(amd_, cpuFamily_).source;
    sample.powerSource = intel_ ? "intel_rapl" : amdEnergy ? "amd_package_energy" : "unavailable";
    if (!handle_) return sample;
    CpuAffinity affinity;
    if (!affinity.Applied()) {
        haveEnergy_ = false; sample.status = "cpu_affinity_failed"; sample.error = affinity.Error(); return sample;
    }
    error_ = S_OK;
    uint64_t target = 0, thermal = 0;
    if (amd_) {
        ReadAmdTemperature(sample);
    } else if (ReadMsr(kThermalTarget, target)) {
        const double tjMax = static_cast<double>((target >> 16) & 0xff);
        sample.tjMaxC = tjMax;
        bool valid = packageThermal_ && ReadMsr(kPackageThermal, thermal) && SensorMath::IntelTemperature(target, thermal).has_value();
        sample.temperatureSource = "intel_package_dts";
        if (!valid) {
            valid = ReadMsr(kCoreThermal, thermal) && SensorMath::IntelTemperature(target, thermal).has_value();
            sample.temperatureSource = "intel_core_dts";
        }
        sample.thermalRaw = thermal;
        if (valid) sample.temperatureC = SensorMath::IntelTemperature(target, thermal);
        else if (SUCCEEDED(error_)) error_ = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    uint64_t unit = 0, energy = 0;
    // A failed/contended temperature read must not suppress package power.
    if ((intel_ || amdEnergy) && ReadMsr(amdEnergy ? kAmdPowerUnit : kRaplUnit, unit) && ReadMsr(amdEnergy ? kAmdPackageEnergy : kPackageEnergy, energy)) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        sample.energyRaw = static_cast<uint32_t>(energy);
        sample.energyUnitRaw = unit;
        sample.energyUnitJoules = SensorMath::EnergyUnitJoules(unit);
        if (haveEnergy_ && clockFrequency_.QuadPart > 0) {
            sample.intervalSeconds = static_cast<double>(now.QuadPart - stamp_.QuadPart) / static_cast<double>(clockFrequency_.QuadPart);
            sample.packageWatts = SensorMath::PackagePower(energy_, sample.energyRaw, sample.energyUnitJoules, sample.intervalSeconds);
        }
        energy_ = sample.energyRaw; stamp_ = now; haveEnergy_ = true;
    } else haveEnergy_ = false;
    sample.status = sample.temperatureC && sample.packageWatts ? "ready" : "partial_sensor_data";
    sample.error = sample.temperatureC && sample.packageWatts ? S_OK : error_;
    return sample;
}
