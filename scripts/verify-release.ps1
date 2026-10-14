$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exePath = Join-Path $projectRoot 'package\CpuOverlay.exe'
$artifactPath = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Path $artifactPath -Force | Out-Null
function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
$version = (Get-Item -LiteralPath $exePath).VersionInfo
$fields = @{
    CompanyName = 'sixunited'
    ProductName = 'CpuOverlay'
    FileDescription = 'CPU Usage and Frequency Desktop Overlay'
    FileVersion = '1.0.0.0'
    ProductVersion = '1.0.0.0'
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

$samples = (Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json).samples
Assert-Condition ($samples.Count -eq 4) 'Expected four real-time samples.'
foreach ($sample in $samples) {
    Assert-Condition ($null -ne $sample.cpu_percent -and $sample.cpu_percent -ge 0 -and $sample.cpu_percent -le 100) 'Invalid CPU usage.'
    Assert-Condition ($null -ne $sample.frequency_ghz -and $sample.frequency_ghz -gt 0 -and $sample.frequency_ghz -lt 20) 'Invalid CPU frequency.'
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
$systemDlls = @('USER32.dll','GDI32.dll','gdiplus.dll','SHELL32.dll','ADVAPI32.dll','pdh.dll','POWRPROF.dll','api-ms-win-shcore-scaling-l1-1-1.dll','KERNEL32.dll')
foreach ($dll in $dlls) { Assert-Condition ($dll -in $systemDlls) "Unexpected runtime dependency: $dll" }

$report = [ordered]@{
    version = $version.FileVersion
    version_info = 'PASS'
    resource_language = $version.Language
    executable_bytes = (Get-Item -LiteralPath $exePath).Length
    sha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
    clean_path_and_foreign_working_directory = 'PASS'
    cpu_sampling = 'PASS'
    transparent_horizontal_yellow_render = 'PASS'
    dll_dependencies = $dlls
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifactPath 'release-report.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 4
