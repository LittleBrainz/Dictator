# Phase 0 verification

## Automated checks

`packaging/scripts/Build.ps1` is used by the single Windows x64 CI job. It restores
locked packages, builds the native DLL and native tests, runs Core tests, publishes
the self-contained app, verifies its app-local publish tree, stages the distribution,
and runs P/Invoke tests against the **staged** DLL.

`Test-Package.ps1` then makes a temporary ZIP from the staged directory and extracts
it once into a path containing spaces and Unicode. That ZIP is only test input, never a nested
ZIP in the uploaded `Dictator` artifact. The test launches the extracted executable
from a different working directory, with a forbidden runtime-extraction destination and invalid external
.NET runtime paths. It checks:

- exact root entries: Dictator.exe, README.txt, lib;
- correct product, commit, runtime and ABI identity;
- UTF-16 string and 24-byte struct round-trips;
- runtime files/resources already present under `lib/WinUI`, retained en-GB/en-US/zh-CN
  resources, and no startup runtime extraction;
- persistent user-data location `%USERPROFILE%\.dictator` independently of cwd/install;
- first and repeated launch timing (reported without an arbitrary timing threshold);
- normal launcher termination leaving exactly one host process, and closing Settings
  successfully terminating that host;
- a headless `--smoke-test` returning a machine-readable result;
- a `--ui-smoke-test` that creates the actual WinUI Settings window, loads its XAML,
  writes the result, closes, and exits;
- `--launch-smoke-test` uses the normal detached launcher path, reports XAML-loaded
  readiness without closing, and lets CI close the window only after startup finishes;
- a missing native DLL returning a useful error and nonzero exit;
- a missing managed host returning a nonzero launcher exit without a modal dialog.

Native and interop suites test mismatch/error codes, buffer sizing, ownership,
10,000 create/destroy cycles, and the documented calling-thread polling contract.
There are no live provider tests or secrets in normal CI.

The runner has development tools installed, so invalid external .NET paths plus
self-contained smoke testing do not establish every prerequisite-free clean-PC
scenario. The following manual check is mandatory.

## Manual Windows 11 x64 acceptance

The user successfully launched the original self-extracting Phase 0 build, reporting
approximately 15 seconds on the first start and 0.5 seconds on subsequent starts.
The revised app-local package must be retested using the following check.

1. Download the successful run's artifact named **Dictator** (Dictator.zip).
2. Use an ordinary Windows 11 x64 machine with no required development tools or
   separately installed .NET/Windows App SDK. Extract once to a path with spaces,
   such as `Downloads\Dictator Phase 0`.
3. Launch `Dictator.exe`. Confirm the window title is **Dictator Settings**,
   Diagnostics shows the expected Git SHA/runtime/ABI, the UTF-16 probe is intact,
   the application/runtime directory is `lib/WinUI`, and the user data directory is
   `%USERPROFILE%\.dictator`. Compare first/subsequent startup time with the old build.
4. Close it, open a command prompt in another directory, and launch the same EXE by
   its quoted absolute path. Confirm the same successful behavior.
5. Confirm there was no runtime installer, elevation request, microphone capture,
   network session, or runtime extraction into Temp. Confirm closing the Phase 0 Settings shell exits it.
6. If Windows blocks the unsigned build, record the policy/error; a successful CI
   build does not establish Smart App Control trust. Do not disable a policy as an
   undocumented acceptance step.
7. Record the artifact run number, Git SHA, Windows build, launch result, and any
   screenshot/error as acceptance evidence.

**Do not begin Phase 1 until Windows CI is green and this manual launch is recorded.**

## Optional Cloud checks

With the pinned .NET SDK and a C++23 compiler available, Core tests and portable
native/interop tests run on Linux. Stage `libDictator.Native.so` under a scratch
`lib/Native` folder and point `DICTATOR_DISTRIBUTION_ROOT` there. This is supplementary
contract coverage, not a Linux product. WinUI compilation and packaging are Windows-only.
