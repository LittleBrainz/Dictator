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

$sandbox = Join-Path $repo 'artifacts/package smoke with spaces 中文'
if (Test-Path $sandbox) { Remove-Item $sandbox -Recurse -Force }
New-Item $sandbox -ItemType Directory | Out-Null
# Model the user's one extraction. This temporary zip is never uploaded.
Compress-Archive -Path "$PackageRoot/*" -DestinationPath "$sandbox/Dictator.zip"
Expand-Archive -Path "$sandbox/Dictator.zip" -DestinationPath "$sandbox/extracted app"
$extracted = Join-Path $sandbox 'extracted app'
$working = Join-Path $sandbox 'different working directory'
New-Item $working -ItemType Directory | Out-Null
$savedEnvironment = @{}
foreach ($name in @('DOTNET_ROOT', 'DOTNET_ROOT_X64', 'DOTNET_MULTILEVEL_LOOKUP', 'DOTNET_BUNDLE_EXTRACT_BASE_DIR')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
}
$env:DOTNET_BUNDLE_EXTRACT_BASE_DIR = Join-Path $sandbox 'must not extract runtime'
# SDK/runtime installations on the runner must not be used by the packaged application.
$env:DOTNET_ROOT = Join-Path $sandbox 'no installed dotnet'
$env:DOTNET_ROOT_X64 = $env:DOTNET_ROOT
$env:DOTNET_MULTILEVEL_LOOKUP = '0'

function Invoke-Smoke([string] $Mode, [bool] $ExpectUi) {
    $output = Join-Path $sandbox "$Mode.json"
    Remove-Item $output -ErrorAction SilentlyContinue
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process "$extracted/Dictator.exe" -ArgumentList @("--$Mode", "`"$output`"") -WorkingDirectory $working -PassThru
    if (-not $process.WaitForExit(90000)) {
        $process.Kill($true)
        throw "Packaged $Mode timed out."
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path $output)) {
        $detail = if (Test-Path $output) { Get-Content $output -Raw } else { 'No startup report was written.' }
        throw "Packaged $Mode failed with exit code $($process.ExitCode): $detail"
    }
    $report = Get-Content $output -Raw | ConvertFrom-Json
    if ($report.status -ne 'ok' -or $report.uiReady -ne $ExpectUi -or $report.nativeAbi -ne 1 -or
        $report.build.GitCommit -ne $identity.gitCommit -or $report.runtimeVersion -ne $identity.dotnetRuntimeVersion -or
        $report.roundTrip -ne 'Dictator — UTF-16 ✓ 中文 😃' -or $report.snapshot.StructSize -ne 24 -or
        $report.distributionRoot -ne $extracted) { throw "Packaged $Mode identity/ABI/UI verification failed." }
    $runtime = [IO.Path]::GetFullPath((Join-Path $extracted 'lib/WinUI'))
    if ([IO.Path]::TrimEndingDirectorySeparator($report.runtimeDirectory) -ne $runtime) {
        throw "The app is not running from its installed lib/WinUI folder: $($report.runtimeDirectory)"
    }
    if (Test-Path $env:DOTNET_BUNDLE_EXTRACT_BASE_DIR) { throw 'Application startup unexpectedly extracted a runtime.' }
    $data = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)) '.dictator'
    if ($report.userDataDirectory -ne $data) { throw 'User data does not resolve to the profile .dictator directory.' }
    # Runtime modules and neutral resources must already be present after unzipping.
    foreach ($file in @('Microsoft.UI.Xaml.dll', 'Microsoft.UI.pri', 'Microsoft.UI.Xaml.Controls.pri', 'Microsoft.WindowsAppRuntime.pri', 'System.Private.CoreLib.dll', 'coreclr.dll', 'hostfxr.dll', 'vcruntime140.dll')) {
        if (-not (Test-Path (Join-Path $runtime $file))) { throw "App-local dependency missing: $file" }
    }
    $runtimeConfig = Get-Content (Join-Path $runtime 'Dictator.App.runtimeconfig.json') -Raw | ConvertFrom-Json
    if ($runtimeConfig.runtimeOptions.PSObject.Properties.Name -contains 'framework' -or
        $runtimeConfig.runtimeOptions.PSObject.Properties.Name -contains 'frameworks') {
        throw 'Package runtime configuration depends on an installed framework.'
    }
    $included = @($runtimeConfig.runtimeOptions.includedFrameworks | Where-Object { $_.name -eq 'Microsoft.NETCore.App' -and $_.version -eq $identity.dotnetRuntimeVersion })
    if ($included.Count -ne 1) { throw 'Self-contained runtime identity is absent from the extracted configuration.' }
    foreach ($locale in @('en-GB', 'en-US', 'fr-FR', 'zh-CN')) {
        if (-not (Test-Path (Join-Path $runtime $locale))) { throw "App-local locale missing: $locale" }
    }
    Write-Host "$Mode completed in $($timer.Elapsed.TotalSeconds.ToString('F2')) s with app-local runtime and profile data path."
}

try {
    Invoke-Smoke 'smoke-test' $false
    Invoke-Smoke 'smoke-test' $false
    Invoke-Smoke 'ui-smoke-test' $true

    # Load real WinUI XAML with each retained language and a removed-language
    # fallback. Keep neutral PRI resources and let the SDK select its fallback.
    foreach ($locale in @('en-GB', 'en-US', 'fr-FR', 'zh-CN', 'de-DE')) {
        $localeReport = Join-Path $sandbox "locale-$locale.json"
        $probe = Start-Process "$extracted/Dictator.exe" -ArgumentList @('--locale-smoke-test', $locale, "`"$localeReport`"") -PassThru
        if (-not $probe.WaitForExit(30000)) { $probe.Kill($true); throw "Locale $locale timed out." }
        if ($probe.ExitCode -ne 0 -or -not (Test-Path $localeReport)) {
            $detail = if (Test-Path $localeReport) { Get-Content $localeReport -Raw } else { 'No locale report.' }
            throw "WinUI locale $locale failed: $detail"
        }
        $localeReady = Get-Content $localeReport -Raw | ConvertFrom-Json
        if ($localeReady.status -ne 'ok' -or -not $localeReady.uiReady) { throw "WinUI locale $locale did not load." }
    }
    $allowed = @('en-GB', 'en-US', 'fr-FR', 'zh-CN')
    $unexpected = @(Get-ChildItem "$extracted/lib/WinUI" -Directory -Recurse | Where-Object {
        $_.Name -match '^[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$' -and $_.Name -notin $allowed
    })
    if ($unexpected.Count) { throw "Unexpected locale directories: $($unexpected.Name -join ', ')" }

    # Normal launches must leave only the managed host running, not a resident helper.
    $normalReport = Join-Path $sandbox 'normal-launch.json'
    $launcher = Start-Process "$extracted/Dictator.exe" -ArgumentList @('--launch-smoke-test', "`"$normalReport`"") -WorkingDirectory $working -PassThru
    $hostProcess = $null
    try {
        if (-not $launcher.WaitForExit(10000) -or $launcher.ExitCode -ne 0) { throw 'The normal launcher did not exit promptly.' }
        $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $($launcher.Id)" |
            Where-Object { $_.ExecutablePath -eq [IO.Path]::GetFullPath((Join-Path $extracted 'lib/WinUI/Dictator.App.exe')) })
        if ($children.Count -ne 1) { throw 'Normal launch did not leave exactly one managed application process.' }
        $hostProcess = [Diagnostics.Process]::GetProcessById($children[0].ProcessId)
        # Keep a process handle before closing the window so Windows can still return
        # its real exit code even if it terminates before WaitForExit is called.
        $null = $hostProcess.Handle
        $deadline = [Diagnostics.Stopwatch]::StartNew()
        do {
            $hostProcess.Refresh()
            if ((Test-Path $normalReport) -and $hostProcess.MainWindowHandle -ne 0) { break }
            Start-Sleep -Milliseconds 100
        } while (-not $hostProcess.HasExited -and $deadline.Elapsed.TotalSeconds -lt 15)
        if (-not (Test-Path $normalReport)) { throw 'The normally launched WinUI application did not report startup completion.' }
        $ready = Get-Content $normalReport -Raw | ConvertFrom-Json
        if ($ready.status -ne 'ok' -or -not $ready.uiReady) { throw "Normal launch failed: $(Get-Content $normalReport -Raw)" }
        if ($hostProcess.MainWindowHandle -eq 0 -or -not $hostProcess.CloseMainWindow() -or -not $hostProcess.WaitForExit(30000)) {
            throw 'Normal Settings window launch/close verification failed.'
        }
        if ($hostProcess.ExitCode -ne 0) { throw "The normal application exited with code $($hostProcess.ExitCode)." }
        Write-Host 'Normal root launcher exits promptly; its one managed host opens and closes successfully.'
    }
    finally {
        if ($hostProcess -and -not $hostProcess.HasExited) { $hostProcess.Kill($true) }
        if (-not $launcher.HasExited) { $launcher.Kill($true) }
    }

    & "$PSScriptRoot/Test-Lifecycle.ps1" -PackageRoot $extracted -EvidenceRoot $sandbox
    & "$PSScriptRoot/Test-Preview.ps1" -PackageRoot $extracted -EvidenceRoot $sandbox

    # Useful dependency error is also part of the staged product contract.
    Remove-Item "$extracted/lib/Native/Dictator.Native.dll"
    $missingResult = Join-Path $sandbox 'missing-native.json'
    $missing = Start-Process "$extracted/Dictator.exe" -ArgumentList @('--smoke-test', "`"$missingResult`"") -WorkingDirectory $working -PassThru
    if (-not $missing.WaitForExit(30000)) { $missing.Kill($true); throw 'Missing-dependency probe timed out.' }
    if ($missing.ExitCode -eq 0 -or -not (Test-Path $missingResult)) { throw 'Missing native DLL did not produce a useful error.' }
    $errorReport = Get-Content $missingResult -Raw | ConvertFrom-Json
    if ($errorReport.status -ne 'error' -or $errorReport.error -ne 'FileNotFoundException') { throw 'Unexpected missing-dependency result.' }
    # A damaged host folder must fail promptly in the native launcher, without a dialog in smoke mode.
    Remove-Item "$extracted/lib/WinUI/Dictator.App.exe"
    $missingHost = Start-Process "$extracted/Dictator.exe" -ArgumentList @('--smoke-test', "`"$missingResult`"") -WorkingDirectory $working -PassThru
    if (-not $missingHost.WaitForExit(10000)) { $missingHost.Kill($true); throw 'Missing host probe timed out.' }
    if ($missingHost.ExitCode -eq 0) { throw 'The launcher accepted a missing managed host.' }
    Write-Host 'Packaged launcher, ABI, real WinUI launch, app-local runtimes/locales, profile data path, and dependency error checks passed.'
}
finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name])
    }
}
