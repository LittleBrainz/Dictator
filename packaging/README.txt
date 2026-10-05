Dictator - Phase 1 personal build

Extract the complete Dictator artifact once, then double-click Dictator.exe.
Windows 11 x64 is required. No separate .NET, Windows App SDK, or development
tools are needed. Dictator.exe is a small launcher; the application and runtimes
are unpacked under lib/WinUI. Startup does not extract them into Temp.
Keep the complete lib folder next to Dictator.exe.

Startup shows a small idle Widget and the Dictator notification-area icon.
Left-click the tray icon to reopen the Widget. Right-click it for Open Widget,
Open Settings, Restart Dictator, and Quit. Closing Settings leaves Dictator
running. Launching Dictator.exe again opens the existing Settings window.
The Widget's gear opens Settings; its close control dismisses only the Widget.

General provides Start with Windows, Theme, and the saved Hotkey preference.
Widget provides five fixed zoom stops. Hotkey interaction and dictation arrive
in later phases; no microphone capture or provider connection is active.

Preferences are saved in %USERPROFILE%\.dictator\settings.json, independent
of the installation folder. Invalid or unsupported settings files are preserved
and changes blocked. Repair or move that file and restart to recover.
Diagnostics includes build identity, paths, defined timings and a Copy button.
Only en-GB, en-US, fr-FR and zh-CN locale directories are currently shipped.

This build is unsigned. Windows security policy may block it; Smart App Control
compatibility is not established. If launch fails, share the error and
lib/build-info.json. Never include API keys or private transcript data.
