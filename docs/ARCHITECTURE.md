# Architecture through Phase 4

The v2 specification is in `SPECIFICATION.md`, updated with the user's explicit
6 October 2026 deployment and user-data requirements.
Phase 0 established the build and deployment chain and passed user acceptance.
Phase 1 adds resident lifecycle, a native tray and Widget shell, Settings and
preferences; the user accepted both phases. Phase 2 adds global input, cursor
eligibility and simulated interaction. Phase 3 adds native microphone capture
and real levels. Phase 4 adds managed streaming transcription and secure API
credentials. Insertion, rewriting and history remain future work.

`Dictator.App` is an unpackaged C# 14/.NET 10 WinUI 3 executable. It creates
**Dictator Settings** quietly and keeps the Widget and tray available until Quit.
`Dictator.Core` contains UI-independent build identity, preferences and preview gesture logic. `Dictator.Native` is a C++23 DLL with ABI 1.
Core and interop tests run through xUnit; native tests run through CTest.
The interop tests compile the production interop source, without referencing WinUI.

## Stable versions checked at bootstrap

On 5 October 2026, the Microsoft .NET 10 release metadata reported SDK **10.0.401**
and runtime **10.0.12**. NuGet's Microsoft.WindowsAppSDK stable package is **2.5.1**.
The published `windows-2025` image manifest listed Windows SDK **10.0.26100.0** and
Visual Studio 2022 **17.14**, but the actual Actions runner already has stable
**Visual Studio 2026 (18.x)** and VC145. The build discovers the installed stable
Visual Studio instance, its matching CMake generator/redistributable, and the latest
installed stable Windows SDK; the actual selected SDK is recorded in metadata.
The managed project targets the 26100 API contracts with a Windows 11 22000 minimum.
No Preview/RC toolchain or SDK installer is required on the user's PC. The observed
runner, rather than an outdated image manifest, is the build authority.

Sources:

- https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json
- https://api.nuget.org/v3-flatcontainer/microsoft.windowsappsdk/index.json
- https://learn.microsoft.com/windows/apps/windows-app-sdk/release-notes/windows-app-sdk-2-0
- https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md

## ABI and ownership

`src/Dictator.Native/include/dictator_native.h` is the contract. It exports only C
functions, fixed-width integers, opaque handles, and fixed-layout metadata structs.
`LibraryImport` declares explicit cdecl calls. `NativeContext` owns exactly one
handle through SafeHandle. No C++ class, STL object, managed callback, or exception
crosses the boundary. UTF-16 lengths count code units; output includes a NUL and
remains caller-owned. Embedded NUL and surrogate pairs survive a round-trip.

Polling is synchronous on the caller's non-real-time consumer thread. Calls for
one handle are serialized. The snapshot records both its creation and polling
thread; tests prove polling from a second thread. The Phase 0 probe creates no callback thread. The resident surface has its own
UI-thread input/window callbacks and a metadata-only accessibility worker.
Balanced live-handle counts establish simple allocation-cycle ownership; this
does not claim a comprehensive operating-system memory leak audit.

## Supported distribution

```text
Dictator.exe                    # small native launcher, static CRT
README.txt
lib/
  WinUI/
    Dictator.App.exe             # managed application
    Dictator.App.dll
    <complete app-local .NET / Windows App SDK / VC runtime and resources>
    en-GB/
    en-US/
    fr-FR/
    zh-CN/
  Native/Dictator.Native.dll
  build-info.json
```

On 6 October 2026 the user reported a roughly 15-second first start with the
self-extracting executable, followed by roughly 0.5-second subsequent starts.
The accepted replacement is a small native launcher at the root and ordinary
self-contained folder publishing under `lib/WinUI`. `PublishSingleFile=false`;
trimming and Native AOT remain disabled. GitHub's artifact ZIP is extracted once.
No application or runtime payload is extracted during application startup.

The entire supported publish tree stays beside the managed host, including neutral
PRI resources and the four user-requested locales (en-GB, en-US, fr-FR, zh-CN).
The other culture directories are removed at staging, without changing neutral
PRI resources. Actual XAML launch checks cover all four languages and de-DE
fallback after its locale directory has been removed. No individual SDK DLLs are moved away from the
host, no PRI files are rewritten, and no WinUI DLL search paths are customized.
The SDK's registration-free automatic initializer owns runtime/resource loading.
Both .NET and Windows App SDK are self-contained. Microsoft's native modules use
the app-local VC redistributable and Microsoft.VCRTForwarders.140; our launcher
and native DLL use the static CRT. No separately installed runtime is required.

- https://learn.microsoft.com/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps

The launcher discovers its own absolute path with `GetModuleFileNameW`, calls
`CreateProcessW` with the absolute `lib/WinUI/Dictator.App.exe` path, and forwards
the original argument tail verbatim. It changes neither cwd nor runtime search
paths. It closes its handles and exits immediately on normal launch; only the
managed host remains. This short-lived launch process is an explicit exception to
having just one process at startup, not a resident helper/service architecture.
For CI smoke arguments only, it waits and returns the host's exit code. Missing
host/runtime files or process-creation failures produce an actionable launch
error; smoke mode returns nonzero without a modal dialog.

`AppPaths.DistributionRootFromHost` resolves the installation root from the known
`lib/WinUI` layout. The managed native resolver loads Dictator's DLL by absolute
path from `lib/Native`. Cwd is never used for dependency discovery. Diagnostics
reports the installation root and actual app-local runtime directory.

`build-info.json` records product, Git SHA, CI run, configuration, architecture,
SDK/runtime versions and ABI. Startup verifies product/runtime/ABI identity before
showing Diagnostics. Missing native files and incompatible metadata produce an
error; failures preceding managed entry may be surfaced by Windows/.NET itself.

## Persistent user data

The user's explicit requirement supersedes the original `%LOCALAPPDATA%\Dictator`
data location. `AppPaths.UserDataRoot` resolves the user's profile known folder
and appends `.dictator`, giving `%USERPROFILE%\.dictator` (`~/.dictator`). It does
not depend on install location, cwd, or a future installer. Settings, the local
database/history, logs and any sanitized crash data will live there. Installation
under `%LOCALAPPDATA%\Programs\Dictator` may be added later for binaries only.
API keys still belong in Windows Credential Manager.

Preferences use schema 1 in `settings.json`: Start with Windows, Theme, Hotkey
and Widget Zoom, plus optional MicrophoneId (empty means follow Windows default). Writes flush a unique temporary file before atomic replacement.
Invalid, inaccessible or future-schema files are preserved and lock preference
changes for that session. Recovery requires repairing or moving the file and
restarting. A fresh launch does not create a config until a preference changes.
There is no history/database or obsolete-prototype migration yet.

## Repository and acceptance status

The existing connected repository is `LittleBrainz/Dictator`; it was public at
bootstrap. No repository was recreated and its visibility was not changed.
The specification's private-repository requirement remains an external setup
decision. Source changes alone cannot establish clean-PC compatibility or trust
under Smart App Control. The build remains unsigned. Phase 0 passed authoritative CI and the user confirmed its manual Windows 11
launch. Phase 1 requires the lifecycle acceptance matrix in TESTING.md.

## Resident lifecycle

C# owns settings, startup registration and lifecycle decisions. A per-user/session
named mutex establishes the resident instance; a current-user-only named pipe
redirects activation. A second manual launch opens existing Settings. `--startup`
and `--restart` leave it quiet. Requests and connections have bounded deadlines;
no network service or elevated helper is involved. Mutex ownership stays on the
main STA thread. The pipe is cancelled and mutex released before restart launches
the same absolute root executable.

The additive ABI 1 `dictator_host` owns a hidden native tray owner HWND, branded
tray icon and non-activating Widget HWND. Calls and destruction run on the WinUI
UI thread; messages use its existing pump. A small event bitset is polled every
50 ms, without managed callbacks. Tray menu: Open Widget (default), Open Settings,
separator, Restart Dictator, Quit. Left click opens the Widget. The icon is restored
on Explorer's TaskbarCreated message. The Widget shell shows Idle, opens Settings
with its gear, and dismisses with its close control. No input gesture, audio or
dictation implementation is included.

Settings is constructed without activation. Its close request is cancelled and
the AppWindow hidden. Quit stops polling, removes the tray icon, destroys native
HWNDs and drawing resources, then uses Application.Exit rather than depending on
Closed for a never-activated WinUI window. This is verified from the actual package.
Windows startup uses HKCU Run with a quoted absolute launcher path and --startup;
changes roll back OS registration if preference persistence fails. The UI displays
the actual registration, including external changes.

Internal smoke flags bypass or isolate residence. Lifecycle smoke uses an explicit
scratch data root and a separate startup registry value, restores that value, and
never writes the runner's real .dictator settings. Product launches always resolve
the known profile path. Internal pipe commands for testing are accepted only by
a resident instance launched with that test flag.

## Icon and artifact identity

`assets/Dictator.png` is the source microphone artwork. `assets/Dictator.ico`
contains 16, 24, 32, 48, 64, 128 and 256 pixel variants of that same image. The
launcher and native DLL embed the ICO through a shared resource template; the
managed host embeds it through ApplicationIcon. Settings uses the published
`Assets/Dictator.ico` via AppWindow.SetIcon. The native tray loads its owned icon
from the DLL resource and releases it with DestroyIcon.

The Actions artifact is `Dictator v<version>`, so its downloaded ZIP includes the
version. The name comes from staged build-info.json, whose product version comes
from Directory.Build.props. The application root still contains only Dictator.exe,
README.txt and lib. Global Hotkey interaction is included in Phase 2.

## Phase 2 input and Widget

C# `PreviewInteraction` owns the 500 ms gesture rules and active target token.
Monotonic timestamps captured at the input edge define tap versus hold; dispatcher
and provider latency cannot reset the boundary. Repeat is ignored, a source owns
its release, and eligibility/identity changes cancel active state.

The native surface reserves the combination using RegisterHotKey (including
conflict detection), and a low-level keyboard hook captures press/release while
consuming only the matching key gesture. Modifier release order does not change
the main-key release. Escape is never registered or consumed. The default
backslash resolves against the focused application's keyboard layout, including
UK OEM102 and US OEM5. A schema-1 optional `layoutBackslash` flag defaults to true
for compatibility with Phase 1 preferences; manually captured bindings set it
false so UK Ctrl+Alt+# remains distinct from the default backslash. A failed rebind preserves the old reservation; a settings
write failure attempts to restore it and reports any rollback failure.

A dedicated MTA worker polls UI Automation metadata every 60 ms and again for
each input edge. Cache requests contain only process, focus, enabled and pattern
availability/read-only properties; no Name, Value.Value, transcript, or target
text is fetched. ValuePattern must be writable. Document/contenteditable targets
may use TextPattern2's active caret range and read-only attribute. Unrecognized
providers fail closed. Runtime ID, foreground/focus HWND, process, executable key
and monitor identify the target. Target identity is never written to diagnostics.
Provider connection/transaction timeouts are 200 ms; the UI never waits on a
provider call. Bounded queues carry metadata and timestamped edges back to C#.
Worker shutdown joins before the host's windows are destroyed.

The UI thread additionally rejects changed foreground/focus HWND or metadata older
than 400 ms. Native rendering stops immediately when a new invalid or changed
target snapshot arrives; C# cancels its gesture on the next 20 ms dispatcher tick.
No microphone, network request, insertion API or audio buffer is introduced.

The native Widget uses WS_EX_NOACTIVATE and MA_NOACTIVATE, SWP_NOACTIVATE
placement, and do not call SetForegroundWindow during ordinary interaction. Tools
is the explicit exception that asks C# to activate Settings. A 216 by 62 DIP
Widget scales with DPI and the five persisted zoom stops. Body dragging is kept
only in native host memory, clamped to monitor work areas and retained through
close/reopen. Default placement is bottom-center, raised by half the scaled height
plus the normal margin and half the relevant bottom taskbar height. Primary and
secondary taskbar bounds are queried; auto-hidden bottom taskbars are reserved
before positioning. A DPI-scaled 24 DIP clearance is the fallback. Dragged
positions are preserved, subject to work-area/taskbar clamping.
Drag requests are clamped before the single SetWindowPos call. The destination
monitor is selected from the proposed rectangle without moving the HWND first,
so cross-monitor dragging retains DPI handling and never paints a speculative
off-screen/taskbar-overlapping location. Display/setting/DPI changes re-clamp
placement.

The Widget renders two glossy capsules into premultiplied GDI+ surfaces and
presents a per-pixel layered HWND with `UpdateLayeredWindow`. Its rounded region
excludes the 1 DIP gap/corners and, while idle, the entire upper capsule. The
underlying application remains visible and receives clicks through those areas.
The full 216 by 62 DIP HWND rectangle remains reserved for placement and clamping,
independent of caption visibility. Layout is 24 DIP text, 1 DIP gap and 37 DIP
controls. The 20% width reduction shortens the waveform and moves Settings/Close
left; button/glyph sizes remain unchanged. Only the waveform starts a drag.

The complete upper capsule is invisible without Talking or a hover hint. After
one-second hover, its background, border and already-present bright-blue (#70DEFF)
12 DIP text fade in together over 180 ms. Talking reveals the same capsule with
white raw text. Each presentation change preserves the full clamped rectangle;
inactive content hides the capsule and clears its input region. The controls
remain opaque throughout. Long hints repeat continuously with a measured spaced
"..." delimiter between repetitions; the middle dot still joins instructions
within each message. Region changes/leave/drag/press clear hints; Talking
suppresses them. Hover instructions are exposed as the Widget accessibility name;
live transcript text is never copied into window titles or diagnostics.

An additive ABI 1 UI-thread `dictator_host_append_live_text` / managed
`ResidentHost.AppendLiveText` bridge accepts future raw UTF-16 transcription
deltas for the active target and presentation epoch (snapshotted at start),
rejecting late text even when Talking restarts in the same field. It returns an error on inactive/stale targets or
full queues, copies borrowed input, and retains at most 4096 code units in 128
segments (2048 units per call). Off-screen segments are pruned. Appending preserves
existing positions; motion is 96 DIP/s from right to left with white text, updated
by the native 16 ms UI timer. Stop/focus loss/close/rebind clears the queue. Neither
audio/provider operations nor transcript logging/persistence are introduced.
Production Phase 2 supplies no text; only the owned test fixture exercises sample
deltas. The user's "translated" label means speech-to-text in the spoken language,
without subsequent formatting. Real transcription is still Phase 4.


## Phase 3 microphone capture

The native host owns an opaque `dictator_audio` service. Configuring it initializes
an MTA worker and endpoint notifications, without opening a microphone. Speech
settings enumerates active capture endpoints on a background managed task and
persists either an endpoint ID or an empty string for Windows default (console
role). An absent selected endpoint stays selected and fails safely; it never
silently falls back. Enumeration/refresh does not start capture.

A valid Talk gesture binds the target identity and requests shared-mode,
event-driven WASAPI capture. The worker owns COM/audio objects and uses MMCSS Audio
priority when available. Device sample rates from 8–192 kHz, up to 32 channels,
PCM 8/16/24/32-bit and float32 are normalized/downmixed to mono float32 at the
source rate. Resampling for a transcription provider belongs to the next phase.
Silence, clipped/non-finite samples and unsupported formats have explicit handling.

The normal packet path uses 1024-frame stack scratch, lock-free level/counter
atomics and a fixed 131072-frame (~512 KiB) single-producer/single-consumer ring.
At most eight packets are processed per event before control/shutdown is checked.
It makes no managed callbacks and performs no UI, disk, network, logging,
allocation or mutex acquisition. Overflow drops incoming frames and counts them;
it cannot grow the ring or block capture. Metadata/catalog operations and stream
setup/error cleanup run outside that packet path.

A background managed consumer reuses one 4096-frame array, reads every 20 ms and
clears/discards chunks. Consumed ring slots are zeroed before reuse. Session epochs
invalidate pending old audio on stop/restart; idle reads purge stale slots. UI
rendering samples coalesced RMS and retains a fixed 25-level history. The active
waveform responds to real sound; silence and inactive states use a straight line.
No sample text, provider, recorder, transcript persistence or audio files exist.

Commands are bounded and asynchronous. Every 20 ms or packet wake the native
worker independently checks foreground/focus HWND, the probe's eligible token
and its 400 ms freshness limit. It stops capture on lost/changed eligibility even
if managed dispatch is stalled. Widget close, gesture release/toggle, rebind,
selection changes and Quit also stop capture. Endpoint notifications interrupt a
removed selected device or a changed followed default and require another gesture;
they do not transfer a live session to a new microphone. Native error metadata
cancels Talking, shows a tray notice without activation and supplies Settings with
a useful message. The managed consumer and catalog task join before native host
and worker destruction; the probe outlives the worker's borrowed atomic guards.

The inactive red microphone has no diagonal slash and a stronger red glow. Live
text and long hint motion are 96 and 36 DIP/s respectively, 1.5 times v0.2.6.
The accepted geometry, conditional strip, bright-blue 12 DIP hints, transparent
1 DIP gap, waveform-only dragging and full-height work-area bounds remain.


## Phase 4 streaming transcription

The provider-neutral Core contract separates PCM delivery, session state, live
deltas, final text, cancellation and sanitized errors from OpenAI event names.
The production provider uses .NET ClientWebSocket with normal TLS validation and
Bearer authentication at `wss://api.openai.com/v1/realtime?intent=transcription`.
The centrally configured model is `gpt-live-transcribe` with low delay, verified
against the official guide on 6 October 2026. Session updates explicitly request
transcription, PCM16 mono at 24 kHz and null turn detection. This model streams
before commit and does not support server/semantic VAD. A Talk session is one
turn; releasing/toggling off commits it. No translation prompt or forced language
is sent. Effective configuration is acknowledged before any audio is transmitted.

Official sources:

- https://developers.openai.com/api/docs/guides/realtime-transcription
- https://developers.openai.com/api/docs/models/gpt-live-transcribe
- https://developers.openai.com/api/docs/guides/voice-websockets?api=realtime

A managed windowed-sinc low-pass resampler retains fixed history and phase across
chunks, converts the native source rate to 24 kHz PCM16 and flushes its small tail.
It runs only on the existing non-RT managed consumer. The outbound queue holds at
most 256 chunks (48000 bytes each maximum); overflow fails rather than blocking
capture or silently losing audio. There is no audio retry/replay cache. Successful
transfer owns the PCM array and clears it after send/discard; JSON wire buffers,
resampling history and native scratch/consumed slots are also cleared. Base64 and
text strings remain transient managed strings; no audio/transcript file is created.

Normal Talk stop closes the device on the native worker but preserves its bounded
final chunks in a draining state. The same consumer reads them and flushes the
resampler before completing the provider queue and committing the turn. Once the
ring empties, native state becomes idle. This avoids UI/network waits and clipping
the last queued audio. Focus loss, errors, close, rebind, device changes, restart
and Quit retain immediate invalidation/purge behavior. A new capture epoch rejects
old native chunks; a new presentation epoch rejects old text even in the same field.

The provider bounds each response to 256 KiB, total transcript text to 65536 UTF-16
units and its display queue to 128 events, splitting deltas without breaking surrogate
pairs. A bounded UI drain coalesces text into the existing native ticker. Display
backpressure clears only old ticker content; the authoritative final text remains
complete in its separate bounded result. The upper strip retains its accepted
visibility/geometry. Normal stop hides it; Speech settings offers the latest final
transcript and explicit Copy, in memory only. It clears on the next successful Talk
start and disappears on Quit. Opening Settings while still capturing/finalizing
loses target eligibility and cancels that pending session; no insertion occurs.

Connect/configuration acknowledgment, send and finalization timeouts are 10/10,
5 and 15 seconds respectively. Connection failure closes capture, keeps the Widget
available and shows a non-activating tray notice and Settings error. A later gesture
creates a new connection; no automatic mid-session replay or silent continuation
loses/duplicates dictated words. Authentication, access, quota and rate limits have
separate safe messages where the protocol supplies that classification. Arbitrary
server error messages are never surfaced or logged. Diagnostics reports only model,
state, credential presence, audio metadata, final character count and static errors.

`CredentialStore` uses generic Windows Credential Manager target `Dictator/OpenAI`
with per-user local-machine persistence. The password entry is never populated
from the stored key, is cleared after saving and can replace/remove the credential.
Keys never enter settings JSON, IPC reports, diagnostics or files. CI uses isolated
credential targets derived from its explicit scratch profiles; unit credentials
use unique targets and are deleted in finally. UI smoke probes read no credential.
