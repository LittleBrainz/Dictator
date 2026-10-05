[CmdletBinding()]
param([Parameter(Mandatory)][string] $WindowsSdkVersion)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$publish = Join-Path $repo 'artifacts/publish'
$stage = Join-Path $repo 'artifacts/Dictator'
$manifest = Join-Path $repo 'artifacts/bundle-manifest.txt'

# Never silently discard a dependency that escaped the single-file bundle.
$external = @(Get-ChildItem $publish | Where-Object { $_.Name -ne 'Dictator.exe' -and $_.Extension -ne '.pdb' })
if ($external.Count) { throw "Unexpected unbundled publish content: $($external.Name -join ', '). Investigate the supported deployment layout." }
if (-not (Test-Path "$publish/Dictator.exe")) { throw 'Published executable is missing.' }
if (-not (Test-Path $manifest)) { throw 'Single-file bundle manifest is missing.' }
$bundled = @(Get-Content $manifest)
Write-Host "Bundle contains $($bundled.Count) files; neutral PRI resources: $(($bundled | Where-Object { $_ -match '\.pri$' }) -join ', ')"
# .NET 10's Windows singlefilehost contains the native CLR/host itself. Its
# RuntimeList.xml marks coreclr/hostpolicy DropFromSingleFile=true by design.
foreach ($dependency in @('System.Private.CoreLib.dll', 'Dictator.runtimeconfig.json', 'Microsoft.UI.Xaml.dll', 'Microsoft.WindowsAppRuntime.dll', 'Dictator.pri', 'Microsoft.UI.pri', 'Microsoft.UI.Xaml.Controls.pri', 'Microsoft.WindowsAppRuntime.pri', 'vcruntime140.dll', 'msvcp140.dll')) {
    if (-not ($bundled | Where-Object { ($_ -replace '\\', '/') -match "(^|/)$([regex]::Escape($dependency))$" })) {
        throw "Required runtime content is absent from the single-file bundle: $dependency"
    }
}
# Retain the SDK's neutral resources and every locale. No PRI rewriting or locale pruning.
foreach ($locale in @('en-GB', 'en-US', 'zh-CN')) {
    if (-not ($bundled | Where-Object { ($_ -replace '\\', '/') -match "(^|/)$locale/" })) {
        throw "Required WinUI runtime locale is absent from the bundle: $locale"
    }
}

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item "$stage/lib/Native" -ItemType Directory -Force | Out-Null
Copy-Item "$publish/Dictator.exe" "$stage/Dictator.exe"
Copy-Item "$repo/artifacts/native/bin/Release/Dictator.Native.dll" "$stage/lib/Native/Dictator.Native.dll"
Copy-Item "$repo/packaging/README.txt" "$stage/README.txt"

[xml]$sdkPin = Get-Content "$repo/src/Dictator.App/Dictator.App.csproj" -Raw
[xml]$packages = Get-Content "$repo/Directory.Packages.props" -Raw
$wasdk = ($packages.Project.ItemGroup.PackageVersion | Where-Object Include -EQ 'Microsoft.WindowsAppSDK').Version
$commit = & git -C $repo rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record the Git commit.' }
$build = if ($env:GITHUB_RUN_NUMBER) { "$env:GITHUB_RUN_NUMBER.$env:GITHUB_RUN_ATTEMPT" } else { 'local' }
$metadata = [ordered]@{
    productVersion = '0.0.1'
    gitCommit = $commit.Trim()
    ciBuildNumber = $build
    configuration = 'Release'
    architecture = 'win-x64'
    dotnetSdkVersion = (Get-Content "$repo/global.json" -Raw | ConvertFrom-Json).sdk.version
    dotnetRuntimeVersion = $sdkPin.Project.PropertyGroup.ExpectedDotnetRuntimeVersion
    windowsAppSdkVersion = $wasdk
    windowsSdkVersion = $WindowsSdkVersion
    nativeAbiVersion = 1
}
$metadata | ConvertTo-Json | Set-Content "$stage/lib/build-info.json" -Encoding utf8NoBOM
Write-Host "Staged self-contained Dictator with .NET $($metadata.dotnetRuntimeVersion), Windows App SDK $wasdk, Windows SDK $WindowsSdkVersion."
