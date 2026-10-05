using Dictator.App.Diagnostics;
using Microsoft.UI.Xaml;

namespace Dictator.App;

public partial class App : Application
{
    private readonly StartupReport report;
    private readonly string? smokeOutput;
    private readonly bool exitAfterReport;
    private Window? window;

    internal App(StartupReport report, string? smokeOutput, bool exitAfterReport)
    {
        this.report = report;
        this.smokeOutput = smokeOutput;
        this.exitAfterReport = exitAfterReport;
        InitializeComponent();
        UnhandledException += (_, args) =>
        {
            if (smokeOutput is not null) StartupReport.WriteError(smokeOutput, args.Exception);
            // Preserve framework fail-fast behavior: never hide a failing smoke test.
        };
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var settings = new Views.MainWindow(report);
        window = settings;
        // Phase 0 has one window and no tray residence. Make normal shutdown explicit.
        settings.Closed += (_, _) => Exit();
        if (smokeOutput is not null)
        {
            settings.DiagnosticsContent.Loaded += (_, _) =>
            {
                if (!settings.DispatcherQueue.TryEnqueue(() =>
                {
                    report.Write(smokeOutput, uiReady: true);
                    if (exitAfterReport) settings.Close();
                })) throw new InvalidOperationException("Could not complete the UI smoke test.");
            };
        }
        settings.Activate();
    }
}
