# Verification through Phase 2

Phase 0 passed Windows CI and the user accepted the revised app-local artifact.
Phase 1 adds the following automated and manual lifecycle checks.

## Automated checks

`packaging/scripts/Build.ps1` is the authoritative Windows x64 build. It restores
locked packages, builds C++23, runs native/Core tests, publishes self-contained
WinUI, stages the distribution, and runs interop tests against its native DLL.

`Test-Package.ps1` extracts the staged tree once into a path with spaces and Unicode,
uses a different working directory and invalid external .NET paths, and forbids
runtime extraction. It checks root cleanliness, build/runtime/ABI identity,
UTF-16 and struct round-trips, actual XAML readiness, detached launcher behavior,
and useful missing-dependency errors. The distribution contains only en-GB,
en-US, fr-FR and zh-CN culture folders. Real WinUI launches run with each retained
language and de-DE to exercise fallback after its folder has been removed.

`Test-Lifecycle.ps1` starts the extracted resident application with a scratch
profile and a separate reversible startup registry value. It verifies:

- normal startup: Widget and tray ready, Settings created but never activated;
- duplicate startup stays quiet in the same process;
- Quit with Settings never activated exits cleanly and destroys the Widget HWND;
- a second manual launch activates existing Settings;
- the real WM_CLOSE message hides Settings and preserves the Widget/tray/process;
- dismissing the Widget and reopening it preserves foreground focus;
- all preference fields persist, and Start with Windows writes/removes the quoted
  launcher command under HKCU Run without elevation, and restores a previous
  registration and settings after a real file-sharing write failure;
- Restart exits the old process, creates a new one, restores preferences and leaves
  Settings hidden with Widget visible;
- future-schema settings errors are surfaced and the file is preserved even when
  a preference change is attempted.

The registry value is restored and the resident process cleaned up in `finally`.
Internal smoke flags isolate test settings and do not touch the runner's real
`%USERPROFILE%\.dictator`. Production launches always use that known profile path.
Core tests also check malformed/unsupported settings, reserved Hotkeys, defaults,
atomic replacement and no temporary-file residue. ABI tests cover error codes,
10,000 ownership cycles, Unicode/embedded NUL, and cross-thread consumer polling.

The `Phase2-test-results` artifact contains TRX and JSON evidence. The `Dictator v<version>`
artifact contains the application tree only, without a nested ZIP. CI checks do
not replace manual acceptance on the user's Windows desktop.

## Manual Windows 11 x64 lifecycle acceptance

1. Download **Dictator v0.2.0** from the passing run and extract once to a path with spaces.
   Keep `lib` beside the root launcher. Use no separately installed runtimes.
2. Launch `Dictator.exe`. Confirm only the idle Widget and notification-area icon
   appear; Settings must remain hidden. Confirm the microphone icon in Explorer,
   the tray and the Settings title bar/taskbar. Left-click the icon to show the Widget.
3. Check the right-click menu exactly: **Open Widget** (default), **Open Settings**,
   separator, **Restart Dictator**, **Quit**. Check the Widget gear opens Settings
   and its close control only dismisses the Widget.
4. With a text editor focused, reopen a dismissed Widget from the tray. Confirm
   the editor retains keyboard focus. Phase 2 adds interaction; real dictation comes later.
5. Open Settings. Confirm the exact title **Dictator Settings** and navigation:
   General, Widget, Speech, AI / Models, Dictionary, Phrase Shortcuts, History,
   Applications, Diagnostics. Close Settings: Widget and tray must stay available.
6. Launch the root EXE again. Confirm it opens the existing Settings and creates
   no second resident host. Launch with `--startup` while Settings is hidden;
   confirm the duplicate stays quiet.
7. Change Theme through System, Light and Dark, Hotkey through a valid combination,
   and Widget Zoom through all five stops. Confirm only one Hotkey is configured;
   Ctrl+Alt+Space is rejected, and default is Ctrl+Alt+backslash. Hotkey activation
   is now registered, and conflicts must be surfaced without losing the old binding.
8. Enable Start with Windows. Confirm HKCU Run contains a quoted root launcher path
   plus `--startup`; disable it and confirm removal, without elevation. If testing
   sign-in, enable again and confirm Widget/tray startup with Settings hidden.
9. Restart from the tray. Confirm the old process ends, a single new host starts,
   preferences persist, Widget is visible and Settings hidden. Quit directly
   without opening Settings in this process; it must end promptly. Repeat after
   opening and closing Settings. Confirm no lingering host or visible Widget.
10. Diagnostics must show build/ABI identity, Windows build, profile settings and
    future database paths, precisely defined timings, sanitized errors and provider
    status. Use Copy and inspect the clipboard; no credentials/transcripts belong there.
11. Optional recovery check: Quit, back up `~/.dictator/settings.json`, replace it
    with malformed or future-schema JSON, then launch and open Settings. Confirm
    an error, disabled preference controls and unchanged file. Restore the backup
    and restart to recover.
12. Record the run, commit, Windows build and results. The build is unsigned; record
    any Windows policy block without disabling policy as an undocumented step.

## Supplementary Cloud checks

Core and portable native/interop tests run on Linux with the pinned .NET SDK and
C++23 compiler. They are contract coverage, never a substitute for Windows package
validation. No microphone or live provider session is used in normal CI.

## Phase 2 automated interaction coverage

Core tests cover the exact 499/500/501 ms boundary for both sources, already-on
release, repeated press, mixed input sources and invalid/changed target cancellation.
`Dictator.Interaction.Tests` launches a separate Win32 target process and exercises
real SendInput with writable, read-only, disabled and non-text controls. It checks
UK/US backslash, repeat suppression, releasing modifiers before the main key,
reservation conflicts, Escape and unrelated-key delivery, recovery without a valid
cursor, source-specific microphone edges, drag/zoom/close/reopen, unchanged target
text/selection and foreground focus. Target text reads are confined to the test
fixture's own verification, never the production eligibility implementation.

`Test-Preview.ps1` runs the extracted WinUI package and actual default global Hotkey
against that fixture. IPC snapshots verify tap toggle, hold release, eligibility
loss, no-cursor suppression/recovery and clean Quit. JSON evidence is saved beside
the lifecycle evidence. The fixture executable is not shipped to users.

## Phase 2 manual Windows 11 acceptance

1. Focus Notepad and a browser text field. Tap the default Hotkey: green microphone
   and simulated waveform start on press and remain active after a short release.
   Tap again: stop on release, retaining focus and leaving text/selection unchanged.
2. From off, hold at least 500 ms: active while held, stop on release. Repeat using
   microphone click/click-hold. Test repeat and modifier release in different orders.
   Combine keyboard and mouse; one source must not finish the other's gesture.
3. Move focus to a button, read-only field, disabled field or unrecognized custom
   control: red/slashed microphone, no animation, no start. Lose eligibility or
   switch between eligible fields while active: preview stops. Exercise browser
   contenteditable and custom controls explicitly; they are not covered by the
   native Win32 fixture. Capture no target text in diagnostic evidence.
4. Close while active: cancel and dismiss only the Widget. Reopen from tray and
   Hotkey with no valid cursor, retaining the target application's focus. Escape
   and unrelated shortcuts must continue to work in that application.
5. Drag by the waveform body. Check each dedicated button does not drag. Close and
   reopen: dragged position survives. Restart and fresh startup reset position.
6. On multiple monitors with mixed DPI and negative coordinates, drag, change all
   five zoom stops, change taskbar/work area, and disconnect a monitor. Confirm
   the complete Widget scales and stays within the available work area.
7. Hover each hit region for one second and compare copy exactly with specification
   section 14.8. Moving regions restarts the delay. Visible content updates on
   eligibility/Talking changes; real divider, actual configured Hotkey, no position
   flash, no focus theft. Check light, dark and System themes.
8. Rebind a free combination and a combination already registered by another app.
   Confirm conflicts are shown and the previous Hotkey/preferences survive. Test
   both UK and US layouts, including changing layouts while Dictator is resident.
9. Confirm Settings explains the simulated preview. No microphone permission,
   provider connection, inserted text, audio/history files or transcript logs
   should appear. Record Windows build, run/commit and any unsupported controls.

Hosted tests do not establish browser/custom-control or physical mixed-DPI
acceptance; these checks must be completed on a Windows desktop.
