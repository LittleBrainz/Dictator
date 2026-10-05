using Dictator.App.Diagnostics;
using Microsoft.UI.Xaml;

namespace Dictator.App;

public partial class App : Application
{
    private readonly StartupReport report;
    private readonly string? smokeOutput;
    private Window? window;

    internal App(StartupReport report, string? smokeOutput)
    {
        this.report = report;
        this.smokeOutput = smokeOutput;
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
        if (smokeOutput is not null)
        {
            settings.DiagnosticsContent.Loaded += (_, _) =>
            {
                if (!settings.DispatcherQueue.TryEnqueue(() =>
                {
                    report.Write(smokeOutput, uiReady: true);
                    settings.Close();
                    Exit();
                })) throw new InvalidOperationException("Could not complete the UI smoke test.");
            };
        }
        settings.Activate();
    }
}
