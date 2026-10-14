param([string]$ExecutablePath, [string]$ArtifactDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exePath = if ($ExecutablePath) { $ExecutablePath } else { Join-Path $projectRoot 'package\CpuOverlay.exe' }
$artifactPath = if ($ArtifactDirectory) { $ArtifactDirectory } else { Join-Path $projectRoot 'artifacts' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
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
    FileDescription = 'CPU, Memory and GPU Performance Desktop Overlay'
    FileVersion = '1.1.0.0'
    ProductVersion = '1.1.0.0'
    OriginalFilename = 'CpuOverlay.exe'
    InternalName = 'CpuOverlay'
    LegalCopyright = 'Copyright (c) 2026 sixunited. Author: sclliang'
}
foreach ($field in $fields.Keys) { Assert-Condition ($version.$field -eq $fields[$field]) "Incorrect VERSIONINFO: $field" }
$numericFileVersion = "$($version.FileMajorPart).$($version.FileMinorPart).$($version.FileBuildPart).$($version.FilePrivatePart)"
$numericProductVersion = "$($version.ProductMajorPart).$($version.ProductMinorPart).$($version.ProductBuildPart).$($version.ProductPrivatePart)"
Assert-Condition ($numericFileVersion -eq $fields.FileVersion) 'Numeric file version mismatch.'
Assert-Condition ($numericProductVersion -eq $fields.ProductVersion) 'Numeric product version mismatch.'
# FileVersionInfo translates the language name using the OS display language.
Assert-Condition ($version.Language -in @('English (United States)', '英语(美国)', '英语（美国）')) "Unexpected resource language: $($version.Language)"
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
$modulePath = Join-Path (Split-Path $exePath) 'bin\pawnio\IntelMSR.bin'
Assert-Condition ((Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash -eq 'D6ED85D65AB17A22F813EF98207D6D537155EE2DED5976A21CB48413C9B92E5F') 'Signed PawnIO module hash mismatch.'
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
public static class InstallerPayload {
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32")] static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32")] static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32")] static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32")] static extern bool FreeLibrary(IntPtr module);
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
Assert-Condition ([InstallerPayload]::Hash($setupPath, 101) -eq (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash) 'Installer contains a stale main executable.'
Assert-Condition ([InstallerPayload]::Hash($setupPath, 106) -eq '1F519A22E47187F70A1379A48CA604981C4FCF694F4E65B734AAA74A9FBA3032') 'Embedded official PawnIO installer hash mismatch.'
Assert-Condition ([InstallerPayload]::Hash($exePath, 201) -eq (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash) 'Embedded fallback module differs from official signed module.'

$jsonPath = Join-Path $artifactPath 'diagnostics.json'
$pngPath = Join-Path $artifactPath 'preview.png'
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $diagnosticProcess = Start-Process -FilePath $exePath -ArgumentList "--diagnostics `"$jsonPath`"" -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru
    Assert-Condition ($diagnosticProcess.WaitForExit(20000)) 'Diagnostics timed out.'
    Assert-Condition ($diagnosticProcess.ExitCode -eq 0) "Diagnostics failed: $($diagnosticProcess.ExitCode)"
    $previewProcess = Start-Process -FilePath $exePath -ArgumentList "--preview `"$pngPath`"" -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru
    Assert-Condition ($previewProcess.WaitForExit(10000)) 'Preview timed out.'
    Assert-Condition ($previewProcess.ExitCode -eq 0) 'Preview failed.'
} finally { $env:PATH = $savedPath }

$diagnostics = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
Assert-Condition $diagnostics.elevated 'Diagnostics did not inherit administrator access.'
$samples = $diagnostics.samples
Assert-Condition ($samples.Count -eq 4) 'Expected four real-time samples.'
$previous = $null
foreach ($sample in $samples) {
    Assert-Condition ($null -ne $sample.cpu_percent -and $sample.cpu_percent -ge 0 -and $sample.cpu_percent -le 100) 'Invalid CPU usage.'
    Assert-Condition ($null -ne $sample.frequency_ghz -and $sample.frequency_ghz -gt 0 -and $sample.frequency_ghz -lt 20) 'Invalid CPU frequency.'
    Assert-Condition ($sample.pawnio_status -eq 'ready' -and $sample.pawnio_hresult -eq '0x0') 'PawnIO sensors unavailable.'
    Assert-Condition ($null -ne $sample.cpu_temperature_c -and $sample.cpu_temperature_c -ge -30 -and $sample.cpu_temperature_c -le 150) 'Invalid CPU temperature.'
    $temperature = $sample.tj_max_c - (([long]$sample.thermal_msr_raw -shr 16) -band 127)
    Assert-Condition ([math]::Abs($temperature - $sample.cpu_temperature_c) -lt 0.001) 'Thermal MSR conversion mismatch.'
    Assert-Condition ($null -ne $sample.cpu_package_watts -and $sample.cpu_package_watts -ge 0 -and $sample.cpu_package_watts -lt 2000) 'Invalid package power.'
    if ($previous) {
        $delta = ([long]$sample.energy_counter_raw - [long]$previous.energy_counter_raw + 4294967296) % 4294967296
        $power = $delta * $sample.energy_unit_j / $sample.power_interval_s
        Assert-Condition ([math]::Abs($power - $sample.cpu_package_watts) -lt 0.01) 'RAPL delta or sampling time mismatch.'
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
$systemDlls = @('USER32.dll','GDI32.dll','gdiplus.dll','SHELL32.dll','ADVAPI32.dll','pdh.dll','POWRPROF.dll','api-ms-win-shcore-scaling-l1-1-1.dll','KERNEL32.dll','ole32.dll','OLEAUT32.dll','COMCTL32.dll','VERSION.dll')
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
    six_metrics_and_raw_sensor_math = 'PASS'
    pawnio_module_hash = 'PASS'
    transparent_horizontal_yellow_render = 'PASS'
    dll_dependencies = $dlls
    installer_version_info = 'PASS'
    embedded_main_and_official_pawnio_installer = 'PASS'
    installer_dll_dependencies = $setupDlls
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifactPath 'release-report.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 4
