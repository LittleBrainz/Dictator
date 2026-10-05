using System.IO.Pipes;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;

namespace Dictator.App.Lifecycle;

// Current-user-only pipe + same-user/session mutex. No elevated broker or socket.
// Mutex ownership stays on Main's STA thread; all pipe work is cancellable.
internal sealed class SingleInstance : IDisposable
{
    private readonly Mutex mutex;
    private readonly string pipeName;
    private readonly CancellationTokenSource stopping = new();
    private Task? listener;
    internal bool IsOwner { get; }
    internal SingleInstance(string dataRoot, bool claimOwnership = true)
    {
        var identity = WindowsIdentity.GetCurrent().User?.Value ?? throw new InvalidOperationException("User identity unavailable.");
        var scope = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(identity + "|" +
            System.Diagnostics.Process.GetCurrentProcess().SessionId + "|" + Path.GetFullPath(dataRoot).ToUpperInvariant())));
        pipeName = "Dictator-" + scope;
        mutex = new Mutex(false, "Local\\" + pipeName);
        // Test control clients never claim residence during the brief restart gap.
        // They wait for the new server, rather than competing with its launch.
        if (!claimOwnership) return;
        try { IsOwner = mutex.WaitOne(0); }
        catch (AbandonedMutexException) { IsOwner = true; }
    }
    internal async Task<string> SendAsync(string command)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        using var pipe = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
        await pipe.ConnectAsync(timeout.Token).ConfigureAwait(false);
        using var writer = new StreamWriter(pipe, leaveOpen: true) { AutoFlush = true };
        using var reader = new StreamReader(pipe, leaveOpen: true);
        await writer.WriteLineAsync(command.AsMemory(), timeout.Token).ConfigureAwait(false);
        return await reader.ReadLineAsync(timeout.Token).ConfigureAwait(false) ?? throw new IOException("Existing instance did not acknowledge activation.");
    }
    internal void Listen(Func<string, Task<string>> dispatch)
    {
        if (!IsOwner || listener is not null) throw new InvalidOperationException("Invalid instance listener ownership.");
        listener = Task.Run(async () =>
        {
            while (!stopping.IsCancellationRequested)
            {
                try
                {
                    using var pipe = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
                        PipeTransmissionMode.Byte, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
                    await pipe.WaitForConnectionAsync(stopping.Token).ConfigureAwait(false);
                    using var deadline = CancellationTokenSource.CreateLinkedTokenSource(stopping.Token);
                    deadline.CancelAfter(TimeSpan.FromSeconds(10));
                    using var reader = new StreamReader(pipe, leaveOpen: true);
                    using var writer = new StreamWriter(pipe, leaveOpen: true) { AutoFlush = true };
                    // Bound the request before allocation. Only tiny named commands are accepted.
                    var command = new StringBuilder();
                    var character = new char[1];
                    while (command.Length <= 64)
                    {
                        if (await reader.ReadAsync(character.AsMemory(), deadline.Token).ConfigureAwait(false) == 0 || character[0] == '\n') break;
                        if (character[0] != '\r') command.Append(character[0]);
                    }
                    if (command.Length > 64) continue;
                    string response;
                    try { response = await dispatch(command.ToString()).WaitAsync(deadline.Token).ConfigureAwait(false); }
                    catch (ArgumentException) { response = "{\"status\":\"error\",\"message\":\"Unknown activation command\"}"; }
                    await writer.WriteLineAsync(response.AsMemory(), deadline.Token).ConfigureAwait(false);
                }
                catch (Exception error) when (error is IOException or OperationCanceledException) { }
            }
        });
    }
    public void Dispose()
    {
        stopping.Cancel();
        // UI pump has already stopped; handlers cannot synchronously block shutdown.
        if (listener is not null) { try { listener.Wait(TimeSpan.FromSeconds(1)); } catch (AggregateException) { } }
        if (IsOwner) mutex.ReleaseMutex();
        mutex.Dispose();
        stopping.Dispose();
    }
}
