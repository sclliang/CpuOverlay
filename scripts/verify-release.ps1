param([string]$ExecutablePath, [string]$ArtifactDirectory, [switch]$SkipHardware)
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exePath = if ($ExecutablePath) { $ExecutablePath } else { Join-Path $projectRoot 'package\CpuOverlay.exe' }
$artifactPath = if ($ArtifactDirectory) { $ArtifactDirectory } else { Join-Path $projectRoot 'artifacts' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $SkipHardware -and -not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run verification in an elevated PowerShell process to read PawnIO sensors.'
}
New-Item -ItemType Directory -Path $artifactPath -Force | Out-Null
function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
$version = (Get-Item -LiteralPath $exePath).VersionInfo
$fields = @{
    CompanyName = 'sixunited'
    ProductName = 'CpuOverlay'
    FileDescription = 'System Performance, Battery and Cooling Display Monitor'
    FileVersion = '1.6.0.0'
    ProductVersion = '1.6.0.0'
    OriginalFilename = 'CpuOverlay.exe'
    InternalName = 'CpuOverlay'
    LegalCopyright = 'Copyright (c) 2026 sixunited. Author: sclliang'
}
foreach ($field in $fields.Keys) { Assert-Condition ($version.$field -eq $fields[$field]) "Incorrect VERSIONINFO: $field" }
$numericFileVersion = "$($version.FileMajorPart).$($version.FileMinorPart).$($version.FileBuildPart).$($version.FilePrivatePart)"
$numericProductVersion = "$($version.ProductMajorPart).$($version.ProductMinorPart).$($version.ProductBuildPart).$($version.ProductPrivatePart)"
Assert-Condition ($numericFileVersion -eq $fields.FileVersion) 'Numeric file version mismatch.'
Assert-Condition ($numericProductVersion -eq $fields.ProductVersion) 'Numeric product version mismatch.'
# Verify numeric translation IDs below; Language is localized by the host OS.
$setupPath = Join-Path $projectRoot 'package\CpuOverlay-Setup.exe'
$setupVersion = (Get-Item -LiteralPath $setupPath).VersionInfo
$setupFields = $fields.Clone()
$setupFields.FileDescription = 'CpuOverlay Installer with PawnIO Driver Setup'
$setupFields.OriginalFilename = 'CpuOverlay-Setup.exe'
$setupFields.InternalName = 'CpuOverlay-Setup'
foreach ($field in $setupFields.Keys) { Assert-Condition ($setupVersion.$field -eq $setupFields[$field]) "Incorrect installer VERSIONINFO: $field" }
Assert-Condition ($setupVersion.Language -eq $version.Language) 'Installer resource language mismatch.'
Assert-Condition ("$($setupVersion.FileMajorPart).$($setupVersion.FileMinorPart).$($setupVersion.FileBuildPart).$($setupVersion.FilePrivatePart)" -eq $fields.FileVersion) 'Installer numeric file version mismatch.'
Assert-Condition ("$($setupVersion.ProductMajorPart).$($setupVersion.ProductMinorPart).$($setupVersion.ProductBuildPart).$($setupVersion.ProductPrivatePart)" -eq $fields.ProductVersion) 'Installer numeric product version mismatch.'
$modules = @(
    @{ Name = 'IntelMSR'; Hash = 'D6ED85D65AB17A22F813EF98207D6D537155EE2DED5976A21CB48413C9B92E5F'; MainId = 201; SetupId = 102; SourceId = 104 },
    @{ Name = 'AMDFamily17'; Hash = 'DAE74615761B78BDF064DFB3E136252DDCC6FC727D88F14738D0E5800D427A91'; MainId = 202; SetupId = 108; SourceId = 109 },
    @{ Name = 'AMDFamily0F'; Hash = 'A6E11619E87A97820705A6523714F22D676CE44F902631833D4429B89D509D55'; MainId = 203; SetupId = 110; SourceId = 111 },
    @{ Name = 'AMDFamily10'; Hash = '6443080B2968474FFBC38AA4356CC56F9664349FA4B917AFDB33027D6BB50525'; MainId = 204; SetupId = 112; SourceId = 113 }
)
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
public static class InstallerPayload {
    [DllImport("version", CharSet=CharSet.Unicode, SetLastError=true)] static extern uint GetFileVersionInfoSize(string path, out uint handle);
    [DllImport("version", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool GetFileVersionInfo(string path, uint handle, uint size, byte[] data);
    [DllImport("version", CharSet=CharSet.Unicode)] static extern bool VerQueryValue(IntPtr data, string subBlock, out IntPtr value, out uint length);
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32")] static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32")] static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32")] static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32")] static extern bool FreeLibrary(IntPtr module);
    public static int LanguageId(string path) {
        uint unused;
        uint size = GetFileVersionInfoSize(path, out unused);
        if (size == 0) throw new Win32Exception();
        var data = new byte[size];
        if (!GetFileVersionInfo(path, 0, size, data)) throw new Win32Exception();
        var pinned = GCHandle.Alloc(data, GCHandleType.Pinned);
        try {
            IntPtr value;
            uint length;
            if (!VerQueryValue(pinned.AddrOfPinnedObject(), @"\VarFileInfo\Translation", out value, out length) || length < 4) return -1;
            return (ushort)Marshal.ReadInt16(value);
        } finally { pinned.Free(); }
    }
    public static string Author(string path) {
        uint unused;
        uint size = GetFileVersionInfoSize(path, out unused);
        if (size == 0) throw new Win32Exception();
        var data = new byte[size];
        if (!GetFileVersionInfo(path, 0, size, data)) throw new Win32Exception();
        var pinned = GCHandle.Alloc(data, GCHandleType.Pinned);
        try {
            IntPtr value;
            uint length;
            if (!VerQueryValue(pinned.AddrOfPinnedObject(), @"\StringFileInfo\040904B0\Author", out value, out length)) return null;
            return Marshal.PtrToStringUni(value);
        } finally { pinned.Free(); }
    }
    public static string Hash(string path, int id) {
        var module = LoadLibraryEx(path, IntPtr.Zero, 2); // Data file only: never execute the image.
        if (module == IntPtr.Zero) throw new Win32Exception();
        try {
            var resource = FindResource(module, (IntPtr)id, (IntPtr)10);
            if (resource == IntPtr.Zero) throw new Win32Exception();
            var bytes = new byte[SizeofResource(module, resource)];
            Marshal.Copy(LockResource(LoadResource(module, resource)), bytes, 0, bytes.Length);
            using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "");
        } finally { FreeLibrary(module); }
    }
}
'@
Assert-Condition ([InstallerPayload]::LanguageId($exePath) -eq 0x0409) 'Main executable language must be English (United States).'
Assert-Condition ([InstallerPayload]::LanguageId($setupPath) -eq 0x0409) 'Installer language must be English (United States).'
Assert-Condition ([InstallerPayload]::Author($exePath) -eq 'sclliang') 'Main executable author metadata mismatch.'
Assert-Condition ([InstallerPayload]::Author($setupPath) -eq 'sclliang') 'Installer author metadata mismatch.'
Assert-Condition ([InstallerPayload]::Hash($setupPath, 101) -eq (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash) 'Installer contains a stale main executable.'
Assert-Condition ([InstallerPayload]::Hash($setupPath, 106) -eq '1F519A22E47187F70A1379A48CA604981C4FCF694F4E65B734AAA74A9FBA3032') 'Embedded official PawnIO installer hash mismatch.'
Assert-Condition ([InstallerPayload]::Hash($setupPath, 107) -eq (Get-FileHash -LiteralPath (Join-Path $projectRoot 'README.md')).Hash) 'Installer contains stale documentation.'
foreach ($document in @(@{Name = 'COPYING'; Id = 103}, @{Name = 'README.md'; Id = 105})) {
    $documentHash = (Get-FileHash -LiteralPath (Join-Path $projectRoot "third_party\pawnio\$($document.Name)")).Hash
    Assert-Condition ([InstallerPayload]::Hash($setupPath, $document.Id) -eq $documentHash) 'Installer module documentation/license mismatch.'
    Assert-Condition ((Get-FileHash -LiteralPath (Join-Path (Split-Path $exePath) "bin\pawnio\$($document.Name)")).Hash -eq $documentHash) 'Portable module documentation/license mismatch.'
}
foreach ($module in $modules) {
    $modulePath = Join-Path (Split-Path $exePath) "bin\pawnio\$($module.Name).bin"
    $sourcePath = Join-Path (Split-Path $exePath) "bin\pawnio\$($module.Name).p"
    Assert-Condition ((Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash -eq $module.Hash) "$($module.Name) signed module hash mismatch."
    Assert-Condition ([InstallerPayload]::Hash($exePath, $module.MainId) -eq $module.Hash) "$($module.Name) embedded fallback mismatch."
    Assert-Condition ([InstallerPayload]::Hash($setupPath, $module.SetupId) -eq $module.Hash) "$($module.Name) installer payload mismatch."
    Assert-Condition ([InstallerPayload]::Hash($setupPath, $module.SourceId) -eq (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash) "$($module.Name) installer source mismatch."
}

$jsonPath = Join-Path $artifactPath 'diagnostics.json'
$pngPath = Join-Path $artifactPath 'preview.png'
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $diagnosticProcess = Start-Process -FilePath $exePath -ArgumentList "--diagnostics `"$jsonPath`"" -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru
    Assert-Condition ($diagnosticProcess.WaitForExit(20000)) 'Diagnostics timed out.'
    Assert-Condition ($diagnosticProcess.ExitCode -eq 0 -or ($SkipHardware -and $diagnosticProcess.ExitCode -eq 3)) "Diagnostics failed: $($diagnosticProcess.ExitCode)"
    $previewProcess = Start-Process -FilePath $exePath -ArgumentList "--preview `"$pngPath`"" -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru
    Assert-Condition ($previewProcess.WaitForExit(10000)) 'Preview timed out.'
    Assert-Condition ($previewProcess.ExitCode -eq 0) 'Preview failed.'
} finally { $env:PATH = $savedPath }

$diagnostics = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
if (-not $SkipHardware) { Assert-Condition $diagnostics.elevated 'Diagnostics did not inherit administrator access.' }
$samples = $diagnostics.samples
Assert-Condition ($samples.Count -eq 4) 'Expected four real-time samples.'
$previous = $null
foreach ($sample in $samples) {
    Assert-Condition ($null -ne $sample.cpu_percent -and $sample.cpu_percent -ge 0 -and $sample.cpu_percent -le 100) 'Invalid CPU usage.'
    Assert-Condition ($null -ne $sample.frequency_ghz -and $sample.frequency_ghz -gt 0 -and $sample.frequency_ghz -lt 20) 'Invalid CPU frequency.'
    if (-not $SkipHardware) {
        $meterFallback = $sample.power_source -eq 'windows_energy_meter_package0'
        Assert-Condition (($sample.pawnio_status -eq 'ready' -and $sample.pawnio_hresult -eq '0x0') -or
            ($meterFallback -and $sample.pawnio_status -eq 'partial_sensor_data')) 'PawnIO sensors unavailable.'
        Assert-Condition ($null -ne $sample.cpu_temperature_c -and $sample.cpu_temperature_c -ge -30 -and $sample.cpu_temperature_c -le 150) 'Invalid CPU temperature.'
        switch ($sample.cpu_sensor_source) {
            'pawnio_intel_msr' {
                Assert-Condition ($sample.cpu_vendor -eq 'GenuineIntel' -and ($meterFallback -or $sample.power_source -eq 'intel_rapl')) 'Intel sensor source mismatch.'
                $temperature = $sample.tj_max_c - (([long]$sample.thermal_msr_raw -shr 16) -band 127)
            }
            'pawnio_amd_family17' {
                Assert-Condition ($sample.cpu_vendor -eq 'AuthenticAMD' -and ($meterFallback -or $sample.power_source -eq 'amd_package_energy')) 'AMD sensor source mismatch.'
                Assert-Condition ($sample.cpu_family -ge 0x17 -and $sample.cpu_family -le 0x1a) 'Unsupported AMD CPU family.'
                $raw = [long]$sample.thermal_smn_raw
                Assert-Condition ($raw -gt 0 -and $raw -lt 4294967295) 'Invalid AMD thermal register.'
                $temperature = ($raw -shr 21) * 0.125 - $sample.amd_tctl_offset_c
                if (($raw -band 0x80000) -ne 0 -or ($raw -band 0x30000) -eq 0x30000) { $temperature -= 49 }
            }
            'pawnio_amd_family0f' {
                Assert-Condition ($sample.cpu_vendor -eq 'AuthenticAMD' -and $sample.cpu_family -eq 0x0f) 'K8 source mismatch.'
                $raw = [long]$sample.thermal_legacy_raw
                Assert-Condition ($raw -gt 0 -and $raw -lt 4294967295 -and $sample.thermal_core_index -le 1) 'Invalid K8 thermal register.'
                $temperature = (($raw -shr 16) -band 255) - 49
                if ($sample.cpu_model -ge 0x69 -and $sample.cpu_model -notin @(0xc1, 0x6c, 0x7c)) { $temperature += 21 }
            }
            'pawnio_amd_family10' {
                Assert-Condition ($sample.cpu_vendor -eq 'AuthenticAMD' -and $sample.cpu_family -in @(0x10, 0x11, 0x12, 0x14, 0x15, 0x16)) 'Legacy AMD source mismatch.'
                $raw = [long]$sample.thermal_legacy_raw
                Assert-Condition ($raw -gt 0 -and $raw -lt 4294967295) 'Invalid legacy AMD thermal register.'
                $temperature = ($raw -shr 21) * 0.125
                if (($raw -band 0x30000) -eq 0x30000) { $temperature -= 49 }
            }
            default { throw "Unexpected sensor source: $($sample.cpu_sensor_source)" }
        }
        Assert-Condition ([math]::Abs($temperature - $sample.cpu_temperature_c) -lt 0.001) 'Thermal register conversion mismatch.'
        Assert-Condition ($null -ne $sample.cpu_package_watts -and $sample.cpu_package_watts -ge 0 -and $sample.cpu_package_watts -lt 2000) 'Invalid package power.'
        if ($meterFallback) {
            Assert-Condition ($null -ne $sample.energy_meter_milliwatts -and
                [math]::Abs($sample.energy_meter_milliwatts / 1000 - $sample.cpu_package_watts) -lt 0.001) 'Windows Energy Meter conversion mismatch.'
        } else {
            Assert-Condition ($sample.power_source -in @('intel_rapl', 'amd_package_energy')) 'Unexpected package power source.'
            $unit = [math]::Pow(0.5, (([long]$sample.energy_unit_raw -shr 8) -band 31))
            Assert-Condition ([math]::Abs($unit - $sample.energy_unit_j) -lt 1e-14) 'Energy unit conversion mismatch.'
            Assert-Condition ($sample.power_interval_s -ge 0.05 -and $sample.power_interval_s -lt 10) 'Invalid power sampling interval.'
            if ($previous -and $previous.power_source -eq $sample.power_source) {
                $delta = ([long]$sample.energy_counter_raw - [long]$previous.energy_counter_raw + 4294967296) % 4294967296
                $power = $delta * $sample.energy_unit_j / $sample.power_interval_s
                Assert-Condition ([math]::Abs($power - $sample.cpu_package_watts) -lt 0.01) 'Energy delta or sampling time mismatch.'
            }
        }
    }
    Assert-Condition ($sample.memory_total_bytes -gt 0 -and $sample.memory_used_bytes -ge 0 -and $sample.memory_used_bytes -le $sample.memory_total_bytes) 'Invalid physical memory counters.'
    $memory = 100.0 * $sample.memory_used_bytes / $sample.memory_total_bytes
    Assert-Condition ($null -ne $sample.memory_percent -and [math]::Abs($sample.memory_percent - $memory) -lt 0.001) 'Memory percentage mismatch.'
    Assert-Condition ($null -ne $sample.gpu_percent -and $sample.gpu_percent -ge 0 -and $sample.gpu_percent -le 100 -and $sample.gpu_engine_count -gt 0) 'Invalid GPU usage.'
    $previous = $sample
}
Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new($pngPath)
try {
    Assert-Condition ($bitmap.Width -gt 5 * $bitmap.Height) 'Preview must be a horizontal line.'
    $transparent = 0; $yellow = 0
    for ($y = 0; $y -lt $bitmap.Height; $y++) {
        for ($x = 0; $x -lt $bitmap.Width; $x++) {
            $pixel = $bitmap.GetPixel($x, $y)
            if ($pixel.A -eq 0) { $transparent++ }
            if ($pixel.A -gt 240 -and $pixel.R -gt 240 -and $pixel.G -gt 240 -and $pixel.B -lt 20) { $yellow++ }
        }
    }
    Assert-Condition ($transparent -gt $bitmap.Width * $bitmap.Height * 0.65) 'Background is not sufficiently transparent.'
    Assert-Condition ($yellow -gt 500) 'Yellow text was not rendered.'
} finally { $bitmap.Dispose() }

$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$toolVersion = Get-ChildItem -LiteralPath (Join-Path $vsPath 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$dumpbin = Join-Path $toolVersion.FullName 'bin\Hostx64\x64\dumpbin.exe'
$dependencies = & $dumpbin /dependents $exePath
Assert-Condition ($LASTEXITCODE -eq 0) 'dumpbin failed.'
$dlls = @($dependencies | ForEach-Object { if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1] } })
Assert-Condition ($dlls.Count -gt 0) 'No import dependencies found.'
$systemDlls = @('USER32.dll','GDI32.dll','gdiplus.dll','SHELL32.dll','ADVAPI32.dll','pdh.dll','POWRPROF.dll','api-ms-win-shcore-scaling-l1-1-1.dll','KERNEL32.dll','ole32.dll','OLEAUT32.dll','COMCTL32.dll','COMDLG32.dll','VERSION.dll','HID.DLL','SETUPAPI.dll')
foreach ($dll in $dlls) { Assert-Condition ($dll -in $systemDlls) "Unexpected runtime dependency: $dll" }
$setupImports = & $dumpbin /dependents $setupPath
Assert-Condition ($LASTEXITCODE -eq 0) 'Installer dumpbin failed.'
$setupDlls = @($setupImports | ForEach-Object { if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1] } })
foreach ($dll in $setupDlls) { Assert-Condition ($dll -in $systemDlls) "Unexpected installer dependency: $dll" }

$report = [ordered]@{
    version = $version.FileVersion
    version_info = 'PASS'
    resource_language = $version.Language
    executable_bytes = (Get-Item -LiteralPath $exePath).Length
    sha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
    clean_path_and_foreign_working_directory = 'PASS'
    os_metrics = 'PASS'
    hardware_sensors_and_raw_math = if ($SkipHardware) { 'SKIPPED: rerun elevated with PawnIO installed' } else { 'PASS' }
    cpu_sensor_source = $samples[0].cpu_sensor_source
    pawnio_status = $samples[0].pawnio_status
    pawnio_module_hash = 'PASS'
    transparent_horizontal_yellow_render = 'PASS'
    dll_dependencies = $dlls
    installer_version_info = 'PASS'
    embedded_main_and_official_pawnio_installer = 'PASS'
    installer_dll_dependencies = $setupDlls
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifactPath 'release-report.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 4
