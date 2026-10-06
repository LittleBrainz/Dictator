# Dictator

Windows 11 x64 voice dictation assistant, built with C# 14/.NET 10, WinUI 3, and a
small C++23 native core. This fresh implementation currently covers **Phase 4**:
a resident tray host, interactive non-activating Widget, configurable global Hotkey,
editable-cursor gating, real microphone capture and waveform levels, plus Dictator Settings,
profile preferences, single-instance activation and self-contained distribution.
Speech settings offers Windows default microphone tracking or a fixed microphone.
Audio stays in a bounded memory buffer and is discarded as it is consumed.
OpenAI `gpt-live-transcribe` supplies streaming raw text and a final transcript.
Add your API key in Speech settings; it is stored in Windows Credential Manager.
Audio is sent to OpenAI while Talking. Dictator saves no recordings/transcripts.
Text insertion and rewriting begin in later phases.

Download **Dictator v0.4.0** from a successful Windows Actions run, extract once, and
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
acceptance; the user also accepted Phases 1, 2 and 3. Phase 4 requires the live transcription
acceptance matrix on Windows 11.
Normal startup shows the Widget and tray icon; use **Open Settings** from the tray
to configure the app, and **Quit** to exit. Closing Settings leaves Dictator running.
Only en-GB, en-US, fr-FR and zh-CN locale directories are shipped for testing.

The downloaded artifact is `Dictator v<version>.zip`, with its version taken from
the same metadata used by Diagnostics. A shared microphone icon brands the launcher,
application, Settings window and tray. Focus an editable text field, then use the
Hotkey (default Ctrl+Alt+backslash) or microphone: release before 500 ms to keep
Talking on, or hold at least 500 ms to stop on release. When already Talking,
release stops it. The waveform follows real microphone levels; silence remains a straight line.
Focus loss, closing the Widget, microphone changes and capture errors stop Talking.
The red inactive microphone has a stronger glow and no slash. Ticker and hover
scrolling are 1.5 times faster; the live text strip displays real raw transcription.
Stopping commits the audio; the latest final transcript is available in Speech
settings with a Copy button, in memory only. Focus loss while Talking cancels the session; after normal stop the final result
can finish in memory while Speech settings is open.
Network errors stop safely; another Talk gesture creates a fresh connection.
