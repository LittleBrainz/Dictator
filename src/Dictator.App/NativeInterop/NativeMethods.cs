using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Dictator.App.NativeInterop;

internal enum NativeResult : uint
{
    Ok = 0, InvalidArgument = 1, AbiMismatch = 2, BufferTooSmall = 3, OutOfMemory = 4,
    PlatformError = 5, WrongThread = 6
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct NativeSnapshot
{
    public uint StructSize;
    public uint AbiVersion;
    public ulong Sequence;
    public uint OwnerThreadId;
    public uint PollThreadId;
    public static NativeSnapshot Create() => new() { StructSize = 24 };
}

internal sealed class NativeContext : SafeHandleZeroOrMinusOneIsInvalid
{
    internal NativeContext(nint value) : base(ownsHandle: true) => SetHandle(value);
    protected override bool ReleaseHandle()
    {
        NativeMethods.DestroyContext(handle);
        return true;
    }
}

internal static partial class NativeMethods
{
    private const string Library = "Dictator.Native";
    internal const uint AbiVersion = 1;
    private static string? libraryPath;

    // Called once before any P/Invoke. Distribution path is based on the executable,
    // not cwd or the managed host's lib/WinUI directory.
    internal static void Initialize(string distributionRoot)
    {
        if (libraryPath is not null) throw new InvalidOperationException("Native resolver is already initialized.");
        libraryPath = Path.GetFullPath(Path.Combine(distributionRoot, "lib", "Native",
            OperatingSystem.IsWindows() ? "Dictator.Native.dll" : "libDictator.Native.so"));
        if (!File.Exists(libraryPath))
            throw new FileNotFoundException("Dictator's native DLL is missing. Extract the entire Dictator artifact again.", libraryPath);
        NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
    }

    private static nint Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
        => name == Library ? NativeLibrary.Load(libraryPath!) : nint.Zero;

    internal static NativeContext Create(uint abi = AbiVersion)
    {
        var result = CreateContext(abi, out var handle);
        EnsureSuccess(result);
        return new NativeContext(handle);
    }

    internal static void EnsureSuccess(NativeResult result)
    {
        if (result != NativeResult.Ok)
            throw new InvalidOperationException($"Native ABI operation failed: {result} ({(uint)result}).");
    }

    internal static unsafe string RoundTrip(NativeContext context, string text)
    {
        fixed (char* input = text)
        {
            var result = CopyText(context, input, (uint)text.Length, null, 0, out var required);
            if (result != NativeResult.BufferTooSmall) EnsureSuccess(result);
            var output = new char[checked((int)required)];
            fixed (char* destination = output)
                EnsureSuccess(CopyText(context, input, (uint)text.Length, destination, required, out _));
            return new string(output, 0, text.Length);
        }
    }

    [LibraryImport(Library, EntryPoint = "dictator_get_abi_version")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint GetAbiVersion();

    [LibraryImport(Library, EntryPoint = "dictator_get_live_context_count")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint GetLiveContextCount();

    [LibraryImport(Library, EntryPoint = "dictator_create_context")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial NativeResult CreateContext(uint abi, out nint handle);

    [LibraryImport(Library, EntryPoint = "dictator_destroy_context")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void DestroyContext(nint handle);

    [LibraryImport(Library, EntryPoint = "dictator_poll")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial NativeResult Poll(NativeContext context, ref NativeSnapshot snapshot);

    [LibraryImport(Library, EntryPoint = "dictator_copy_text")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static unsafe partial NativeResult CopyText(NativeContext context, char* input,
        uint inputCount, char* output, uint capacity, out uint required);
}
