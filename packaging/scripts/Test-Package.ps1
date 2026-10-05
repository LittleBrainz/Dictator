[CmdletBinding()]
param([string] $PackageRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $PackageRoot) { $PackageRoot = Join-Path $repo 'artifacts/Dictator' }
$expected = @('Dictator.exe', 'README.txt', 'lib')
$actual = @(Get-ChildItem $PackageRoot | Select-Object -ExpandProperty Name | Sort-Object)
if (Compare-Object ($expected | Sort-Object) $actual) { throw "Distribution root must contain exactly: $($expected -join ', ')" }
$identity = Get-Content "$PackageRoot/lib/build-info.json" -Raw | ConvertFrom-Json
if ($identity.architecture -ne 'win-x64' -or $identity.nativeAbiVersion -ne 1) { throw 'Package build identity is invalid.' }

$sandbox = Join-Path $repo 'artifacts/package smoke with spaces'
if (Test-Path $sandbox) { Remove-Item $sandbox -Recurse -Force }
New-Item $sandbox -ItemType Directory | Out-Null
# Model the user's one extraction. This temporary zip is never uploaded.
Compress-Archive -Path "$PackageRoot/*" -DestinationPath "$sandbox/Dictator.zip"
Expand-Archive -Path "$sandbox/Dictator.zip" -DestinationPath "$sandbox/extracted app"
$extracted = Join-Path $sandbox 'extracted app'
$working = Join-Path $sandbox 'different working directory'
New-Item $working -ItemType Directory | Out-Null
$env:DOTNET_BUNDLE_EXTRACT_BASE_DIR = Join-Path $sandbox 'isolated runtime extraction'
# SDK/runtime installations on the runner must not be used by the packaged application.
$env:DOTNET_ROOT = Join-Path $sandbox 'no installed dotnet'
$env:DOTNET_ROOT_X64 = $env:DOTNET_ROOT
$env:DOTNET_MULTILEVEL_LOOKUP = '0'

function Invoke-Smoke([string] $Mode, [bool] $ExpectUi) {
    $output = Join-Path $sandbox "$Mode.json"
    $process = Start-Process "$extracted/Dictator.exe" -ArgumentList @("--$Mode", "`"$output`"") -WorkingDirectory $working -PassThru
    if (-not $process.WaitForExit(90000)) {
        $process.Kill($true)
        throw "Packaged $Mode timed out."
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path $output)) { throw "Packaged $Mode failed with exit code $($process.ExitCode)." }
    $report = Get-Content $output -Raw | ConvertFrom-Json
    if ($report.status -ne 'ok' -or $report.uiReady -ne $ExpectUi -or $report.nativeAbi -ne 1 -or
        $report.build.GitCommit -ne $identity.gitCommit -or $report.runtimeVersion -ne $identity.dotnetRuntimeVersion -or
        $report.roundTrip -ne 'Dictator — UTF-16 ✓ 中文 😃' -or $report.snapshot.StructSize -ne 24 -or
        $report.distributionRoot -ne $extracted) { throw "Packaged $Mode identity/ABI/UI verification failed." }
    # Prove bundled WinUI resources were really extracted, rather than resolved from the runner.
    foreach ($file in @('Microsoft.UI.Xaml.dll', 'Dictator.pri', 'Microsoft.UI.pri', 'Microsoft.UI.Xaml.Controls.pri', 'Microsoft.WindowsAppRuntime.pri', 'System.Private.CoreLib.dll', 'vcruntime140.dll')) {
        if (-not (Test-Path (Join-Path $report.runtimeDirectory $file))) { throw "Extracted dependency missing: $file" }
    }
    $runtimeConfig = Get-Content (Join-Path $report.runtimeDirectory 'Dictator.runtimeconfig.json') -Raw | ConvertFrom-Json
    if ($runtimeConfig.runtimeOptions.PSObject.Properties.Name -contains 'framework' -or
        $runtimeConfig.runtimeOptions.PSObject.Properties.Name -contains 'frameworks') {
        throw 'Package runtime configuration depends on an installed framework.'
    }
    $included = @($runtimeConfig.runtimeOptions.includedFrameworks | Where-Object { $_.name -eq 'Microsoft.NETCore.App' -and $_.version -eq $identity.dotnetRuntimeVersion })
    if ($included.Count -ne 1) { throw 'Self-contained runtime identity is absent from the extracted configuration.' }
    foreach ($locale in @('en-GB', 'en-US', 'zh-CN')) {
        if (-not (Test-Path (Join-Path $report.runtimeDirectory $locale))) { throw "Extracted locale missing: $locale" }
    }
}

try {
    Invoke-Smoke 'smoke-test' $false
    Invoke-Smoke 'ui-smoke-test' $true

    # Useful dependency error is also part of the staged product contract.
    Remove-Item "$extracted/lib/Native/Dictator.Native.dll"
    $missingResult = Join-Path $sandbox 'missing-native.json'
    $missing = Start-Process "$extracted/Dictator.exe" -ArgumentList @('--smoke-test', "`"$missingResult`"") -WorkingDirectory $working -PassThru
    if (-not $missing.WaitForExit(30000)) { $missing.Kill($true); throw 'Missing-dependency probe timed out.' }
    if ($missing.ExitCode -eq 0 -or -not (Test-Path $missingResult)) { throw 'Missing native DLL did not produce a useful error.' }
    $errorReport = Get-Content $missingResult -Raw | ConvertFrom-Json
    if ($errorReport.status -ne 'error' -or $errorReport.error -ne 'FileNotFoundException') { throw 'Unexpected missing-dependency result.' }
    Write-Host 'Packaged ABI, real WinUI launch, extraction, locales, build identity, and dependency error checks passed.'
}
finally {
    Remove-Item Env:DOTNET_ROOT, Env:DOTNET_ROOT_X64, Env:DOTNET_MULTILEVEL_LOOKUP, Env:DOTNET_BUNDLE_EXTRACT_BASE_DIR -ErrorAction SilentlyContinue
}
