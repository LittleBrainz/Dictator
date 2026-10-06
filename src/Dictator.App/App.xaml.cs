using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using Dictator.App.Diagnostics;
using Dictator.App.Lifecycle;
using Dictator.App.NativeInterop;
using Dictator.Core;
using Microsoft.UI.Xaml;

namespace Dictator.App;

public partial class App : Application
{
    private readonly StartupReport report;
    private readonly LaunchOptions options;
    private readonly Stopwatch managedTimer;
    private Views.MainWindow? settings;
    private ResidentHost? host;
    private AudioBridge? audio;
    private bool refreshingMicrophones;
    private bool microphonesLoaded;
    internal string? AudioError { get; private set; }
    internal IReadOnlyList<MicrophoneChoice> Microphones { get; private set; } = [new("", "Windows default microphone")];
    private DispatcherTimer? events;
    private bool stopping;
    private double managedReadyMs;
    private double widgetReadyMs;
    private double? settingsActivationMs;
    private readonly Queue<string> recentErrors = new();
    private readonly PreviewInteraction preview = new();
    internal string? HotkeyError { get; private set; }
    internal bool RestartRequested { get; private set; }
    internal bool IsProbe => options.IsProbe;
    internal bool IsStopping => stopping;
    internal TaskCompletionSource<bool> ResidentReady { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    internal PreferencesStore Store { get; }
    internal Preferences Preferences { get; private set; } = new();
    internal WindowsStartup Startup { get; }
    internal string? SettingsError { get; private set; }
    internal string DiagnosticsText => report.DisplayText + $"""

        Windows build: {Environment.OSVersion.Version}
        Settings path: {Store.FilePath}
        Database path (not yet created): {Path.Combine(Path.GetDirectoryName(Store.FilePath)!, "dictator.db")}
        Managed Main entry to resident ready: {managedReadyMs:F2} ms
        Process creation to Widget shown (not a first-paint measurement): {widgetReadyMs:F2} ms
        Last Settings activation call duration (not first paint): {(settingsActivationMs.HasValue ? settingsActivationMs.Value.ToString("F2") + " ms" : "not activated")}
        Provider connection: not configured
        Current settings error: {SettingsError ?? "none"}
        Hotkey: {Preferences.Hotkey.Display}; registration: {HotkeyError ?? "ready"}
        Widget mode: real microphone capture (transcription not yet configured)
        Audio state: {audio?.Snapshot.State.ToString() ?? "unavailable"}; source sample rate: {audio?.Snapshot.SampleRate ?? 0}
        Captured frames: {audio?.Snapshot.CapturedFrames ?? 0}; dropped frames: {audio?.Snapshot.DroppedFrames ?? 0}
        Microphone error: {AudioError ?? "none"}
        Recent sanitized errors: {(recentErrors.Count == 0 ? "none" : string.Join(Environment.NewLine, recentErrors))}
        """;

    internal App(StartupReport report, LaunchOptions options, Stopwatch managedTimer)
    {
        this.report = report;
        this.options = options;
        this.managedTimer = managedTimer;
        Store = new(options.TestDataRoot ?? AppPaths.UserDataRoot);
        Startup = new(report.DistributionRoot, options.IsTest);
        if (!IsProbe) Preferences = Store.Load();
        SettingsError = Store.Error;
        if (SettingsError is not null) RememberError(SettingsError);
        InitializeComponent();
        UnhandledException += (_, args) =>
        {
            if (options.ReportPath is not null) StartupReport.WriteError(options.ReportPath, args.Exception);
        };
    }
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        // Create Settings quietly. Never Activate during normal startup/restart.
        settings = new Views.MainWindow(this);
        if (IsProbe)
        {
            settings.Closed += (_, _) => Exit();
            settings.DiagnosticsContent.Loaded += (_, _) => settings.DispatcherQueue.TryEnqueue(() =>
            {
                report.Write(options.ReportPath!, uiReady: true);
                if (options.Command == "--ui-smoke-test") settings.Close();
            });
            settings.Open();
            return;
        }
        host = new ResidentHost();
        ConfigureMicrophone();
        if (!host.BindHotkey(Preferences.Hotkey)) HotkeyError = "The Hotkey is already in use or could not be registered. Choose another combination.";
        OpenWidget();
        managedReadyMs = managedTimer.Elapsed.TotalMilliseconds;
        widgetReadyMs = (DateTime.UtcNow - Process.GetCurrentProcess().StartTime.ToUniversalTime()).TotalMilliseconds;
        events = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(20) };
        events.Tick += (_, _) =>
        {
            var pending = host?.PollEvents() ?? 0;
            if ((pending & 1) != 0) OpenWidget();
            if ((pending & 2) != 0) OpenSettings();
            if ((pending & 4) != 0) ScheduleShutdown(restart: true);
            if ((pending & 8) != 0) ScheduleShutdown(restart: false);
            if ((pending & 16) != 0)
            {
                HotkeyError = "The Hotkey could not be registered for this keyboard layout. Choose another combination.";
                settings?.RefreshPreferences();
            }
            if ((pending & 32) != 0) preview.Cancel();
            if ((pending & 64) != 0) {
                AudioError = DescribeAudioError(audio?.Snapshot.Error ?? 5);
                settings?.RefreshPreferences();
            }
            if ((pending & 128) != 0) _ = RefreshMicrophonesAsync();
            if (!stopping && host is not null)
            {
                var target = host.Target;
                preview.RefreshTarget(target.Eligible != 0 ? target.Token : 0);
                while (host.TryInput(out var input))
                {
                    if (input.Down == 1)
                        preview.Press((PreviewInput)input.Source, input.Timestamp,
                            input.Target == target.Token && target.Eligible != 0 ? target.Token : 0);
                    else if (input.Down == 2) preview.CancelInput((PreviewInput)input.Source);
                    else preview.Release((PreviewInput)input.Source, input.Timestamp);
                }
                host.SetPreview(preview.Talking ? preview.Target : 0);
                if (audio is null && preview.Talking) { preview.Cancel(); host.SetPreview(0); }
                if (audio?.Snapshot.State == 2 && AudioError is not null) { AudioError = null; settings?.RefreshPreferences(); }
            }
        };
        events.Start();
        _ = RefreshMicrophonesAsync();
        ResidentReady.TrySetResult(true);
        if (options.ReportPath is not null) File.WriteAllText(options.ReportPath, Snapshot());
    }
    internal void OpenWidget() { if (!stopping) host?.SetWidget(true, Preferences); }
    private void ConfigureMicrophone()
    {
        if (host is null) return;
        try {
            host.ConfigureAudio(Preferences.MicrophoneId);
            audio ??= new AudioBridge(host.AudioHandle);
            AudioError = null;
        } catch (InvalidOperationException) {
            AudioError = "Microphone capture could not initialize. Restart Dictator and check Windows audio services.";
        }
    }
    internal async Task RefreshMicrophonesAsync()
    {
        if (stopping || refreshingMicrophones || audio is null) return;
        refreshingMicrophones = true;
        try {
            var found = await audio.DevicesAsync();
            if (stopping) return;
            var choices = found.ToList();
            if (Preferences.MicrophoneId.Length != 0 && !choices.Any(x => x.Id == Preferences.MicrophoneId))
                choices.Add(new(Preferences.MicrophoneId, "Unavailable microphone (reconnect or select another)"));
            Microphones = choices; microphonesLoaded = true;
        } catch (InvalidOperationException) {
            if (!stopping) { AudioError = "The microphone list could not be loaded. Check Windows audio services, then refresh the list."; microphonesLoaded = true; }
        } finally {
            refreshingMicrophones = false;
            if (!stopping) settings?.RefreshPreferences();
        }
    }
    private static string DescribeAudioError(uint error) => error switch {
        1 => "No microphone is available. Connect one or choose an available microphone in Speech settings.",
        2 => "Microphone access was denied. Enable microphone access for desktop apps in Windows privacy settings.",
        3 => "The microphone changed or disconnected. Check the selection in Speech settings, then start Talking again.",
        4 => "This microphone's audio format is not supported. Choose another microphone or change its Windows audio format.",
        _ => "Microphone capture could not start or stopped unexpectedly. Check Windows audio services and the microphone selection."
    };
    private void OpenSettings()
    {
        if (stopping || settings is null) return;
        var timer = Stopwatch.StartNew();
        settings.Open();
        settingsActivationMs = timer.Elapsed.TotalMilliseconds;
    }
    internal void Save(Preferences candidate)
    {
        var rebound = false;
        try
        {
            candidate.Validate();
            if (Store.Error is not null) throw new InvalidOperationException(Store.Error);
            if (host is not null && (candidate.Hotkey != Preferences.Hotkey || HotkeyError is not null))
            {
                if (!host.BindHotkey(candidate.Hotkey))
                {
                    HotkeyError = "The Hotkey is already in use or could not be registered. Choose another combination.";
                    settings?.RefreshPreferences();
                    return;
                }
                rebound = true;
            }
            var microphoneChanged = candidate.MicrophoneId != Preferences.MicrophoneId;
            Store.Save(candidate);
            Preferences = candidate;
            if (microphoneChanged) { preview.Cancel(); ConfigureMicrophone(); _ = RefreshMicrophonesAsync(); }
            if (rebound) { HotkeyError = null; preview.Cancel(); }
            SettingsError = Store.Error;
            if (host is not null) host.SetWidget(IsWindowVisible(host.WidgetHandle) != 0, Preferences);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException or InvalidOperationException)
        {
            if (rebound && host is not null && !host.BindHotkey(Preferences.Hotkey))
                HotkeyError = "The previous Hotkey could not be restored. Choose another combination.";
            SettingsError = Store.Error ?? ("The preference could not be saved. " + error.Message);
            RememberError(SettingsError);
        }
        settings?.RefreshPreferences();
    }
    internal void ChangeStartup(bool enabled)
    {
        WindowsStartup.Registration? before = null;
        var changed = false;
        try
        {
            if (Store.Error is not null) throw new InvalidOperationException(Store.Error);
            before = Startup.Capture();
            Startup.Set(enabled);
            changed = true;
            // Commit settings only after the OS registration succeeds; roll back the
            // registration on persistence failure so both remain consistent.
            var candidate = Preferences with { StartWithWindows = enabled };
            Store.Save(candidate);
            Preferences = candidate;
            SettingsError = null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidOperationException or System.Security.SecurityException)
        {
            if (changed && before is not null)
                try { Startup.Restore(before); } catch (Exception rollback) when (rollback is IOException or UnauthorizedAccessException or System.Security.SecurityException) { }
            SettingsError = Store.Error ?? "Start with Windows could not be changed. " + error.GetType().Name;
            RememberError(SettingsError);
        }
        settings?.RefreshPreferences();
    }
    internal Task<string> DispatchAsync(string command)
    {
        var completion = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
        if (settings is null || !settings.DispatcherQueue.TryEnqueue(() =>
        {
            try
            {
                if (command == "open-settings") OpenSettings();
                else if (command == "startup") { }
                else if (options.IsTest)
                {
                    switch (command)
                    {
                        case "snapshot": break;
                        case "open-widget": OpenWidget(); break;
                        case "close-widget": host?.SetWidget(false, Preferences); break;
                        case "set-preferences": Save(Preferences with { Theme = AppTheme.Dark, WidgetZoom = 1.155, Hotkey = new(6, 0x77) }); break;
                        case "startup-on": ChangeStartup(true); break;
                        case "startup-off": ChangeStartup(false); break;
                        case "microphone-missing": Save(Preferences with { MicrophoneId = "Dictator.Missing.Test.Microphone" }); break;
                        case "microphone-default": Save(Preferences with { MicrophoneId = "" }); break;
                        case "restart": ScheduleShutdown(true); break;
                        case "quit": ScheduleShutdown(false); break;
                        default: throw new ArgumentException("Unknown lifecycle test command.");
                    }
                }
                else throw new ArgumentException("Unknown activation command.");
                completion.SetResult(Snapshot());
            }
            catch (Exception error) { completion.SetException(error); }
        })) completion.TrySetException(new InvalidOperationException("Resident dispatcher is unavailable."));
        return completion.Task;
    }
    private void RememberError(string message)
    {
        if (recentErrors.Count == 8) recentErrors.Dequeue();
        recentErrors.Enqueue($"{DateTime.UtcNow:O} {message}");
    }
    private string Snapshot()
    {
        var target = host?.Target ?? default;
        return JsonSerializer.Serialize(new {
        status = "ok", processId = Environment.ProcessId,
        settingsTitle = settings?.Title, settingsHandle = (long)(settings?.Handle ?? 0),
        settingsVisible = settings is not null && IsWindowVisible(settings.Handle) != 0,
        settingsWasActivated = settings?.WasActivated ?? false,
        widgetHandle = (long)(host?.WidgetHandle ?? 0),
        widgetVisible = host is not null && IsWindowVisible(host.WidgetHandle) != 0,
        previewTalking = preview.Talking, targetEligible = target.Eligible != 0,
        targetProcessId = target.ProcessId, targetForeground = (long)target.Foreground,
        targetFocus = (long)target.Focus, targetReason = target.Reason, targetStatus = target.Status,
        hotkeyError = HotkeyError,
        audioState = audio?.Snapshot.State ?? 0, audioError = AudioError,
        audioErrorCode = audio?.Snapshot.Error ?? 0,
        capturedFrames = audio?.Snapshot.CapturedFrames ?? 0,
        bufferedFrames = audio?.Snapshot.BufferedFrames ?? 0,
        droppedFrames = audio?.Snapshot.DroppedFrames ?? 0,
        microphonesLoaded, microphoneCount = Microphones.Count(x => x.Id.Length != 0 && !x.Name.StartsWith("Unavailable microphone", StringComparison.Ordinal)),
        trayReady = host?.TrayReady ?? false,
        preferences = Preferences, settingsError = SettingsError, startupEnabled = Startup.Enabled,
        settingsPath = Store.FilePath, managedReadyMs, widgetReadyMs
        });
    }

    private async void ScheduleShutdown(bool restart)
    {
        if (stopping) return;
        stopping = true;
        preview.Cancel(); host?.SetPreview(0);
        RestartRequested = restart;
        // Let the activation pipe acknowledge the request before Main disposes it.
        await Task.Delay(200);
        events?.Stop();
        host?.SetPreview(0);
        audio?.Dispose(); audio = null;
        host?.Dispose();
        host = null;
        // Application.Exit avoids depending on Close/Closed for a Window that has
        // never been activated. Settings close always means hide during residence.
        Exit();
    }
    internal void Cleanup()
    {
        stopping = true;
        events?.Stop();
        host?.SetPreview(0);
        audio?.Dispose(); audio = null;
        host?.Dispose();
        host = null;
    }
    [LibraryImport("user32.dll")]
    private static partial int IsWindowVisible(nint window);
}
