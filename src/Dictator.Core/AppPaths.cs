namespace Dictator.Core;

public static class AppPaths
{
    // User data survives moving/replacing the installation or adding an installer.
    // Resolve the Windows profile's known folder, never the executable or cwd.
    public static string UserDataRoot => UserDataRootFromProfile(
        Environment.GetFolderPath(Environment.SpecialFolder.UserProfile));

    public static string UserDataRootFromProfile(string profileDirectory)
    {
        if (string.IsNullOrWhiteSpace(profileDirectory) || !Path.IsPathFullyQualified(profileDirectory))
            throw new InvalidDataException("The user profile directory is unavailable.");
        return Path.Combine(profileDirectory, ".dictator");
    }

    public static string DistributionRootFromHost(string executablePath)
    {
        if (!Path.IsPathFullyQualified(executablePath))
            throw new InvalidDataException("The application executable path must be absolute.");
        var hostDirectory = new DirectoryInfo(Path.GetDirectoryName(executablePath)!);
        if (!hostDirectory.Name.Equals("WinUI", StringComparison.OrdinalIgnoreCase) ||
            hostDirectory.Parent is not { } lib || !lib.Name.Equals("lib", StringComparison.OrdinalIgnoreCase) ||
            lib.Parent is not { } root)
            throw new InvalidDataException("Dictator's application must be under lib/WinUI. Extract the complete artifact again.");
        return root.FullName;
    }
}
