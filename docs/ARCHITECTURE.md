# Architecture through Phase 1

The v2 specification is in `SPECIFICATION.md`, updated with the user's explicit
6 October 2026 deployment and user-data requirements.
Phase 0 established the build and deployment chain and passed user acceptance.
Phase 1 adds resident lifecycle, a native tray and Widget shell, Settings and
preferences. Dictation, audio capture, global Hotkey interaction, networking,
history, and credentials remain future work.

`Dictator.App` is an unpackaged C# 14/.NET 10 WinUI 3 executable. It creates
**Dictator Settings** quietly and keeps the Widget and tray available until Quit.
`Dictator.Core` contains UI-independent build identity and preferences. `Dictator.Native` is a C++23 DLL with ABI 1.
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
functions, fixed-width integers, one opaque handle, and a 24-byte sized struct.
`LibraryImport` declares explicit cdecl calls. `NativeContext` owns exactly one
handle through SafeHandle. No C++ class, STL object, managed callback, or exception
crosses the boundary. UTF-16 lengths count code units; output includes a NUL and
remains caller-owned. Embedded NUL and surrogate pairs survive a round-trip.

Polling is synchronous on the caller's non-real-time consumer thread. Calls for
one handle are serialized. The snapshot records both its creation and polling
thread; tests prove polling from a second thread. No callback thread exists.
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
and Widget Zoom. Writes flush a unique temporary file before atomic replacement.
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
README.txt and lib. Hotkey storage remains Phase 1; global interaction is Phase 2.
