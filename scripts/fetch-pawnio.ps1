$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$folder = Join-Path $projectRoot 'artifacts\pawnio-downloads'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$path = Join-Path $folder 'PawnIO_setup.exe'
if (-not (Test-Path -LiteralPath $path)) {
    Invoke-WebRequest -Uri 'https://github.com/namazso/PawnIO.Setup/releases/download/2.2.0/PawnIO_setup.exe' -OutFile $path
}
if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne '1f519a22e47187f70a1379a48ca604981c4fcf694f4e65b734aaa74a9fba3032') { throw 'PawnIO installer SHA-256 mismatch.' }
if ((Get-AuthenticodeSignature -LiteralPath $path).Status -ne 'Valid') { throw 'PawnIO installer signature invalid.' }
$module = Join-Path $projectRoot 'third_party\pawnio\IntelMSR.bin'
if ((Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -ne 'd6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f') { throw 'PawnIO signed module SHA-256 mismatch.' }
$amdModule = Join-Path $projectRoot 'third_party\pawnio\AMDFamily17.bin'
if ((Get-FileHash -LiteralPath $amdModule -Algorithm SHA256).Hash -ne 'dae74615761b78bdf064dfb3e136252ddcc6fc727d88f14738d0e5800d427a91') { throw 'PawnIO AMD signed module SHA-256 mismatch.' }
$amdSource = Join-Path $projectRoot 'third_party\pawnio\AMDFamily17.p'
if ((Get-FileHash -LiteralPath $amdSource -Algorithm SHA256).Hash -ne '8a1808a2e51e184f96d17362991ae900c65a1a23135a68c256ec6759e8297bb9') { throw 'PawnIO AMD module source SHA-256 mismatch.' }
Write-Host 'PawnIO 2.2.0 signed installer and 0.2.11 IntelMSR / AMDFamily17 modules verified.'
