# Dictator

This is a fresh implementation of Dictator, not a reconstruction of the deleted
C++ prototype. Read `docs/SPECIFICATION.md` and the requested phase. Later explicit
requirements override generic framework conventions. Phase 0 passed Windows CI and
the user accepted the staged artifact. The user accepted Phases 1 and 2. The current scope is the user-requested Widget
design refinement before Phase 3. Keep the two glossy blue capsules (270 x 62
DIP) separated by a real desktop-visible, click-through gap. The upper strip
shows white raw transcription scrolling right-to-left while Talking; off-state
hover information fades in bright yellow after one second, without a separate tooltip
window. Raw text means speech-to-text in the spoken language, before formatting.
Keep the left microphone, right Settings/Close controls, red/slashed inactive
microphone, cyan quiet line with an eligible cursor, and red quiet line without
one. Phase 2 has no real transcription; the live-text bridge accepts future
provider deltas, and normal operation never injects sample text.

## Platform and architecture

- Windows 11 x64 only. Stable .NET 10 LTS / C# 14 / Windows App SDK 2.5.1 / WinUI 3.
- `global.json` pins SDK 10.0.401; package versions and NuGet lock files are checked in.
- C# owns application logic and WinUI. C++23 owns narrowly scoped native facilities.
- Versioned C ABI plus source-generated P/Invoke. No C++/CLI or custom WinRT component.
- Native buffers and handles have explicit ownership; no exceptions cross the ABI.
- Real-time audio must never block on managed code, UI, network, disk, or logging.
- The future Widget must remain non-activating and interactive while preserving focus.
- Persistent settings/database/logs belong in `%USERPROFILE%\.dictator` (`~/.dictator`),
  independently of install location. A future installer may place binaries under
  `%LOCALAPPDATA%\Programs\Dictator`. No config lives with the binaries.
- The root `Dictator.exe` is a short-lived native launcher. The managed host and
  complete self-contained publish tree live in `lib/WinUI`; no runtime self-extraction.
- Keep only en-GB, en-US, fr-FR and zh-CN locale directories in the artifact.
- Credentials belong only in Windows Credential Manager. No telemetry or audio retention.
- No transcript content in logs by default. Never dump credentials or environment values.
- User-facing words: Dictator Settings, Widget, Hotkey, Talk/Talking, Phrase Shortcuts.

## Builds and validation

Hosted `windows-2025` GitHub Actions is the authoritative Windows compiler and
package environment. Do not assume the user's PC has Visual Studio, a Windows SDK,
.NET, or Windows App SDK installed. Ship both runtimes self-contained. Do not bypass
failing checks. Verify the staged artifact, not just the source build.

From a hosted Windows runner with the pinned SDK selected:

```powershell
dotnet restore Dictator.sln --locked-mode
cmake -S . -B artifacts/native -G "Visual Studio 18 2026" -A x64
cmake --build artifacts/native --config Release --parallel 2
ctest --test-dir artifacts/native -C Release --output-on-failure
dotnet test tests/Dictator.Core.Tests -c Release --no-restore
```

The exact complete restore/build/test/publish/stage/smoke command is:

```powershell
./packaging/scripts/Build.ps1
```

It locates the hosted x64 VC redistributable, selects the latest installed stable
Windows SDK, publishes the complete app-local runtime tree under `lib/WinUI`,
and runs ABI tests against `artifacts/Dictator/lib/Native/Dictator.Native.dll`.
To repeat only package verification:

```powershell
./packaging/scripts/Test-Package.ps1 -PackageRoot ./artifacts/Dictator
```

For interop tests, set `DICTATOR_DISTRIBUTION_ROOT` to the absolute staged root and
run `dotnet test tests/Dictator.Interop.Tests -c Release --no-restore`.

Portable native/Core/interop tests can run in Cloud/Linux as supplementary evidence.
Linux output is never a product or a substitute for Windows/package acceptance.
See `docs/TESTING.md` for the mandatory manual Windows check.
