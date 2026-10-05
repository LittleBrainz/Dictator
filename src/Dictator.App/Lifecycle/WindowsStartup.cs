using Microsoft.Win32;

namespace Dictator.App.Lifecycle;

internal sealed class WindowsStartup(string distributionRoot, bool test)
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private readonly string valueName = test ? "Dictator.Phase1.Test" : "Dictator";
    internal string Command => $"\"{Path.Combine(distributionRoot, "Dictator.exe")}\" --startup";
    internal bool Enabled
    {
        get { using var key = Registry.CurrentUser.OpenSubKey(RunKey); return key?.GetValue(valueName) is string text && text == Command; }
    }
    internal void Set(bool enabled)
    {
        using var key = Registry.CurrentUser.CreateSubKey(RunKey, writable: true);
        if (enabled) key.SetValue(valueName, Command, RegistryValueKind.String);
        else key.DeleteValue(valueName, throwOnMissingValue: false);
    }
}
