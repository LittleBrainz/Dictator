[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'The authoritative package build requires the hosted Windows x64 runner.' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location $repo

function Invoke-Checked([scriptblock] $Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Build command failed with exit code $LASTEXITCODE." }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Hosted MSVC x64 tools were not found.' }
$vsVersion = [version](& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion)
$generator = switch ($vsVersion.Major) {
    17 { 'Visual Studio 17 2022' }
    18 { 'Visual Studio 18 2026' }
    default { throw "Validate CMake support for the installed stable Visual Studio $vsVersion." }
}
$redistVersions = Get-ChildItem (Join-Path $visualStudio 'VC/Redist/MSVC') -Directory |
    Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending
$crtDirectory = $redistVersions | ForEach-Object {
    Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'vcruntime140.dll') }
} | Select-Object -First 1
if (-not $crtDirectory) { throw 'The app-local VC redistributable is missing.' }
$crt = $crtDirectory.FullName
Write-Host "Bundling the hosted x64 VC runtime from $crt"
$sdkVersion = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include') -Directory |
    Where-Object Name -Match '^10\.0\.\d+\.0$' | Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1 -ExpandProperty Name
if ([version]$sdkVersion -lt [version]'10.0.26100.0') { throw 'The hosted Windows 11 SDK is too old.' }
Write-Host "Building with $generator ($vsVersion), Windows SDK $sdkVersion"

New-Item artifacts -ItemType Directory -Force | Out-Null
$manifest = Join-Path $repo 'artifacts/bundle-manifest.txt'
Invoke-Checked { cmake -S . -B artifacts/native -G $generator -A x64 "-DCMAKE_SYSTEM_VERSION=$sdkVersion" "-DCMAKE_GENERATOR_INSTANCE=$visualStudio" }
Invoke-Checked { cmake --build artifacts/native --config Release --parallel 2 }
Invoke-Checked { ctest --test-dir artifacts/native -C Release --output-on-failure }
Invoke-Checked { dotnet restore Dictator.sln --locked-mode }
Invoke-Checked { dotnet test tests/Dictator.Core.Tests -c Release --no-restore --logger 'trx;LogFileName=core.trx' --results-directory artifacts/test-results }
Invoke-Checked { dotnet publish src/Dictator.App -c Release --no-restore --self-contained true -o artifacts/publish "-p:BundledVCRuntimeDir=$crt" "-p:BundleManifestPath=$manifest" }
& "$PSScriptRoot/Stage.ps1" -WindowsSdkVersion $sdkVersion
$env:DICTATOR_DISTRIBUTION_ROOT = Join-Path $repo 'artifacts/Dictator'
Invoke-Checked { dotnet test tests/Dictator.Interop.Tests -c Release --no-restore --logger 'trx;LogFileName=interop.trx' --results-directory artifacts/test-results }
& "$PSScriptRoot/Test-Package.ps1"
