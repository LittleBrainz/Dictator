using Dictator.Core;

namespace Dictator.Core.Tests;

public sealed class AppPathsTests
{
    [Fact]
    public void ResolvesDistributionFromHostInPathWithSpacesAndUnicode()
    {
        var root = Path.Combine(Path.GetTempPath(), "Dictator launch 中文 with spaces");
        var host = Path.Combine(root, "lib", "WinUI", "Dictator.App.exe");
        Assert.Equal(root, AppPaths.DistributionRootFromHost(host));
    }

    [Fact]
    public void RejectsUnstagedHostInsteadOfSearchingWorkingDirectory()
    {
        Assert.Throws<InvalidDataException>(() => AppPaths.DistributionRootFromHost(
            Path.Combine(Path.GetTempPath(), "Dictator.App.exe")));
        Assert.Throws<InvalidDataException>(() => AppPaths.DistributionRootFromHost("Dictator.App.exe"));
    }

    [Fact]
    public void UserDataBelongsToTheProfileIndependentlyOfInstallation()
    {
        var profile = Path.Combine(Path.GetTempPath(), "User profile 中文");
        Assert.Equal(Path.Combine(profile, ".dictator"), AppPaths.UserDataRootFromProfile(profile));
        Assert.Equal(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".dictator"),
            AppPaths.UserDataRoot);
        Assert.Throws<InvalidDataException>(() => AppPaths.UserDataRootFromProfile(""));
        Assert.Throws<InvalidDataException>(() => AppPaths.UserDataRootFromProfile("relative profile"));
    }
}
