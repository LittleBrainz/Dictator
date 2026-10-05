namespace Dictator.App.Lifecycle;

internal sealed record LaunchOptions(string Command, string? ReportPath = null, string? TestDataRoot = null)
{
    internal bool IsTest => TestDataRoot is not null;
    internal bool IsProbe => Command is "--smoke-test" or "--ui-smoke-test" or "--launch-smoke-test";
    internal static LaunchOptions Parse(string[] args)
    {
        if (args.Length == 0) return new("launch");
        if (args.Length == 1 && args[0] is "--startup" or "--restart") return new("startup");
        if (args.Length == 2 && args[0] is "--smoke-test" or "--ui-smoke-test" or "--launch-smoke-test" && Path.IsPathFullyQualified(args[1]))
            return new(args[0], args[1]);
        if (args.Length == 4 && args[0] == "--lifecycle-test" && Path.IsPathFullyQualified(args[1]) && Path.IsPathFullyQualified(args[2]))
            return new(args[3], args[2], args[1]);
        throw new ArgumentException("Usage: Dictator.exe [--startup]");
    }
}
