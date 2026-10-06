using Microsoft.Win32;

namespace Dictator.App.Lifecycle;

internal sealed class WindowsStartup(string distributionRoot, bool test)
{
    internal sealed record Registration(object? Value, RegistryValueKind Kind);
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
    internal Registration Capture()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey);
        var value = key?.GetValue(valueName, null, RegistryValueOptions.DoNotExpandEnvironmentNames);
        return new(value, value is null ? RegistryValueKind.String : key!.GetValueKind(valueName));
    }
    internal void Restore(Registration registration)
    {
        using var key = Registry.CurrentUser.CreateSubKey(RunKey, writable: true);
        if (registration.Value is null) key.DeleteValue(valueName, false);
        else key.SetValue(valueName, registration.Value, registration.Kind);
    }
}
