# Verification through Phase 4

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

The `Phase4-test-results` artifact contains TRX and JSON evidence. The `Dictator v<version>`
artifact contains the application tree only, without a nested ZIP. CI checks do
not replace manual acceptance on the user's Windows desktop.

## Manual Windows 11 x64 lifecycle acceptance

1. Download **Dictator v0.4.0** from the passing run and extract once to a path with spaces.
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
validation. Hosted package checks attempt real capture and verify the explicit error path
when no microphone is available. No live transcription provider is used.

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

## Widget design refinement evidence

The native interaction test captures the actual HWND paint path in ready, Talking
and no-cursor states, plus inline hints and a fixture-fed ticker, under `artifacts/test-results/widget`. It checks cyan/red
quiet-line pixels with no vertical bars, taskbar clearance, bright-blue/white text pixels, transparent-region/input-hole checks, left microphone input
and right Settings/Close hit regions, in addition to existing focus/drag/zoom and
ownership regressions. The ticker contract test verifies elapsed-time motion,
append continuity, Unicode, bounded character/segment storage, pruning, reset
and the 180 ms whole-caption fade curve. Native checks cover the 1000 ms hover delay,
hidden/visible input regions, fixed full drag bounds, waveform-only dragging,
and actual per-pixel desktop compositing. A window-procedure observer records every applied drag
position against all four screen edges at every zoom stop, requiring one bounded
move per update instead of an out-of-bounds move followed by correction. The user's Windows desktop acceptance confirmed Phase 2
before this visual refinement; repeat appearance and taskbar placement below for
v0.4.0, including an auto-hidden taskbar and secondary monitors.

## Phase 2 manual Windows 11 acceptance

1. On fresh startup check the controls capsule and hidden upper strip, with a real desktop-visible gap when revealed,
   left microphone, top text strip and right Settings/Close buttons. The inactive microphone is
   red with stronger glow and no slash. With a valid cursor the waveform is a cyan straight line;
   without one it is a red straight line. Check additional clearance above the
   taskbar, including auto-hide. Focus Notepad and a browser text field. Tap the default Hotkey: green microphone
   and real waveform start on press and remain active after a short release.
   Tap again: stop on release, retaining focus and leaving text/selection unchanged.
2. From off, hold at least 500 ms: active while held, stop on release. Repeat using
   microphone click/click-hold. Test repeat and modifier release in different orders.
   Combine keyboard and mouse; one source must not finish the other's gesture.
3. Move focus to a button, read-only field, disabled field or unrecognized custom
   control: red microphone, no animation, no start. Lose eligibility or
   switch between eligible fields while active: capture stops. Exercise browser
   contenteditable and custom controls explicitly; they are not covered by the
   native Win32 fixture. Capture no target text in diagnostic evidence.
4. Close while active: cancel and dismiss only the Widget. Reopen from tray and
   Hotkey with no valid cursor, retaining the target application's focus. Escape
   and unrelated shortcuts must continue to work in that application.
5. Drag by the waveform body. Check each dedicated button does not drag. Close and
   reopen: dragged position survives. Drag hard against all four screen boundaries
   and the taskbar at every zoom: no brief off-screen paint or taskbar overlap.
   Restart and fresh startup reset position.
6. On multiple monitors with mixed DPI and negative coordinates, drag, change all
   five zoom stops, change taskbar/work area, and disconnect a monitor. Confirm
   the complete Widget scales and stays within the available work area.
7. Hover each control/waveform region: the upper capsule remains invisible for
   one second, then the whole capsule (rim/background and already-present text)
   fades in over 180 ms. Check very bright blue 12 DIP hints, a 24 DIP strip height,
   unchanged button sizes, and a 216 DIP total width. Long hints repeat without a
   blank pause, using "..." between repetitions. Leaving clears/hides the strip.
   Talking quickly reveals it with white raw text and suppresses hints. Changing
   visibility never changes the reserved 62 DIP drag bounds; drag the waveform
   to the top edge with the upper strip hidden, then reveal it and confirm it is
   fully on-screen. The upper strip must never initiate a drag. Test the invisible
   upper strip, gap and rounded corners for click-through without focus theft.
   Check light, dark and System Settings themes. Real raw text remains Phase 4;
   native fixture evidence exercises the ticker with owned sample input.
8. Rebind a free combination and a combination already registered by another app.
   Confirm conflicts are shown and the previous Hotkey/preferences survive. Test
   both UK and US layouts, including changing layouts while Dictator is resident.
9. Confirm Speech settings explains real capture and the next transcription phase.
   No provider connection, inserted text, audio/history files or transcript logs
   should appear. Record Windows build, run/commit and any unsupported controls.

Hosted tests do not establish browser/custom-control or physical mixed-DPI
acceptance; these checks must be completed on a Windows desktop.


## Phase 3 automated and manual microphone checks

Portable audio tests exercise float/PCM normalization, non-finite/clipped input,
stereo downmix, ring overflow/drop-new behavior, stale-session discard and
one-million-frame concurrent ordering. Windows native tests check ABI sizes,
argument/thread validation and repeated service/catalog lifetimes. Managed interop
tests run the production background catalog and consumer against the staged DLL,
joining both before host destruction. Core checks persist explicit microphone IDs,
old-file default tracking and invalid-ID rejection without overwriting settings.

The extracted package test attempts real WASAPI start. With an endpoint it checks
frame delivery, bounded buffering, tap/hold and eligibility stop. Without usable
hardware it requires a classified capture error, inactive state and unchanged
focus. In both cases it persists a deliberately absent microphone, verifies failure
without fallback, restores default selection and checks no-cursor recovery/Quit.
Hosted runners cannot establish physical microphone/device-change acceptance.

On the user's Windows 11 desktop, additionally:

1. In Speech settings refresh the list; select Windows default, then a specific
   microphone. Restart and confirm the selection persists. Enumeration and opening
   Settings must leave the microphone off.
2. Focus an editable field and start Talking with the Hotkey and microphone button.
   Speak/stop speaking: confirm real varying levels and a straight line in silence,
   preserved typing focus, prompt tap/hold stop and unchanged text. No transcription
   should be shown yet. Check the brighter unslashed red mic and faster hover loop.
3. While Talking change focus, make the target read-only, close the Widget, rebind,
   Restart and Quit. Confirm capture stops and no host remains after Quit. Repeat
   start/stop and a long Talk session; Diagnostics frame counters may rise, but
   audio storage must remain bounded and no recordings appear in `~/.dictator`.
4. With Windows default selected, change the Windows default recording device while
   Talking. Capture must stop with a useful notice; another gesture uses the new
   default. With an explicit device selected, an unrelated default change must not
   switch it. Disconnect/disable the selected device: capture stops safely, its
   missing selection remains visible, and restarting Talk fails until reconnected
   or another device is selected. Reconnect/refresh and verify recovery.
5. Disable desktop-app microphone access in Windows privacy settings and start Talk.
   Check the permission error and inactive mic; restore access and verify recovery.
   Check missing-device and unsupported-format errors where the hardware permits.
6. Drag/change zoom/hover controls throughout capture. Check responsiveness,
   taskbar/work-area bounds and no activation except explicitly opening Settings.
   Record the hardware, Windows build, run/commit and results without audio content.


## Phase 4 automated and manual transcription checks

Core tests use a loopback WebSocket protocol fixture with the production
ClientWebSocket client. They verify effective session configuration before audio,
raw Unicode deltas before commit, fragmented responses, authoritative final text,
short-turn padding, streamed resampling and tail flush, cancellation, queue bounds
and wiping, unsupported/missing acknowledgment, interrupted connections, HTTP
401/403/429 and server authentication/quota/rate/model errors. Error responses
containing fake secret/transcript text must never echo those fields. DSP checks
cover 8/16/24/44.1/48/192 kHz chunk continuity, tail counts, NaN handling and
anti-alias filtering. Native interaction checks enforce presentation epoch guards,
ticker backpressure clear and graceful stop without focus theft. Interop tests
write/read/delete a uniquely named test credential through production WinCred calls.

The extracted package uses an isolated empty credential target. An eligible Talk
gesture must show an actionable missing-key error before opening the microphone,
preserve focus and leave target text/selection unchanged. It also checks microphone
preference persistence, no-cursor recovery and Quit. This replaces Phase 3's
no-key capture attempt because a provider credential is now required. Native audio
contracts remain covered independently. No CI test reads a personal key, contacts
OpenAI, sends real microphone audio or saves recordings. Loopback fixture success
cannot establish real OpenAI account/model access or physical microphone quality.

On Windows 11:

1. Enter a valid OpenAI API key under Speech and Save. The password field must clear,
   credential status must show stored, and restart must preserve that status without
   putting a key in `~/.dictator/settings.json` or Diagnostics. OpenAI API billing
   is separate from ChatGPT. Remove must erase the credential; Talking with no key
   must fail before capture and preserve target focus. Re-enter to continue.
2. Focus a writable field, Talk and speak English, French and Chinese as appropriate.
   Check real waveform levels and smoothly scrolling white raw text in the spoken
   language. Stop normally; wait for Completed before opening Speech. Inspect/copy
   the authoritative final transcript. No text is inserted into the target yet.
3. Test tap/hold with Hotkey and mic, silence, a very short phrase, rapid stop/start,
   and long speech. Last audio must finalize cleanly, storage stays bounded, and old
   deltas/finals must never appear in the newly started session, even in the same
   field. Full final text is separate from ticker content that has scrolled away.
4. Lose/change cursor eligibility, close the Widget, change mic/Hotkey, Restart and
   Quit while connecting, talking and finalizing. Check immediate capture stop,
   cancelled late events, responsive Widget, preserved focus and no lingering host.
5. Test an invalid key, an account without credits/access, network loss and restoration.
   Check safe inactive state and useful errors; no partial result is inserted or
   promoted to a final transcript. Another Talk gesture must create a fresh working
   connection after the problem is fixed. Check no credential, transcript content
   or audio appears in diagnostics, IPC evidence, settings or files.
6. Repeat microphone default-switch/disconnection and desktop privacy-denial checks
   from Phase 3 with transcription active. Record account/model access, run/commit,
   Windows build and results without sharing the key, audio or private transcript.
