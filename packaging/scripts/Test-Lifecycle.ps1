[CmdletBinding()]
param([Parameter(Mandatory)][string] $PackageRoot, [Parameter(Mandatory)][string] $EvidenceRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DictatorWindows {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
}
'@
$data = Join-Path $EvidenceRoot 'isolated profile 中文/.dictator'
$launcher = Join-Path $PackageRoot 'Dictator.exe'
$sequence = 0
$hostProcess = $null
$run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$valueName = 'Dictator.Phase1.Test'
$existing = Get-ItemPropertyValue $run $valueName -ErrorAction SilentlyContinue
function Request([string] $Command) {
    $script:sequence++
    $output = Join-Path $EvidenceRoot "lifecycle-$sequence-$Command.json"
    $process = Start-Process $launcher -ArgumentList @('--lifecycle-test', "`"$data`"", "`"$output`"", $Command) -PassThru
    if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) { throw "Launcher failed for $Command" }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path $output) -and $timer.Elapsed.TotalSeconds -lt 15) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path $output)) { throw "No resident response for $Command" }
    # File writes are small, but retry a concurrent startup/restart report write.
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try { $report = Get-Content $output -Raw | ConvertFrom-Json; break }
        catch { Start-Sleep -Milliseconds 50 }
    }
    if ($report.status -ne 'ok') { throw "Resident $Command failed: $(Get-Content $output -Raw)" }
    return $report
}
function Track-Host($Report) {
    $script:hostProcess = [Diagnostics.Process]::GetProcessById($Report.processId)
    $null = $hostProcess.Handle
}
function Assert-CleanExit {
    if (-not $hostProcess.WaitForExit(15000)) { throw 'Resident Quit/Restart hung.' }
    if ($hostProcess.ExitCode -ne 0) { throw "Resident exited with $($hostProcess.ExitCode)" }
}
try {
    $fresh = Request 'launch'
    Track-Host $fresh
    if ($fresh.settingsTitle -ne 'Dictator Settings' -or $fresh.settingsVisible -or $fresh.settingsWasActivated -or
        -not $fresh.widgetVisible -or -not $fresh.trayReady -or $fresh.settingsPath -ne (Join-Path $data 'settings.json')) {
        throw 'Normal resident startup did not show only the Widget with a hidden Settings window.'
    }
    $quiet = Request 'startup'
    if ($quiet.processId -ne $fresh.processId -or $quiet.settingsVisible) { throw 'Duplicate startup did not stay quiet in the existing instance.' }
    $null = Request 'quit'
    Assert-CleanExit
    if ([DictatorWindows]::IsWindow([IntPtr]$fresh.widgetHandle)) { throw 'Quit leaked the Widget HWND.' }
    Write-Host 'Quit succeeds with Settings created quietly and never activated.'

    $fresh = Request 'launch'
    Track-Host $fresh
    $manual = Request 'launch'
    if ($manual.processId -ne $fresh.processId -or -not $manual.settingsVisible) { throw 'Second manual launch did not activate existing Settings.' }
    # Exercise the real WM_CLOSE path instead of calling an app-only hide method.
    $null = [DictatorWindows]::PostMessage([IntPtr]$manual.settingsHandle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $closed = Request 'snapshot'
    if ($closed.settingsVisible -or -not $closed.trayReady -or -not $closed.widgetVisible -or $hostProcess.HasExited) {
        throw 'Closing Settings ended residence or hid the Widget/tray.'
    }
    $null = [DictatorWindows]::PostMessage([IntPtr]$closed.widgetHandle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    $dismissed = Request 'snapshot'
    if ($dismissed.widgetVisible) { throw 'Widget close did not dismiss the Widget.' }
    $foreground = [DictatorWindows]::GetForegroundWindow()
    $opened = Request 'open-widget'
    if (-not $opened.widgetVisible -or [DictatorWindows]::GetForegroundWindow() -ne $foreground) { throw 'Open Widget stole foreground focus or failed.' }
    $changed = Request 'set-preferences'
    if ($changed.preferences.Theme -ne 'Dark' -or $changed.preferences.WidgetZoom -ne 1.155 -or
        $changed.preferences.Hotkey.Modifiers -ne 6 -or $changed.settingsError) { throw 'Preferences could not be saved.' }
    $enabled = Request 'startup-on'
    $registered = Get-ItemPropertyValue $run $valueName
    if (-not $enabled.startupEnabled -or $registered -ne "`"$launcher`" --startup") { throw 'Per-user startup registration is invalid.' }
    $disabled = Request 'startup-off'
    if ($disabled.startupEnabled -or (Get-ItemPropertyValue $run $valueName -ErrorAction SilentlyContinue)) { throw 'Per-user startup registration was not removed.' }
    $oldPid = $fresh.processId
    $null = Request 'restart'
    Assert-CleanExit
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 300
        try { $restarted = Request 'snapshot' } catch { $restarted = $null }
    } while (($null -eq $restarted -or $restarted.processId -eq $oldPid) -and $timer.Elapsed.TotalSeconds -lt 20)
    if ($null -eq $restarted -or $restarted.processId -eq $oldPid) { throw 'Restart did not create a new resident instance.' }
    Track-Host $restarted
    if ($restarted.settingsVisible -or $restarted.settingsWasActivated -or -not $restarted.widgetVisible -or
        $restarted.preferences.Theme -ne 'Dark' -or $restarted.preferences.WidgetZoom -ne 1.155 -or $restarted.preferences.Hotkey.Key -ne 0x77) {
        throw 'Restart did not preserve preferences and restore the quiet startup layout.'
    }
    $null = Request 'quit'
    Assert-CleanExit
    Write-Host 'Single instance, Settings close, focus-preserving Widget recovery, preferences, startup registration and Restart passed.'

    $badFile = Join-Path $data 'settings.json'
    $badText = '{"schemaVersion":999,"futureContent":"preserve me"}'
    Set-Content $badFile $badText -Encoding utf8NoBOM -NoNewline
    $bad = Request 'launch'
    Track-Host $bad
    if (-not $bad.settingsError) { throw 'Unsupported settings schema was not surfaced.' }
    $blocked = Request 'set-preferences'
    if (-not $blocked.settingsError -or (Get-Content $badFile -Raw) -ne $badText) { throw 'Bad settings were overwritten.' }
    $null = Request 'quit'
    Assert-CleanExit
    Write-Host 'Unsupported settings remain intact and cannot be silently overwritten.'
}
finally {
    if ($hostProcess -and -not $hostProcess.HasExited) { $hostProcess.Kill($true) }
    if ($null -ne $existing) { Set-ItemProperty $run $valueName $existing }
    else { Remove-ItemProperty $run $valueName -ErrorAction SilentlyContinue }
}
