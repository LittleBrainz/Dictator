[CmdletBinding()]
param([Parameter(Mandatory)][string] $WindowsSdkVersion)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$publish = Join-Path $repo 'artifacts/publish'
$stage = Join-Path $repo 'artifacts/Dictator'

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item "$stage/lib/Native" -ItemType Directory -Force | Out-Null
New-Item "$stage/lib/WinUI" -ItemType Directory -Force | Out-Null
# Keep runtime modules and neutral PRI resources intact. The current testing
# distribution retains only the four locales explicitly requested by the user.
Copy-Item "$publish/*" "$stage/lib/WinUI" -Recurse
Copy-Item "$repo/artifacts/native/bin/Release/Dictator.exe" "$stage/Dictator.exe"
Copy-Item "$repo/artifacts/native/bin/Release/Dictator.Native.dll" "$stage/lib/Native/Dictator.Native.dll"
Copy-Item "$repo/packaging/README.txt" "$stage/README.txt"
$runtime = Join-Path $stage 'lib/WinUI'
$allowedLocales = @('en-GB', 'en-US', 'fr-FR', 'zh-CN')
Get-ChildItem $runtime -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
    if ($_.Name -match '^[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$' -and $_.Name -notin $allowedLocales) {
        # Identify culture names rather than deleting unrelated runtime folders.
        try { $null = [Globalization.CultureInfo]::GetCultureInfo($_.Name) }
        catch { return }
        Remove-Item $_.FullName -Recurse -Force
    }
}
foreach ($dependency in @('Dictator.App.exe', 'Dictator.App.runtimeconfig.json', 'System.Private.CoreLib.dll', 'coreclr.dll', 'hostfxr.dll', 'hostpolicy.dll', 'Microsoft.UI.Xaml.dll', 'Microsoft.WindowsAppRuntime.dll', 'Microsoft.UI.pri', 'Microsoft.UI.Xaml.Controls.pri', 'Microsoft.WindowsAppRuntime.pri', 'vcruntime140.dll', 'msvcp140.dll')) {
    if (-not (Test-Path (Join-Path $runtime $dependency))) { throw "Required app-local runtime file is missing: $dependency" }
}
if (-not (Test-Path "$runtime/Dictator.App.pri") -and -not (Test-Path "$runtime/resources.pri")) {
    throw 'The managed application PRI resource is missing.'
}
foreach ($locale in $allowedLocales) {
    if (-not (Test-Path (Join-Path $runtime $locale))) { throw "Required WinUI runtime locale is missing: $locale" }
}
Get-ChildItem $runtime -Recurse -File | ForEach-Object {
    [IO.Path]::GetRelativePath($runtime, $_.FullName)
} | Set-Content "$repo/artifacts/publish-manifest.txt" -Encoding utf8NoBOM

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
Write-Host "Staged app-local Dictator with .NET $($metadata.dotnetRuntimeVersion), Windows App SDK $wasdk, Windows SDK $WindowsSdkVersion. Root launcher: $((Get-Item "$stage/Dictator.exe").Length) bytes."
