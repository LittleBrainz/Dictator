using System.Runtime.InteropServices;
using Dictator.App.Diagnostics;
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
        try
        {
            if (args.Length != 0 && (args.Length != 2 ||
                args[0] is not ("--smoke-test" or "--ui-smoke-test")))
                throw new ArgumentException("Usage: Dictator.exe [--smoke-test|--ui-smoke-test <absolute-result-path>]");
            if (args.Length == 2)
            {
                if (!Path.IsPathFullyQualified(args[1]))
                    throw new ArgumentException("The smoke-test result path must be absolute.");
                smokeOutput = args[1];
            }
            if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000) || !Environment.Is64BitProcess)
                throw new PlatformNotSupportedException("Dictator requires Windows 11 x64.");

            var executable = Environment.ProcessPath ?? throw new InvalidOperationException("Executable path is unavailable.");
            var report = StartupReport.Create(AppPaths.DistributionRootFromHost(executable));
            if (args.Length == 2 && args[0] == "--smoke-test")
            {
                report.Write(smokeOutput!, uiReady: false);
                return 0;
            }

            WinRT.ComWrappersSupport.InitializeComWrappers();
            Application.Start(initialization =>
            {
                SynchronizationContext.SetSynchronizationContext(
                    new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
                _ = new App(report, smokeOutput);
            });
            return 0;
        }
        catch (Exception error)
        {
            if (smokeOutput is not null)
            {
                StartupReport.WriteError(smokeOutput, error);
            }
            else
            {
                MessageBox(0, $"Dictator could not start.\n\n{error.Message}\n\nExtract the complete Dictator artifact to a writable folder and launch Dictator.exe again. Include the artifact's lib/build-info.json when reporting the failure.",
                    "Dictator launch error", 0x10);
            }
            return 1;
        }
    }

    [LibraryImport("user32.dll", EntryPoint = "MessageBoxW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int MessageBox(nint owner, string text, string caption, uint type);
}
