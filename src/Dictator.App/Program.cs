using System.Diagnostics;
using System.Runtime.InteropServices;
using Dictator.App.Diagnostics;
using Dictator.App.Lifecycle;
using Dictator.Core;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace Dictator.App;

internal static partial class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        string? smokeOutput = null;
        App? application = null;
        var timer = Stopwatch.StartNew();
        try
        {
            var options = LaunchOptions.Parse(args);
            smokeOutput = options.ReportPath;
            if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000) || !Environment.Is64BitProcess)
                throw new PlatformNotSupportedException("Dictator requires Windows 11 x64.");
            var executable = Environment.ProcessPath ?? throw new InvalidOperationException("Executable path is unavailable.");
            var root = AppPaths.DistributionRootFromHost(executable);
            using (var instance = options.IsProbe ? null : new SingleInstance(options.TestDataRoot ?? AppPaths.UserDataRoot,
                claimOwnership: !options.IsTest || options.Command is "launch" or "startup"))
            {
                if (instance is { IsOwner: false })
                {
                    var command = options.Command == "launch" ? "open-settings" : options.Command;
                    var response = instance.SendAsync(command).GetAwaiter().GetResult();
                    if (smokeOutput is not null) File.WriteAllText(smokeOutput, response);
                    return 0;
                }
                if (options.IsTest && options.Command is not ("launch" or "startup"))
                    throw new InvalidOperationException("The test resident instance is not running.");
                var report = StartupReport.Create(root);
                if (options.Command == "--smoke-test") { report.Write(smokeOutput!, uiReady: false); return 0; }
                var ready = new TaskCompletionSource<App>(TaskCreationOptions.RunContinuationsAsynchronously);
                instance?.Listen(async command => {
                    var resident = await ready.Task.ConfigureAwait(false);
                    await resident.ResidentReady.Task.ConfigureAwait(false);
                    return await resident.DispatchAsync(command).ConfigureAwait(false);
                });
                WinRT.ComWrappersSupport.InitializeComWrappers();
                if (options.TestLocale is not null) Windows.Globalization.ApplicationLanguages.PrimaryLanguageOverride = options.TestLocale;
                try
                {
                    Application.Start(initialization =>
                    {
                        SynchronizationContext.SetSynchronizationContext(
                            new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
                        application = new App(report, options, timer);
                        // OnLaunched executes after Application.Start initialization.
                        // Queue activation after that event has constructed Settings.
                        DispatcherQueue.GetForCurrentThread().TryEnqueue(() => ready.TrySetResult(application));
                    });
                }
                finally { application?.Cleanup(); }
            } // Release the instance mutex and pipe before launching the replacement.
            if (application?.RestartRequested == true)
            {
                var restart = new ProcessStartInfo(Path.Combine(root, "Dictator.exe")) { UseShellExecute = false, WorkingDirectory = root };
                if (options.IsTest)
                {
                    restart.ArgumentList.Add("--lifecycle-test"); restart.ArgumentList.Add(options.TestDataRoot!);
                    restart.ArgumentList.Add(options.ReportPath!); restart.ArgumentList.Add("startup");
                }
                else restart.ArgumentList.Add("--restart");
                using var child = Process.Start(restart) ?? throw new IOException("Dictator could not restart.");
            }
            return 0;
        }
        catch (Exception error)
        {
            if (smokeOutput is not null) StartupReport.WriteError(smokeOutput, error);
            else MessageBox(0, $"Dictator could not start.\n\n{error.Message}\n\nKeep the complete lib folder beside Dictator.exe. Include lib/build-info.json when reporting a failure.", "Dictator launch error", 0x10);
            return 1;
        }
    }
    [LibraryImport("user32.dll", EntryPoint = "MessageBoxW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int MessageBox(nint owner, string text, string caption, uint type);
}
