using Dictator.Core;
using Dictator.App.NativeInterop;
using System.Runtime.InteropServices;
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
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "Dictator.ico"));
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
        MicrophonePicker.ItemsSource = owner.Microphones;
        MicrophonePicker.SelectedItem = owner.Microphones.FirstOrDefault(x => x.Id == owner.Preferences.MicrophoneId);
        MicrophonePicker.IsEnabled = owner.Store.Error is null;
        DiagnosticsRoot.RequestedTheme = owner.Preferences.Theme switch {
            AppTheme.Light => ElementTheme.Light, AppTheme.Dark => ElementTheme.Dark, _ => ElementTheme.Default
        };
        Navigation.RequestedTheme = DiagnosticsRoot.RequestedTheme;
        SettingsError.IsOpen = owner.SettingsError is not null || owner.HotkeyError is not null || owner.AudioError is not null;
        SettingsError.Message = owner.SettingsError ?? owner.HotkeyError ?? owner.AudioError ?? "";
        SettingsError.Title = owner.SettingsError is null && owner.HotkeyError is null && owner.AudioError is not null ? "Microphone needs attention" : "Settings need attention";
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
        SpeechPage.Visibility = page == "Speech" ? Visibility.Visible : Visibility.Collapsed;
        DiagnosticsPage.Visibility = page == "Diagnostics" ? Visibility.Visible : Visibility.Collapsed;
        FuturePage.Visibility = page is "General" or "Widget" or "Speech" or "Diagnostics" ? Visibility.Collapsed : Visibility.Visible;
        if (page == "Speech") _ = owner.RefreshMicrophonesAsync();
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
        var backslash = (MapVirtualKeyExW((uint)args.Key, 2, GetKeyboardLayout(0)) & 0x7FFFFFFF) == '\\';
        owner.Save(owner.Preferences with {
            Hotkey = backslash ? new(modifiers, 0xDC) : new(modifiers, (int)args.Key, LayoutBackslash: false)
        });
    }
    private void OnResetHotkey(object sender, RoutedEventArgs args) => owner.Save(owner.Preferences with { Hotkey = new() });
    private void OnOpenWidget(object sender, RoutedEventArgs args) => owner.OpenWidget();
    private void OnMicrophoneChanged(object sender, SelectionChangedEventArgs args)
    {
        if (!updating && MicrophonePicker.SelectedItem is MicrophoneChoice choice)
            owner.Save(owner.Preferences with { MicrophoneId = choice.Id });
    }
    private async void OnRefreshMicrophones(object sender, RoutedEventArgs args) => await owner.RefreshMicrophonesAsync();
    private void OnCopyDiagnostics(object sender, RoutedEventArgs args)
    {
        var content = new DataPackage();
        content.SetText(owner.DiagnosticsText);
        Clipboard.SetContent(content);
    }
    [LibraryImport("user32.dll")]
    private static partial nint GetKeyboardLayout(uint thread);
    [LibraryImport("user32.dll")]
    private static partial uint MapVirtualKeyExW(uint key, uint type, nint layout);
}
