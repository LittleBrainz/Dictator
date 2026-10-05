Dictator - Phase 0 personal build

Extract the complete Dictator artifact once, then double-click Dictator.exe.
Windows 11 x64 is required. No separate .NET, Windows App SDK, or development
tools are needed. Dictator.exe is a small launcher; the application and runtimes
are already unpacked under lib/WinUI, so startup does not extract them into Temp.
Keep the complete lib folder next to Dictator.exe.

Persistent settings and local data belong in %USERPROFILE%\.dictator, independent
of the installation folder. Phase 0 shows this path in Diagnostics; settings and
history persistence are implemented in later phases.

This build opens Dictator Settings with Diagnostics. Dictation, the Widget,
Hotkey, and notification-area recovery are not available in Phase 0.
Closing Dictator Settings exits this skeleton build.

This is unsigned. Windows security policy may block it; Smart App Control
compatibility is not established.

If launch fails, record the error and share lib/build-info.json plus the
Diagnostics build identity. Never include API keys or private transcript data.
