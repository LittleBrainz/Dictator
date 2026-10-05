using System.Runtime.InteropServices;
using System.Text.Json;
using Dictator.App.NativeInterop;
using Dictator.Core;

namespace Dictator.App.Diagnostics;

internal sealed record StartupReport(BuildInfo Build, uint NativeAbi, NativeSnapshot Snapshot,
    string RoundTrip, string DistributionRoot)
{
    internal const string ProbeText = "Dictator — UTF-16 ✓ 中文 😃";

    internal static StartupReport Create(string distributionRoot)
    {
        var build = BuildInfo.Load(Path.Combine(distributionRoot, "lib", "build-info.json"));
        NativeMethods.Initialize(distributionRoot);
        var abi = NativeMethods.GetAbiVersion();
        if (abi != build.NativeAbiVersion)
            throw new InvalidDataException($"Native ABI mismatch: expected {build.NativeAbiVersion}, found {abi}. Extract the entire artifact again.");
        if (typeof(StartupReport).Assembly.GetName().Version?.ToString(3) != build.ProductVersion)
            throw new InvalidDataException("Executable and build-info.json product versions differ.");
        if (Environment.Version.ToString() != build.DotnetRuntimeVersion)
            throw new InvalidDataException("Bundled .NET runtime and build-info.json versions differ.");
        var before = NativeMethods.GetLiveContextCount();
        NativeSnapshot snapshot;
        string text;
        using (var context = NativeMethods.Create())
        {
            snapshot = NativeSnapshot.Create();
            NativeMethods.EnsureSuccess(NativeMethods.Poll(context, ref snapshot));
            text = NativeMethods.RoundTrip(context, ProbeText);
        }
        if (text != ProbeText || snapshot.AbiVersion != abi || snapshot.Sequence != 1 ||
            snapshot.OwnerThreadId != snapshot.PollThreadId || NativeMethods.GetLiveContextCount() != before)
            throw new InvalidDataException("Native contract startup probe failed.");
        return new(build, abi, snapshot, text, distributionRoot);
    }

    internal string DisplayText => $"""
        Product: Dictator {Build.ProductVersion}
        Commit: {Build.GitCommit}
        CI build: {Build.CiBuildNumber}
        Configuration: {Build.Configuration}
        Architecture: {Build.Architecture}
        .NET SDK: {Build.DotnetSdkVersion}
        .NET bundled runtime: {Build.DotnetRuntimeVersion}
        .NET running runtime: {Environment.Version}
        Windows App SDK: {Build.WindowsAppSdkVersion}
        Windows SDK: {Build.WindowsSdkVersion}
        Native ABI: {NativeAbi}
        Native snapshot: {Marshal.SizeOf<NativeSnapshot>()} bytes; sequence {Snapshot.Sequence}
        Native consumer thread: {Snapshot.PollThreadId}
        UTF-16 round-trip: {RoundTrip}
        Installation folder: {DistributionRoot}
        Application/runtime folder: {AppContext.BaseDirectory}
        User data folder: {AppPaths.UserDataRoot}
        """;

    internal void Write(string path, bool uiReady) => File.WriteAllText(path,
        JsonSerializer.Serialize(new { status = "ok", uiReady, build = Build, nativeAbi = NativeAbi,
            snapshot = Snapshot, roundTrip = RoundTrip, distributionRoot = DistributionRoot,
            runtimeDirectory = AppContext.BaseDirectory, runtimeVersion = Environment.Version.ToString(),
            userDataDirectory = AppPaths.UserDataRoot },
            new JsonSerializerOptions { WriteIndented = true, IncludeFields = true }));

    internal static void WriteError(string path, Exception error) => File.WriteAllText(path,
        JsonSerializer.Serialize(new { status = "error", error = error.GetType().Name, message = error.Message }));
}
