using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Dictator.App.NativeInterop;

// Windows owns persistence. Settings/diagnostics never contain the secret.
internal sealed partial class CredentialStore(string target = "Dictator/OpenAI")
{
    internal bool Exists {
        get {
            if (CredRead(target, 1, 0, out var pointer) == 0) {
                if (Marshal.GetLastPInvokeError() == 1168) return false;
                throw new InvalidOperationException("Windows Credential Manager could not check the OpenAI credential.");
            }
            CredFree(pointer); return true;
        }
    }
    internal unsafe string? Read()
    {
        if (CredRead(target, 1, 0, out var pointer) == 0) {
            if (Marshal.GetLastPInvokeError() == 1168) return null;
            throw new InvalidOperationException("Windows Credential Manager could not read the OpenAI credential.");
        }
        try {
            var value = (Credential*)pointer;
            if (value->BlobSize > 4096 || value->Blob == 0) throw new InvalidOperationException("The stored OpenAI credential is invalid. Replace it in Speech settings.");
            return Encoding.UTF8.GetString(new ReadOnlySpan<byte>((void*)value->Blob, checked((int)value->BlobSize)));
        } finally { CredFree(pointer); }
    }
    internal unsafe void Save(string key)
    {
        key = key.Trim();
        if (key.Length is < 8 or > 1024 || key.Any(char.IsWhiteSpace) || key.Any(char.IsControl))
            throw new InvalidOperationException("Enter a complete OpenAI API key without spaces or line breaks.");
        var bytes = Encoding.UTF8.GetBytes(key);
        try {
            fixed (char* name = target)
            fixed (char* user = "Dictator")
            fixed (byte* secret = bytes) {
                var value = new Credential { Type = 1, TargetName = (nint)name, Blob = (nint)secret,
                    BlobSize = (uint)bytes.Length, Persist = 2, UserName = (nint)user };
                if (CredWrite(ref value, 0) == 0) throw new InvalidOperationException("Windows Credential Manager could not save the OpenAI credential.");
            }
        } finally { Array.Clear(bytes); }
    }
    internal void Remove()
    {
        if (CredDelete(target, 1, 0) == 0 && Marshal.GetLastPInvokeError() != 1168)
            throw new InvalidOperationException("Windows Credential Manager could not remove the OpenAI credential.");
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct Credential
    {
        public uint Flags, Type;
        public nint TargetName, Comment;
        public long LastWritten;
        public uint BlobSize;
        public nint Blob;
        public uint Persist, AttributeCount;
        public nint Attributes, TargetAlias, UserName;
    }
    [LibraryImport("advapi32.dll", EntryPoint = "CredReadW", StringMarshalling = StringMarshalling.Utf16, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    private static partial int CredRead(string target, uint type, uint flags, out nint credential);
    [LibraryImport("advapi32.dll", EntryPoint = "CredWriteW", SetLastError = true)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    private static partial int CredWrite(ref Credential credential, uint flags);
    [LibraryImport("advapi32.dll", EntryPoint = "CredDeleteW", StringMarshalling = StringMarshalling.Utf16, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    private static partial int CredDelete(string target, uint type, uint flags);
    [LibraryImport("advapi32.dll", EntryPoint = "CredFree")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    private static partial void CredFree(nint credential);
}
