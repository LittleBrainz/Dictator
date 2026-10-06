using Dictator.Core;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Input;
using Windows.ApplicationModel.DataTransfer;
using Windows.Graphics;
using Windows.System;
using Windows.UI.Core;

namespace Dictator.App.Views;

public sealed partial class MainWindow : Window
{
    private readonly App owner;
    private bool updating = true;
    internal FrameworkElement DiagnosticsContent => DiagnosticsRoot;
    internal nint Handle => WinRT.Interop.WindowNative.GetWindowHandle(this);
    internal bool WasActivated { get; private set; }

    internal MainWindow(App owner)
    {
        this.owner = owner;
        InitializeComponent();
        Title = "Dictator Settings";
        AppWindow.Resize(new SizeInt32(960, 740));
        AppWindow.Closing += (_, args) => { if (!owner.IsProbe && !owner.IsStopping) { args.Cancel = true; AppWindow.Hide(); } };
        Navigation.SelectedItem = Navigation.MenuItems[0];
        RefreshPreferences();
    }
    internal void Open()
    {
        RefreshPreferences();
        IdentityText.Text = owner.DiagnosticsText;
        WasActivated = true;
        AppWindow.Show();
        Activate();
    }
    internal void Hide() => AppWindow.Hide();
    internal void RefreshPreferences()
    {
        updating = true;
        StartupSwitch.IsOn = owner.Startup.Enabled;
        ThemeChoice.SelectedIndex = (int)owner.Preferences.Theme;
        HotkeyBox.Text = owner.Preferences.Hotkey.Display;
        ZoomChoice.Value = Array.IndexOf(Preferences.ZoomFactors, owner.Preferences.WidgetZoom);
        DiagnosticsRoot.RequestedTheme = owner.Preferences.Theme switch {
            AppTheme.Light => ElementTheme.Light, AppTheme.Dark => ElementTheme.Dark, _ => ElementTheme.Default
        };
        Navigation.RequestedTheme = DiagnosticsRoot.RequestedTheme;
        SettingsError.IsOpen = owner.SettingsError is not null;
        SettingsError.Message = owner.SettingsError ?? "";
        StartupSwitch.IsEnabled = owner.Store.Error is null;
        ThemeChoice.IsEnabled = owner.Store.Error is null;
        HotkeyBox.IsEnabled = owner.Store.Error is null;
        ResetHotkeyButton.IsEnabled = owner.Store.Error is null;
        ZoomChoice.IsEnabled = owner.Store.Error is null;
        updating = false;
    }
    private void OnNavigationChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.SelectedItem is not NavigationViewItem item || GeneralPage is null) return;
        var page = item.Tag?.ToString() ?? "General";
        PageTitle.Text = page;
        GeneralPage.Visibility = page == "General" ? Visibility.Visible : Visibility.Collapsed;
        WidgetPage.Visibility = page == "Widget" ? Visibility.Visible : Visibility.Collapsed;
        DiagnosticsPage.Visibility = page == "Diagnostics" ? Visibility.Visible : Visibility.Collapsed;
        FuturePage.Visibility = page is "General" or "Widget" or "Diagnostics" ? Visibility.Collapsed : Visibility.Visible;
        if (page == "Diagnostics") IdentityText.Text = owner.DiagnosticsText;
    }
    private void OnStartupChanged(object sender, RoutedEventArgs args)
    {
        if (!updating) owner.ChangeStartup(StartupSwitch.IsOn);
    }
    private void OnThemeChanged(object sender, SelectionChangedEventArgs args)
    {
        if (!updating && ThemeChoice.SelectedIndex >= 0) owner.Save(owner.Preferences with { Theme = (AppTheme)ThemeChoice.SelectedIndex });
    }
    private void OnZoomChanged(object sender, RangeBaseValueChangedEventArgs args)
    {
        if (!updating) owner.Save(owner.Preferences with { WidgetZoom = Preferences.ZoomFactors[(int)Math.Round(args.NewValue)] });
    }
    private void OnHotkeyDown(object sender, KeyRoutedEventArgs args)
    {
        if ((int)args.Key < 0x20 || args.Key is VirtualKey.LeftWindows or VirtualKey.RightWindows || (int)args.Key is >= 0xA0 and <= 0xA5) return;
        static bool Down(VirtualKey key) => InputKeyboardSource.GetKeyStateForCurrentThread(key).HasFlag(CoreVirtualKeyStates.Down);
        var modifiers = (Down(VirtualKey.Menu) ? 1 : 0) | (Down(VirtualKey.Control) ? 2 : 0) |
            (Down(VirtualKey.Shift) ? 4 : 0) | (Down(VirtualKey.LeftWindows) || Down(VirtualKey.RightWindows) ? 8 : 0);
        if (modifiers == 0) return;
        args.Handled = true;
        owner.Save(owner.Preferences with { Hotkey = new(modifiers, (int)args.Key) });
    }
    private void OnResetHotkey(object sender, RoutedEventArgs args) => owner.Save(owner.Preferences with { Hotkey = new() });
    private void OnOpenWidget(object sender, RoutedEventArgs args) => owner.OpenWidget();
    private void OnCopyDiagnostics(object sender, RoutedEventArgs args)
    {
        var content = new DataPackage();
        content.SetText(owner.DiagnosticsText);
        Clipboard.SetContent(content);
    }
}
