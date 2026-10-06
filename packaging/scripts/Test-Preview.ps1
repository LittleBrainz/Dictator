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
    public static IntPtr FindTarget() => FindWindow("Dictator.Phase2.Target", null);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    public static string ForegroundProcess() {
        GetWindowThreadProcessId(GetForegroundWindow(), out var process);
        try { return System.Diagnostics.Process.GetProcessById((int)process).ProcessName; }
        catch { return "unavailable"; }
    }
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
}
'@
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$fixtureExe = Join-Path $repo 'artifacts/native/bin/Release/Dictator.Interaction.Tests.exe'
$launcher = Join-Path $PackageRoot 'Dictator.exe'
$data = Join-Path $EvidenceRoot 'phase4 profile 中文/.dictator'
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
    $report = $null
    for ($retry = 0; $retry -lt 40; $retry++) {
        try {
            $candidate = Get-Content $output -Raw | ConvertFrom-Json
            # ConvertFrom-Json accepts an empty file as null without throwing.
            # Startup creates the report path before its write completes.
            if ($null -ne $candidate -and $candidate.PSObject.Properties['status']) {
                $report = $candidate; break
            }
            Start-Sleep -Milliseconds 25
        }
        catch { Start-Sleep -Milliseconds 25 }
    }
    if ($null -eq $report) { throw "No complete preview resident response for $Command." }
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
    throw "Preview state mismatch: eligible=$($state.targetEligible), talking=$($state.previewTalking), visible=$($state.widgetVisible), targetPID=$($state.targetProcessId), foreground=$($state.targetForeground), focus=$($state.targetFocus), reason=$($state.targetReason), status=$($state.targetStatus), fixturePID=$($fixtureProcess.Id), fixtureHWND=$target, currentForeground=$([DictatorPreviewWindows]::GetForegroundWindow()), foregroundProcess=$([DictatorPreviewWindows]::ForegroundProcess())."
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
    $fixtureProcess = Start-Process $fixtureExe -ArgumentList '--target-fixture' -NoNewWindow -PassThru
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        # Keep the null window-name wildcard inside C#: PowerShell can coerce a
        # null string argument into empty text, which requests an empty title.
        $target = [DictatorPreviewWindows]::FindTarget()
        if ($target -ne [IntPtr]::Zero) { break }
        if ($fixtureProcess.HasExited) { throw "Preview fixture exited with $($fixtureProcess.ExitCode)." }
        Start-Sleep -Milliseconds 25
    } while ($timer.Elapsed.TotalSeconds -lt 8)
    if ($target -eq [IntPtr]::Zero) { throw 'Preview target fixture did not start.' }
    $null = [DictatorPreviewWindows]::SetForegroundWindow($target)
    # Explicit US layout for this package test; native interaction tests cover UK too.
    $null = [DictatorPreviewWindows]::SendMessage($target, 0x8005, [IntPtr]::Zero, [IntPtr]::Zero)
    $null = [DictatorPreviewWindows]::SendMessage($target, 0x8001, [IntPtr]::Zero, [IntPtr]::Zero)
    Write-Host "Preview fixture HWND=$target, PID=$($fixtureProcess.Id), foreground=$([DictatorPreviewWindows]::GetForegroundWindow()) before application launch."
    $fresh = Request 'launch'
    $hostProcess = [Diagnostics.Process]::GetProcessById($fresh.processId)
    $null = $hostProcess.Handle
    if ($fresh.hotkeyError) { throw "Default Hotkey registration failed: $($fresh.hotkeyError)" }
    if ([DictatorPreviewWindows]::GetForegroundWindow() -ne $target) { throw "Startup lost target focus to $([DictatorPreviewWindows]::ForegroundProcess())." }
    $null = Wait-State $true $false
    # Phase 4 requires a credential before capture. The test target is isolated:
    # never read a real user key or contact OpenAI from package verification.
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $catalog = Request 'snapshot'
        if ($catalog.microphonesLoaded) { break }
        Start-Sleep -Milliseconds 25
    } while ($timer.Elapsed.TotalSeconds -lt 8)
    if (-not $catalog.microphonesLoaded) { throw 'Microphone enumeration did not complete.' }
    if ($catalog.apiKeyConfigured) { throw 'Fresh isolated profile unexpectedly has a credential.' }
    Tap-Hotkey
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $active = Request 'snapshot'
        if ($active.speechError -and -not $active.previewTalking) { break }
        Start-Sleep -Milliseconds 25
    } while ($timer.Elapsed.TotalSeconds -lt 8)
    if (-not $active.speechError -or $active.previewTalking -or $active.audioState -eq 2 -or $active.capturedFrames -ne 0) {
        throw 'Missing API credential did not fail before capture.'
    }
    if ([DictatorPreviewWindows]::GetForegroundWindow() -ne $target) { throw 'Transcription setup error stole focus.' }
    $missing = Request 'microphone-missing'
    if ($missing.preferences.microphoneId -ne 'Dictator.Missing.Test.Microphone') { throw 'Microphone selection did not persist.' }
    if ($missing.preferences.PSObject.Properties['apiKey']) { throw 'Credential leaked into preferences.' }
    Write-Host 'Transcription credential gate: actionable error, no microphone capture, isolated credentials and preserved focus passed.'
    $null = Request 'microphone-default'
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
    Write-Host 'Extracted package: missing-key safety, persisted microphone selection, no-cursor recovery and unchanged target text/focus passed.'
}
finally {
    Release-Hotkey
    if ($hostProcess -and -not $hostProcess.HasExited) { $hostProcess.Kill($true) }
    if ($fixtureProcess -and -not $fixtureProcess.HasExited) { $fixtureProcess.Kill($true) }
}
