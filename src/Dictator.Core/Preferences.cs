using System.Text.Json;
using System.Text.Json.Serialization;

namespace Dictator.Core;

[JsonConverter(typeof(JsonStringEnumConverter<AppTheme>))]
public enum AppTheme { System, Light, Dark }

public sealed record HotkeyPreference(int Modifiers = 3, int Key = 0xDC)
{
    // Win32 MOD_ALT=1, MOD_CONTROL=2, MOD_SHIFT=4, MOD_WIN=8.
    public void Validate()
    {
        if (Modifiers is < 1 or > 15 || Key is < 0x20 or > 0xFE ||
            Key is 0x5B or 0x5C or >= 0xA0 and <= 0xA5 || (Modifiers == 3 && Key == 0x20))
            throw new InvalidDataException("Choose a Hotkey with a modifier and a non-modifier key. Ctrl+Alt+Space is reserved.");
    }
    [JsonIgnore]
    public string Display => string.Join("+", new[] {
        (Modifiers & 2) != 0 ? "Ctrl" : null, (Modifiers & 1) != 0 ? "Alt" : null,
        (Modifiers & 4) != 0 ? "Shift" : null, (Modifiers & 8) != 0 ? "Win" : null,
        Key is 0xDC or 0xE2 ? "\\" : Key == 0x20 ? "Space" : Key is >= 0x70 and <= 0x87 ? $"F{Key - 0x6F}" :
            Key is >= 0x30 and <= 0x5A ? ((char)Key).ToString() : $"Key 0x{Key:X2}"
    }.Where(x => x is not null));
}

public sealed record Preferences
{
    public const int CurrentSchema = 1;
    public static readonly double[] ZoomFactors = [0.750, 0.866, 1.000, 1.155, 1.333];
    public int SchemaVersion { get; init; } = CurrentSchema;
    public bool StartWithWindows { get; init; }
    public AppTheme Theme { get; init; } = AppTheme.System;
    public HotkeyPreference Hotkey { get; init; } = new();
    public double WidgetZoom { get; init; } = 1.000;
    public void Validate()
    {
        if (SchemaVersion != CurrentSchema) throw new InvalidDataException("Unsupported settings schema. The existing file has been preserved.");
        if (!Enum.IsDefined(Theme) || Hotkey is null || !ZoomFactors.Contains(WidgetZoom))
            throw new InvalidDataException("Invalid settings values. The existing file has been preserved.");
        Hotkey.Validate();
    }
}

// A failed load locks this store for the entire session. Recovery is explicit: move
// or repair the preserved file, then restart. Never replace unknown data with defaults.
public sealed class PreferencesStore(string directory)
{
    private static readonly JsonSerializerOptions Json = new() {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
    };
    public string FilePath { get; } = Path.Combine(directory, "settings.json");
    public string? Error { get; private set; }
    public Preferences Load()
    {
        try
        {
            using var input = new FileStream(FilePath, FileMode.Open, FileAccess.Read, FileShare.Read);
            if (input.Length > 65536) throw new InvalidDataException("Settings exceed the supported size.");
            using var document = JsonDocument.Parse(input);
            if (!document.RootElement.TryGetProperty("schemaVersion", out var schema) || schema.GetInt32() != Preferences.CurrentSchema)
                throw new InvalidDataException("Unsupported or missing settings schema.");
            var value = document.RootElement.Deserialize<Preferences>(Json) ?? throw new InvalidDataException("Settings are empty.");
            value.Validate();
            return value;
        }
        catch (FileNotFoundException) { return new(); }
        catch (DirectoryNotFoundException) { return new(); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException or InvalidDataException or InvalidOperationException or FormatException)
        {
            Error = "Settings could not be loaded. The file is preserved; repair or move it and restart Dictator. " + error.GetType().Name;
            return new();
        }
    }
    public void Save(Preferences value)
    {
        if (Error is not null) throw new InvalidOperationException(Error);
        value.Validate();
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        var temporary = FilePath + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough))
            {
                JsonSerializer.Serialize(output, value, Json);
                output.Flush(flushToDisk: true);
            }
            if (File.Exists(FilePath)) File.Replace(temporary, FilePath, null);
            else File.Move(temporary, FilePath);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
