# Dictator

Windows 11 x64 voice dictation assistant, built with C# 14/.NET 10, WinUI 3, and a
small C++23 native core. This fresh implementation currently covers **Phase 2**:
a resident tray host, interactive non-activating Widget, configurable global Hotkey,
editable-cursor gating and simulated waveform preview, plus Dictator Settings,
profile preferences, single-instance activation and self-contained distribution.
Audio capture, transcription and text insertion are not implemented yet.

Download **Dictator v0.2.5** from a successful Windows Actions run, extract once, and
launch the small root `Dictator.exe` launcher. The full self-contained application
and runtimes stay under `lib/WinUI`; startup does not extract them into Temp. No
separate .NET, Windows App SDK, or development stack is required. Persistent user
data belongs in `%USERPROFILE%\.dictator` (`~/.dictator`), independently of the
installation folder. The current build is unsigned; Windows policy may block it.

- [Specification](docs/SPECIFICATION.md)
- [Architecture and verified platform baseline](docs/ARCHITECTURE.md)
- [Automated verification and manual Windows acceptance](docs/TESTING.md)
- [Agent/build instructions](AGENTS.md)

The complete hosted Windows build is `./packaging/scripts/Build.ps1`. GitHub
Actions on `windows-2025` is authoritative. Phase 0 passed Windows CI and manual
acceptance; the user also accepted Phase 1. Phase 2 requires the manual interaction
acceptance matrix on Windows 11.
Normal startup shows the Widget and tray icon; use **Open Settings** from the tray
to configure the app, and **Quit** to exit. Closing Settings leaves Dictator running.
Only en-GB, en-US, fr-FR and zh-CN locale directories are shipped for testing.

The downloaded artifact is `Dictator v<version>.zip`, with its version taken from
the same metadata used by Diagnostics. A shared microphone icon brands the launcher,
application, Settings window and tray. Focus an editable text field, then use the
Hotkey (default Ctrl+Alt+backslash) or microphone: release before 500 ms to keep
Talking on, or hold at least 500 ms to stop on release. When already Talking,
release stops it. The waveform is simulated; no microphone is opened.
