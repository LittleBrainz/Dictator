using Dictator.Core;

namespace Dictator.Core.Tests;

public sealed class PreferencesTests : IDisposable
{
    private readonly string directory = Path.Combine(Path.GetTempPath(), "Dictator prefs 中文 " + Guid.NewGuid());
    public void Dispose() { if (Directory.Exists(directory)) Directory.Delete(directory, true); }
    [Fact]
    public void FreshPreferencesHaveRequiredDefaultsAndNoSideEffects()
    {
        var store = new PreferencesStore(directory);
        var value = store.Load();
        Assert.Equal("Ctrl+Alt+\\", value.Hotkey.Display);
        Assert.Equal(1.000, value.WidgetZoom);
        Assert.Equal("", value.MicrophoneId);
        Assert.False(Directory.Exists(directory));
    }
    [Fact]
    public void LegacyDefaultFollowsLayoutButCapturedOemKeyCanRemainLiteral()
    {
        Directory.CreateDirectory(directory);
        var store = new PreferencesStore(directory);
        File.WriteAllText(store.FilePath, "{\"schemaVersion\":1,\"hotkey\":{\"modifiers\":3,\"key\":220}}");
        Assert.True(store.Load().Hotkey.LayoutBackslash);
        var literal = new Preferences { Hotkey = new(3, 0xDC, LayoutBackslash: false) };
        store.Save(literal);
        Assert.Equal(literal, store.Load());
        Assert.Equal("Ctrl+Alt+Key 0xDC", literal.Hotkey.Display);
    }
    [Fact]
    public void AllPreferencesRoundTripWithNoTemporaryFiles()
    {
        var store = new PreferencesStore(directory);
        var value = new Preferences { Theme = AppTheme.Dark, StartWithWindows = true, WidgetZoom = 1.333, Hotkey = new(6, 0x77), MicrophoneId = "{stable-device-id}" };
        store.Save(value);
        Assert.Equal(value, new PreferencesStore(directory).Load());
        Assert.Single(Directory.GetFiles(directory));
        store.Save(value with { Theme = AppTheme.Light });
        Assert.Equal(AppTheme.Light, store.Load().Theme);
    }
    [Fact]
    public void InvalidMicrophoneIdentityCannotReplacePreferences()
    {
        var store = new PreferencesStore(directory); store.Save(new());
        var before = File.ReadAllText(store.FilePath);
        Assert.Throws<InvalidDataException>(() => store.Save(new() { MicrophoneId = "bad\0identity" }));
        Assert.Throws<InvalidDataException>(() => store.Save(new() { MicrophoneId = new string('x', 512) }));
        Assert.Equal(before, File.ReadAllText(store.FilePath));
    }
    [Theory]
    [InlineData("{broken")]
    [InlineData("{\"schemaVersion\":2,\"futureValue\":true}")]
    [InlineData("{\"theme\":\"Dark\"}")]
    [InlineData("{\"schemaVersion\":1,\"widgetZoom\":42}")]
    public void InvalidOrFutureSettingsArePreservedAndCannotBeOverwritten(string text)
    {
        Directory.CreateDirectory(directory);
        var store = new PreferencesStore(directory);
        File.WriteAllText(store.FilePath, text);
        store.Load();
        Assert.NotNull(store.Error);
        Assert.Throws<InvalidOperationException>(() => store.Save(new()));
        Assert.Equal(text, File.ReadAllText(store.FilePath));
    }
    [Fact]
    public void InaccessibleSettingsEntryBlocksWritesWithoutDeletingIt()
    {
        var store = new PreferencesStore(directory);
        Directory.CreateDirectory(store.FilePath);
        store.Load();
        Assert.NotNull(store.Error);
        Assert.Throws<InvalidOperationException>(() => store.Save(new()));
        Assert.True(Directory.Exists(store.FilePath));
    }
    [Fact]
    public void ExcessiveSettingsFileIsPreserved()
    {
        Directory.CreateDirectory(directory);
        var store = new PreferencesStore(directory);
        var text = new string(' ', 65537);
        File.WriteAllText(store.FilePath, text);
        store.Load();
        Assert.NotNull(store.Error);
        Assert.Equal(text, File.ReadAllText(store.FilePath));
    }
    [Fact]
    public void ReservedHotkeyAndInvalidZoomCannotReplaceGoodFile()
    {
        var store = new PreferencesStore(directory);
        store.Save(new());
        var before = File.ReadAllText(store.FilePath);
        Assert.Throws<InvalidDataException>(() => store.Save(new() { Hotkey = new(3, 0x20) }));
        Assert.Throws<InvalidDataException>(() => store.Save(new() { WidgetZoom = 0.9 }));
        Assert.Equal(before, File.ReadAllText(store.FilePath));
    }
}
