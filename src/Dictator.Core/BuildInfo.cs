using System.Text.Json;
using System.Text.Json.Serialization;

namespace Dictator.Core;

public sealed record BuildInfo(
    string ProductVersion,
    string GitCommit,
    string CiBuildNumber,
    string Configuration,
    string Architecture,
    string DotnetSdkVersion,
    string DotnetRuntimeVersion,
    string WindowsAppSdkVersion,
    string WindowsSdkVersion,
    uint NativeAbiVersion)
{
    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
    };

    public static BuildInfo Load(string path)
    {
        var info = JsonSerializer.Deserialize<BuildInfo>(File.ReadAllText(path), Options)
            ?? throw new InvalidDataException("Build identity is empty.");
        if (info.Architecture != "win-x64" || info.Configuration != "Release" ||
            info.NativeAbiVersion != 1 || !Version.TryParse(info.ProductVersion, out _) ||
            !Version.TryParse(info.DotnetSdkVersion, out _) ||
            !Version.TryParse(info.DotnetRuntimeVersion, out _) ||
            !Version.TryParse(info.WindowsAppSdkVersion, out _) ||
            !Version.TryParse(info.WindowsSdkVersion, out _) ||
            string.IsNullOrWhiteSpace(info.CiBuildNumber) ||
            info.GitCommit is null || info.GitCommit.Length != 40 ||
            !info.GitCommit.All(Uri.IsHexDigit))
        {
            throw new InvalidDataException("Build identity is incomplete or incompatible with this build.");
        }
        return info;
    }
}
