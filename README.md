# Dictator

Windows 11 x64 voice dictation assistant, built with C# 14/.NET 10, WinUI 3, and a
small C++23 native core. This fresh implementation currently covers **Phase 0**:
the Settings/Diagnostics shell, versioned native ABI, tests, Windows CI, and
self-contained distribution. Dictation features are not implemented yet.

Download **Dictator** from a successful Windows Actions run, extract once, and
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
Actions on `windows-2025` is authoritative. Phase 1 requires green CI and a
successful manual launch of the staged artifact on Windows 11.
