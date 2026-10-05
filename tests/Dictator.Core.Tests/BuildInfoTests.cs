using System.Text.Json;
using Dictator.Core;

namespace Dictator.Core.Tests;

public sealed class BuildInfoTests
{
    private static readonly BuildInfo Valid = new("0.0.1", new string('a', 40), "123", "Release",
        "win-x64", "10.0.401", "10.0.12", "2.5.1", "10.0.28000.0", 1);

    [Fact]
    public void LoadsCompleteBuildIdentity() => WithFile(Valid, path => Assert.Equal(Valid, BuildInfo.Load(path)));

    [Fact]
    public void RejectsArchitectureAndAbiMismatch()
    {
        WithFile(Valid with { Architecture = "win-arm64" }, path => Assert.Throws<InvalidDataException>(() => BuildInfo.Load(path)));
        WithFile(Valid with { NativeAbiVersion = 2 }, path => Assert.Throws<InvalidDataException>(() => BuildInfo.Load(path)));
    }

    [Fact]
    public void RejectsIncompleteOrMalformedIdentity()
    {
        WithFile(Valid with { GitCommit = "missing" }, path => Assert.Throws<InvalidDataException>(() => BuildInfo.Load(path)));
        WithFile(Valid with { DotnetSdkVersion = "preview" }, path => Assert.Throws<InvalidDataException>(() => BuildInfo.Load(path)));
        WithFile(Valid with { CiBuildNumber = "" }, path => Assert.Throws<InvalidDataException>(() => BuildInfo.Load(path)));
    }

    private static void WithFile(BuildInfo info, Action<string> verify)
    {
        var path = Path.GetTempFileName();
        try
        {
            File.WriteAllText(path, JsonSerializer.Serialize(info, new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase }));
            verify(path);
        }
        finally { File.Delete(path); }
    }
}
