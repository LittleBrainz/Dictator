namespace Dictator.Core.Transcription;

public enum TranscriptionState { Created, Connecting, Listening, Finalizing, Completed, Cancelled, Failed }
public enum TranscriptEventKind { Delta, Final }
public sealed record TranscriptEvent(TranscriptEventKind Kind, string Text);
public sealed record TranscriptionFailure(string Code, string Message);

// PCM16 little-endian, mono, 24 kHz. Successful TryAppend transfers ownership;
// the provider zeroes each chunk after sending/discarding it. No RT callbacks.
public interface ITranscriptionProvider
{
    string Name { get; }
    string Model { get; }
    ITranscriptionSession CreateSession();
}
public interface ITranscriptionSession : IAsyncDisposable
{
    TranscriptionState State { get; }
    TranscriptionFailure? Failure { get; }
    Task StartAsync(string apiKey, CancellationToken cancellationToken = default);
    bool TryAppend(byte[] pcm);
    bool TryRead(out TranscriptEvent? value);
    void CompleteAudio();
    void Cancel();
}
public sealed class TranscriptionException(string code, string message) : Exception(message)
{
    public string Code { get; } = code;
}
