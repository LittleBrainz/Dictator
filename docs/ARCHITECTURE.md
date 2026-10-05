# Phase 0 architecture

The attached v2 specification is preserved verbatim in `SPECIFICATION.md`.
Phase 0 implements the build and deployment chain only. There is no dictation,
networking, microphone capture, tray, Widget, Hotkey, history, or credential store.

`Dictator.App` is an unpackaged C# 14/.NET 10 WinUI 3 executable. It opens
**Dictator Settings** and displays Diagnostics. `Dictator.Core` contains the
UI-independent build identity reader. `Dictator.Native` is a C++23 DLL with ABI 1.
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
Dictator.exe
README.txt
lib/
  Native/Dictator.Native.dll
  build-info.json
```

The executable uses the Windows App SDK's supported unpackaged self-contained
single-file deployment. `EnableMsixTooling`, `WindowsPackageType=None`,
`WindowsAppSDKSelfContained`, `SelfContained`, `IncludeAllContentForSelfExtract`,
and `PublishSingleFile` are explicit. Trimming and Native AOT are disabled.
The SDK's registration-free automatic initializer owns runtime loading and
resource resolution. The SDK installs nothing; .NET extracts bundled content
automatically on first launch. No custom WinUI DLL search path is installed.

Microsoft's self-contained overview contains a statement that single-file is not
available for WinUI, while its specific unpackaged single-file guide and the
2.5.1 package's `WindowsAppSDKSingleFileVerifyConfiguration` target explicitly
support this configuration. The latter documented configuration and SDK validation
are used; Windows CI and clean-PC launch acceptance must establish that it works.

- https://learn.microsoft.com/windows/apps/package-and-deploy/unpackage-winui-app#single-file-exe
- https://learn.microsoft.com/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps

WinUI resources, neutral fallback, and **all** SDK locales (including en-GB,
en-US, zh-CN) remain bundled. This avoids pruning files that a PRI index references.
`lib/WinUI` is unnecessary when no WinUI content remains external. If a future
supported publish requires external modules, they belong under `lib/WinUI` with
their resources, and any layout change requires packaged proof and documentation.
Staging fails if publish leaves an unexpected dependency outside the bundle.

The native DLL uses the static MSVC CRT. Microsoft's runtime modules receive the
app-local x64 VC redistributable and Microsoft.VCRTForwarders.140 inside the bundle.
The managed native resolver loads only Dictator's DLL by absolute path from
`lib/Native`. It derives that path from `Environment.ProcessPath`, because
`AppContext.BaseDirectory` points to the framework's extraction directory.
The current working directory is never used for dependency discovery.

`build-info.json` records product, Git SHA, CI run, configuration, architecture,
SDK/runtime versions and ABI. Startup verifies product/runtime/ABI identity before
showing Diagnostics. Missing native files and incompatible metadata give an
actionable startup error; errors preceding managed entry (OS policy, apphost or SDK
automatic initialization) may be surfaced by Windows/.NET itself.

## Repository and acceptance status

The existing connected repository is `LittleBrainz/Dictator`; it was public at
bootstrap. No repository was recreated and its visibility was not changed.
The specification's private-repository requirement remains an external setup
decision. Source changes alone cannot establish clean-PC compatibility or trust
under Smart App Control. The build remains unsigned. Phase 1 stays gated on both
green authoritative CI and the user's manual Windows 11 launch.
