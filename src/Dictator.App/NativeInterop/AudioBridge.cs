using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Dictator.App.NativeInterop;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioSnapshot
{
    public ulong Session, DeviceRevision, DroppedFrames, CapturedFrames;
    public uint State, Error;
    public int Status;
    public uint SampleRate, Channels;
    public float Peak, Rms;
    public uint BufferedFrames;
}
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAudioDevice
{
    public fixed char Id[512];
    public fixed char Name[256];
    public uint IsDefault;
    internal string DeviceId { get { fixed (char* value = Id) return new(value); } }
    internal string DeviceName { get { fixed (char* value = Name) return new(value); } }
}
internal sealed record MicrophoneChoice(string Id, string Name)
{
    public override string ToString() => Name;
}

// Borrowed native service. App joins this consumer and catalog task before the
// UI-thread ResidentHost destroys it. No callbacks into managed code from WASAPI.
internal sealed class AudioBridge : IDisposable
{
    private readonly nint handle;
    private readonly CancellationTokenSource cancellation = new();
    private readonly Task consumer;
    private Task<IReadOnlyList<MicrophoneChoice>>? catalog;
    private readonly object transfer = new();
    private readonly float[] samples = new float[4096];
    private Dictator.Core.Transcription.TranscriptionRun? sink;
    private ulong? captureSession;
    private bool transferFailed;
    internal bool TransferFailed { get { lock (transfer) return transferFailed; } }
    internal void BeginTransfer(Dictator.Core.Transcription.TranscriptionRun run)
    {
        lock (transfer) { sink = run; captureSession = null; transferFailed = false; }
    }
    internal void FinishTransfer()
    {
        lock (transfer) sink?.RequestFinish();
    }
    internal void EndTransfer()
    {
        lock (transfer) { sink = null; captureSession = null; }
    }
    internal AudioBridge(nint handle)
    {
        if (handle == 0) throw new InvalidOperationException("Microphone service is unavailable.");
        this.handle = handle;
        consumer = Task.Run(ConsumeAsync);
    }
    internal NativeAudioSnapshot Snapshot
    {
        get { NativeMethods.EnsureSuccess(NativeMethods.AudioStatus(handle, out var value)); return value; }
    }
    internal Task<IReadOnlyList<MicrophoneChoice>> DevicesAsync()
        => catalog is { IsCompleted: false } ? catalog : catalog = Task.Run(() => Enumerate(handle));
    private static unsafe IReadOnlyList<MicrophoneChoice> Enumerate(nint handle)
    {
        var values = new NativeAudioDevice[128];
        uint length;
        fixed (NativeAudioDevice* pointer = values)
            NativeMethods.EnsureSuccess(NativeMethods.AudioDevices(handle, pointer, 128, out length));
        var devices = new List<MicrophoneChoice> { new("", "Windows default microphone") };
        for (uint i = 0; i < length; ++i) devices.Add(new(values[i].DeviceId, values[i].DeviceName));
        return devices;
    }
    private async Task ConsumeAsync()
    {
        try
        {
            while (!cancellation.IsCancellationRequested)
            {
                lock (transfer) ReadAndTransfer();
                await Task.Delay(20, cancellation.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
        finally { Array.Clear(samples); }
    }
    private unsafe bool ReadAndTransfer()
    {
        uint count; ulong epoch;
        var snapshot = Snapshot;
        fixed (float* pointer = samples)
            NativeMethods.EnsureSuccess(NativeMethods.AudioRead(handle, pointer, (uint)samples.Length, out count, out epoch));
        try {
            if (sink is not null && count != 0 && snapshot.State is 2 or 4) {
                captureSession ??= epoch;
                if (captureSession != epoch || snapshot.Session != epoch || snapshot.DroppedFrames != 0 ||
                    !sink.Append(samples.AsSpan(0, checked((int)count)), checked((int)snapshot.SampleRate))) transferFailed = true;
            }
            if (sink is { EndRequested: true } && Snapshot.State == 0) {
                sink.Finish(); sink = null; captureSession = null;
            }
            return count != 0;
        } finally { Array.Clear(samples); }
    }
    public void Dispose()
    {
        cancellation.Cancel();
        try { consumer.GetAwaiter().GetResult(); }
        finally {
            try { catalog?.GetAwaiter().GetResult(); } catch (InvalidOperationException) { }
            cancellation.Dispose();
        }
    }
}
internal static partial class NativeMethods
{
    [LibraryImport(Library, EntryPoint = "dictator_host_configure_audio", StringMarshalling = StringMarshalling.Utf16)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial NativeResult HostConfigureAudio(nint host, string deviceId);
    [LibraryImport(Library, EntryPoint = "dictator_host_audio_handle")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial nint HostAudioHandle(nint host);
    [LibraryImport(Library, EntryPoint = "dictator_audio_devices")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static unsafe partial NativeResult AudioDevices(nint audio, NativeAudioDevice* devices, uint capacity, out uint count);
    [LibraryImport(Library, EntryPoint = "dictator_audio_status")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial NativeResult AudioStatus(nint audio, out NativeAudioSnapshot snapshot);
    [LibraryImport(Library, EntryPoint = "dictator_audio_read")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static unsafe partial NativeResult AudioRead(nint audio, float* samples, uint capacity, out uint count, out ulong session);
}
