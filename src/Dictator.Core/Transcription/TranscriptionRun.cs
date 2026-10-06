namespace Dictator.Core.Transcription;

// UI-thread lifecycle; sample conversion/TryAppend are serialized on the managed
// audio consumer. App detaches that consumer before Finish/Cancel. No native types.
public sealed class TranscriptionRun(ITranscriptionSession session, ulong target, ulong presentation)
{
    public ITranscriptionSession Session { get; } = session;
    public ulong Target { get; } = target;
    public ulong Presentation { get; } = presentation;
    public bool EndRequested { get; private set; }
    public bool Finishing { get; private set; }
    public bool Invalidated { get; private set; }
    private Pcm16Resampler? converter;
    private int rate;
    public void Start(string key) => _ = Session.StartAsync(key);
    public bool Append(ReadOnlySpan<float> samples, int sourceRate)
    {
        if (Invalidated || Finishing) return false;
        if (converter is null) { rate = sourceRate; converter = new(sourceRate); }
        if (sourceRate != rate) return false;
        var bytes = converter.Convert(samples);
        if (Session.TryAppend(bytes)) return true;
        Array.Clear(bytes); return false;
    }
    public void RequestFinish() => EndRequested = true;
    public void Finish()
    {
        if (Finishing || Invalidated) return;
        if (converter is not null) {
            var tail = converter.Flush();
            if (!Session.TryAppend(tail)) Array.Clear(tail);
        }
        Finishing = true; Session.CompleteAudio();
    }
    public void Cancel()
    {
        Invalidated = true; converter?.Clear(); Session.Cancel();
    }
    public bool CanDisplay(ulong currentTarget, ulong currentPresentation)
        => !Invalidated && !EndRequested && !Finishing && Target == currentTarget && Presentation == currentPresentation;
}
