using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Dictator.Core;

namespace Dictator.App.NativeInterop;

// Explicit UI-thread disposal rather than a finalizer: HWNDs cannot be destroyed
// on the SafeHandle finalizer thread. App owns this for its entire resident lifetime.
internal sealed class ResidentHost : IDisposable
{
    private nint handle;
    internal ResidentHost()
    {
        NativeMethods.EnsureSuccess(NativeMethods.HostCreate(NativeMethods.AbiVersion, out handle));
    }
    internal uint PollEvents() => NativeMethods.HostPollEvents(handle);
    internal nint WidgetHandle => NativeMethods.HostWidgetHandle(handle);
    internal bool TrayReady => NativeMethods.HostTrayReady(handle) != 0;
    internal bool BindHotkey(HotkeyPreference preference) => NativeMethods.HostBindHotkey(handle,
        (uint)preference.Modifiers, (uint)preference.Key, preference.Display.Replace('+', '-')) == NativeResult.Ok;
    internal NativeTarget Target
    {
        get { NativeMethods.EnsureSuccess(NativeMethods.HostTarget(handle, out var target)); return target; }
    }
    internal bool TryInput(out NativeInput input) => NativeMethods.HostInput(handle, out input) != 0;
    internal void SetPreview(ulong target) => NativeMethods.EnsureSuccess(NativeMethods.HostPreview(handle, target));
    internal void SetWidget(bool visible, Preferences preferences) => NativeMethods.EnsureSuccess(
        NativeMethods.HostSetWidget(handle, visible ? 1u : 0u, preferences.WidgetZoom, (uint)preferences.Theme));
    public void Dispose()
    {
        if (handle != 0) { NativeMethods.HostDestroy(handle); handle = 0; }
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeTarget
{
    public ulong Token;
    public nuint Foreground;
    public nuint Focus;
    public ulong CheckedAt;
    public uint ProcessId;
    public uint Eligible;
}
[StructLayout(LayoutKind.Sequential)]
internal struct NativeInput
{
    public ulong Timestamp;
    public ulong Target;
    public PreviewInput Source;
    public uint Down;
}

internal static partial class NativeMethods
{
    [LibraryImport(Library, EntryPoint = "dictator_host_bind_hotkey", StringMarshalling = StringMarshalling.Utf16)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial NativeResult HostBindHotkey(nint handle, uint modifiers, uint key, string display);
    [LibraryImport(Library, EntryPoint = "dictator_host_target")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial NativeResult HostTarget(nint handle, out NativeTarget target);
    [LibraryImport(Library, EntryPoint = "dictator_host_input")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint HostInput(nint handle, out NativeInput input);
    [LibraryImport(Library, EntryPoint = "dictator_host_preview")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial NativeResult HostPreview(nint handle, ulong target);
    [LibraryImport(Library, EntryPoint = "dictator_host_create")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial NativeResult HostCreate(uint abi, out nint handle);
    [LibraryImport(Library, EntryPoint = "dictator_host_destroy")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void HostDestroy(nint handle);
    [LibraryImport(Library, EntryPoint = "dictator_host_poll_events")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint HostPollEvents(nint handle);
    [LibraryImport(Library, EntryPoint = "dictator_host_set_widget")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial NativeResult HostSetWidget(nint handle, uint visible, double zoom, uint theme);
    [LibraryImport(Library, EntryPoint = "dictator_host_widget_handle")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial nint HostWidgetHandle(nint handle);
    [LibraryImport(Library, EntryPoint = "dictator_host_tray_ready")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint HostTrayReady(nint handle);
}
