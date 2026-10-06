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
    internal void SetWidget(bool visible, Preferences preferences) => NativeMethods.EnsureSuccess(
        NativeMethods.HostSetWidget(handle, visible ? 1u : 0u, preferences.WidgetZoom, (uint)preferences.Theme));
    public void Dispose()
    {
        if (handle != 0) { NativeMethods.HostDestroy(handle); handle = 0; }
    }
}

internal static partial class NativeMethods
{
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
