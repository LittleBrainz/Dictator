using Dictator.App.Diagnostics;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace Dictator.App.Views;

public sealed partial class MainWindow : Window
{
    internal FrameworkElement DiagnosticsContent => DiagnosticsRoot;

    internal MainWindow(StartupReport report)
    {
        InitializeComponent();
        Title = "Dictator Settings";
        AppWindow.Resize(new SizeInt32(900, 720));
        IdentityText.Text = report.DisplayText;
    }
}
