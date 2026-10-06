[CmdletBinding()]
param([Parameter(Mandatory)][string] $PackageRoot, [Parameter(Mandatory)][string] $EvidenceRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DictatorPreviewWindows {
    [StructLayout(LayoutKind.Explicit, Size=40)] public struct Input {
        [FieldOffset(0)] public uint Type;
        [FieldOffset(8)] public ushort Key;
        [FieldOffset(12)] public uint Flags;
    }
    [DllImport("user32.dll")] static extern uint SendInput(uint count, Input[] inputs, int size);
    public static void Key(ushort key, bool up) {
        if (SendInput(1, new[] {new Input { Type=1, Key=key, Flags=up ? 2u : 0u }}, 40) != 1)
            throw new InvalidOperationException("SendInput failed");
    }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
}
'@
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$fixtureExe = Join-Path $repo 'artifacts/native/bin/Release/Dictator.Interaction.Tests.exe'
$launcher = Join-Path $PackageRoot 'Dictator.exe'
$data = Join-Path $EvidenceRoot 'phase2 profile 中文/.dictator'
$sequence = 0
$fixtureProcess = $null
$hostProcess = $null
function Request([string] $Command) {
    $script:sequence++
    $output = Join-Path $EvidenceRoot "preview-$sequence-$Command.json"
    $process = Start-Process $launcher -ArgumentList @('--lifecycle-test', "`"$data`"", "`"$output`"", $Command) -PassThru
    if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) { throw "Preview launcher failed: $Command" }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path $output) -and $timer.Elapsed.TotalSeconds -lt 10) { Start-Sleep -Milliseconds 25 }
    for ($retry = 0; $retry -lt 20; $retry++) {
        try { $report = Get-Content $output -Raw | ConvertFrom-Json; break }
        catch { Start-Sleep -Milliseconds 25 }
    }
    if ($report.status -ne 'ok') { throw 'Preview resident response failed.' }
    return $report
}
function Wait-State([bool] $Eligible, [bool] $Talking, [bool] $Visible = $true) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $state = Request 'snapshot'
        if ($state.targetEligible -eq $Eligible -and $state.previewTalking -eq $Talking -and $state.widgetVisible -eq $Visible) { return $state }
        Start-Sleep -Milliseconds 30
    } while ($timer.Elapsed.TotalSeconds -lt 8)
    throw "Preview state mismatch: eligible=$($state.targetEligible), talking=$($state.previewTalking), visible=$($state.widgetVisible)."
}
function Press-Hotkey {
    [DictatorPreviewWindows]::Key(0x11, $false)
    [DictatorPreviewWindows]::Key(0x12, $false)
    [DictatorPreviewWindows]::Key(0xDC, $false)
}
function Release-Hotkey {
    [DictatorPreviewWindows]::Key(0xDC, $true)
    [DictatorPreviewWindows]::Key(0x12, $true)
    [DictatorPreviewWindows]::Key(0x11, $true)
}
function Tap-Hotkey { Press-Hotkey; Start-Sleep -Milliseconds 40; Release-Hotkey }
try {
    $fixtureProcess = Start-Process $fixtureExe -ArgumentList '--target-fixture' -PassThru
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $target = [DictatorPreviewWindows]::FindWindow('Dictator.Phase2.Target', $null)
        if ($target -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 25
    } while ($timer.Elapsed.TotalSeconds -lt 8)
    if ($target -eq [IntPtr]::Zero) { throw 'Preview target fixture did not start.' }
    $null = [DictatorPreviewWindows]::SetForegroundWindow($target)
    # Explicit US layout for this package test; native interaction tests cover UK too.
    $null = [DictatorPreviewWindows]::SendMessage($target, 0x8005, [IntPtr]::Zero, [IntPtr]::Zero)
    $null = [DictatorPreviewWindows]::SendMessage($target, 0x8001, [IntPtr]::Zero, [IntPtr]::Zero)
    $fresh = Request 'launch'
    $hostProcess = [Diagnostics.Process]::GetProcessById($fresh.processId)
    $null = $hostProcess.Handle
    if ($fresh.hotkeyError) { throw "Default Hotkey registration failed: $($fresh.hotkeyError)" }
    $null = Wait-State $true $false
    Tap-Hotkey
    $active = Wait-State $true $true
    if ([DictatorPreviewWindows]::GetForegroundWindow() -ne $target) { throw 'Hotkey stole focus.' }
    Tap-Hotkey
    $null = Wait-State $true $false
    Press-Hotkey
    $null = Wait-State $true $true
    # Waiting for the IPC snapshot itself exceeds 500 ms; retain a further 550 ms.
    Start-Sleep -Milliseconds 550
    Release-Hotkey
    $null = Wait-State $true $false
    Tap-Hotkey
    $null = Wait-State $true $true
    $null = [DictatorPreviewWindows]::SendMessage($target, 0x8002, [IntPtr]::Zero, [IntPtr]::Zero)
    $null = Wait-State $false $false
    Tap-Hotkey
    $null = Wait-State $false $false
    $null = Request 'close-widget'
    $null = Wait-State $false $false $false
    Tap-Hotkey
    $recovered = Wait-State $false $false
    if ([DictatorPreviewWindows]::GetForegroundWindow() -ne $target) { throw 'Hotkey recovery stole focus.' }
    if ([DictatorPreviewWindows]::SendMessage($target, 0x800A, [IntPtr]::Zero, [IntPtr]::Zero) -ne [IntPtr]1) {
        throw 'Preview modified target text or selection.'
    }
    $null = Request 'quit'
    if (-not $hostProcess.WaitForExit(15000) -or $hostProcess.ExitCode -ne 0) { throw 'Preview Quit did not complete cleanly.' }
    Write-Host 'Extracted package: tap toggle, hold release, eligibility loss, no-cursor recovery and unchanged target text/focus passed.'
}
finally {
    Release-Hotkey
    if ($hostProcess -and -not $hostProcess.HasExited) { $hostProcess.Kill($true) }
    if ($fixtureProcess -and -not $fixtureProcess.HasExited) { $fixtureProcess.Kill($true) }
}
