Dictator - Phase 2 interaction preview

Download Dictator v0.2.1.zip and extract the complete artifact once, then double-click Dictator.exe.
Windows 11 x64 is required. No separate .NET, Windows App SDK, or development
tools are needed. Dictator.exe is a small launcher; the application and runtimes
are unpacked under lib/WinUI. Startup does not extract them into Temp.
Keep the complete lib folder next to Dictator.exe. The application and tray
use the same stylized microphone icon.

Startup shows a compact Widget and the Dictator notification-area icon.
Left-click the tray icon to reopen the Widget. Right-click it for Open Widget,
Open Settings, Restart Dictator, and Quit. Closing Settings leaves Dictator
running. Launching Dictator.exe again opens the existing Settings window.
The Widget's gear opens Settings; its close control dismisses only the Widget.

General provides Start with Windows, Theme and a configurable global Hotkey.
Focus an editable text field and press Ctrl+Alt+backslash, or the microphone.
Release before 500 ms to leave Talking on; hold at least 500 ms to stop on
release. If already Talking, pressing and releasing stops it. Losing the
eligible cursor stops the preview. Escape continues to the focused application.
The waveform is SIMULATED: no microphone, transcription or text insertion yet.

Hold the waveform body to drag. Position survives close/reopen in this process
and resets on startup or Restart. Widget has five zoom stops and tooltips after
a one-second hover. Hotkey/tray reopen a closed Widget even without a cursor.

Preferences are saved in %USERPROFILE%\.dictator\settings.json, independent
of the installation folder. Invalid or unsupported settings files are preserved
and changes blocked. Repair or move that file and restart to recover.
Diagnostics includes build identity, paths, defined timings and a Copy button.
Only en-GB, en-US, fr-FR and zh-CN locale directories are currently shipped.

This build is unsigned. Windows security policy may block it; Smart App Control
compatibility is not established. If launch fails, share the error and
lib/build-info.json. Never include API keys or private transcript data.
