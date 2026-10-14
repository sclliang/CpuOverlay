param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmakeCommand) { throw 'CMake 3.24+ is required. Install the Visual Studio C++ CMake tools.' }
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Visual Studio with Desktop development with C++ is required.' }
$vsJson = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
$vsInfo = @($vsJson | ConvertFrom-Json)[0]
if (-not $vsInfo) { throw 'No Visual Studio C++ toolchain was found.' }
$vsMajor = ([version]$vsInfo.installationVersion).Major
$generator = if ($vsMajor -ge 18) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
& $cmakeCommand.Source -S $projectRoot -B (Join-Path $projectRoot 'build') -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($vsInfo.installationPath)"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmakeCommand.Source --build (Join-Path $projectRoot 'build') --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed. Exit the running CpuOverlay before rebuilding.' }
if ($Configuration -eq 'Release') {
    $exe = Get-Item -LiteralPath (Join-Path $projectRoot 'package\CpuOverlay.exe')
    $exe.VersionInfo | Format-List CompanyName,FileDescription,ProductName,FileVersion,ProductVersion,LegalCopyright,OriginalFilename,Language
    Write-Host "Ready: $($exe.FullName) ($($exe.Length) bytes)"
}
