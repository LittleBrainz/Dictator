using System.Runtime.InteropServices;
using Dictator.App.NativeInterop;

[assembly: CollectionBehavior(DisableTestParallelization = true)]

namespace Dictator.Interop.Tests;

public sealed class NativeContractTests
{
    [Fact]
    public void MetadataLayoutsMatchTheNativeX64Contract()
    {
        Assert.Equal(48, Marshal.SizeOf<NativeTarget>());
        Assert.Equal(24, Marshal.SizeOf<NativeInput>());
        Assert.Equal(32, Marshal.OffsetOf<NativeTarget>(nameof(NativeTarget.ProcessId)).ToInt32());
        Assert.Equal(16, Marshal.OffsetOf<NativeInput>(nameof(NativeInput.Source)).ToInt32());
        Assert.Equal(64, Marshal.SizeOf<NativeAudioSnapshot>());
        Assert.Equal(1540, Marshal.SizeOf<NativeAudioDevice>());
        Assert.Equal(32, Marshal.OffsetOf<NativeAudioSnapshot>(nameof(NativeAudioSnapshot.State)).ToInt32());
    }
    static NativeContractTests()
    {
        var root = Environment.GetEnvironmentVariable("DICTATOR_DISTRIBUTION_ROOT")
            ?? throw new InvalidOperationException("Set DICTATOR_DISTRIBUTION_ROOT to the staged artifact root.");
        NativeMethods.Initialize(root);
    }

    [Fact]
    public async Task AudioConsumerAndCatalogJoinBeforeHostDestruction()
    {
        if (OperatingSystem.IsWindows()) await Task.Run(VerifyAudioLifetime);
    }
    private static void VerifyAudioLifetime()
    {
        // Synchronous ownership keeps host create/configure/destroy on one thread.
        // Enumeration and chunk reads execute in production background tasks.
        using var host = new ResidentHost();
        host.ConfigureAudio("");
        using var audio = new AudioBridge(host.AudioHandle);
        var choices = audio.DevicesAsync().GetAwaiter().GetResult();
        Assert.Equal("", choices[0].Id);
        Assert.All(choices.Skip(1), item => Assert.InRange(item.Id.Length, 1, 511));
        Assert.NotEqual(2u, audio.Snapshot.State); // Enumeration never starts capture.
        host.ConfigureAudio("Dictator.Missing.Test.Microphone");
        Assert.NotEqual(2u, audio.Snapshot.State);
        var audioHandle = host.AudioHandle;
        Assert.Equal(NativeResult.Ok, Task.Run(() => NativeMethods.AudioCancel(audioHandle)).GetAwaiter().GetResult());
    }

    [Fact]
    public void CredentialManagerRoundTripUsesAnIsolatedTarget()
    {
        if (!OperatingSystem.IsWindows()) return;
        var store = new CredentialStore("Dictator/Test/" + Guid.NewGuid());
        try {
            Assert.False(store.Exists); Assert.Null(store.Read());
            store.Save("fixture-key-never-a-real-secret");
            Assert.True(store.Exists); Assert.Equal("fixture-key-never-a-real-secret", store.Read());
            Assert.Throws<InvalidOperationException>(() => store.Save("bad key with whitespace"));
            Assert.Equal("fixture-key-never-a-real-secret", store.Read());
            store.Remove(); Assert.False(store.Exists); Assert.Null(store.Read());
        } finally { store.Remove(); }
    }

    [Fact]
    public void VersionAndStructLayoutAreExact()
    {
        Assert.Equal(1u, NativeMethods.GetAbiVersion());
        Assert.Equal(24, Marshal.SizeOf<NativeSnapshot>());
        Assert.Equal(8, Marshal.OffsetOf<NativeSnapshot>(nameof(NativeSnapshot.Sequence)).ToInt32());
        Assert.Equal(20, Marshal.OffsetOf<NativeSnapshot>(nameof(NativeSnapshot.PollThreadId)).ToInt32());
    }

    [Fact]
    public void ErrorCodesSurviveBoundaryWithoutAllocating()
    {
        var before = NativeMethods.GetLiveContextCount();
        Assert.Equal(NativeResult.AbiMismatch, NativeMethods.CreateContext(2, out var handle));
        Assert.Equal(nint.Zero, handle);
        using var context = NativeMethods.Create();
        var incompatible = new NativeSnapshot { StructSize = 23 };
        Assert.Equal(NativeResult.AbiMismatch, NativeMethods.Poll(context, ref incompatible));
        Assert.Equal(before + 1, NativeMethods.GetLiveContextCount());
    }

    [Theory]
    [InlineData("")]
    [InlineData("Dictator — 中文 😃")]
    [InlineData("before\0after")]
    public void Utf16RoundTripPreservesEveryCodeUnit(string input)
    {
        using var context = NativeMethods.Create();
        Assert.Equal(input, NativeMethods.RoundTrip(context, input));
    }

    [Fact]
    public unsafe void ShortBufferIsNotModified()
    {
        using var context = NativeMethods.Create();
        var output = new char[] { 'x', 'x', 'x' };
        fixed (char* input = "abc")
        fixed (char* destination = output)
        {
            Assert.Equal(NativeResult.BufferTooSmall, NativeMethods.CopyText(context, input, 3, destination, 3, out var required));
            Assert.Equal(4u, required);
        }
        Assert.Equal("xxx", new string(output));
    }

    [Fact]
    public async Task PollRunsOnCallingConsumerThread()
    {
        using var context = NativeMethods.Create();
        var first = NativeSnapshot.Create();
        Assert.Equal(NativeResult.Ok, NativeMethods.Poll(context, ref first));
        Assert.Equal(first.OwnerThreadId, first.PollThreadId);
        var completed = new TaskCompletionSource<NativeSnapshot>(TaskCreationOptions.RunContinuationsAsynchronously);
        var worker = new Thread(() =>
        {
            try
            {
                var value = NativeSnapshot.Create();
                Assert.Equal(NativeResult.Ok, NativeMethods.Poll(context, ref value));
                completed.SetResult(value);
            }
            catch (Exception error) { completed.SetException(error); }
        });
        worker.Start();
        var second = await completed.Task;
        Assert.Equal(2ul, second.Sequence);
        Assert.Equal(first.OwnerThreadId, second.OwnerThreadId);
        Assert.NotEqual(second.OwnerThreadId, second.PollThreadId);
    }

    [Fact]
    public void SafeHandleBalancesTenThousandCreateDestroyCycles()
    {
        var before = NativeMethods.GetLiveContextCount();
        for (var i = 0; i < 10000; i++)
        {
            using var context = NativeMethods.Create();
            Assert.False(context.IsInvalid);
        }
        Assert.Equal(before, NativeMethods.GetLiveContextCount());
    }
}
