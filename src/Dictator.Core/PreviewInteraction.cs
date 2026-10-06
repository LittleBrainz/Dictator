namespace Dictator.Core;

public enum PreviewInput : uint { Hotkey = 1, Microphone = 2 }

// Monotonic input timestamps, rather than dispatcher timing, define the boundary.
// A gesture owns its release. Target identity is metadata, never target text.
public sealed class PreviewInteraction
{
    public const ulong HoldMilliseconds = 500;
    public bool Talking { get; private set; }
    public ulong Target { get; private set; }
    private PreviewInput? source;
    private ulong pressedAt;
    private bool startedOn;

    public void RefreshTarget(ulong eligibleTarget)
    {
        if (Talking && Target != eligibleTarget) Cancel();
    }
    public void Press(PreviewInput input, ulong timestamp, ulong eligibleTarget)
    {
        if (source is not null || eligibleTarget == 0) return;
        RefreshTarget(eligibleTarget);
        source = input;
        pressedAt = timestamp;
        startedOn = Talking;
        Talking = true;
        Target = eligibleTarget;
    }
    public void Release(PreviewInput input, ulong timestamp)
    {
        if (source != input) return;
        source = null;
        if (startedOn || timestamp < pressedAt || timestamp - pressedAt >= HoldMilliseconds) Cancel();
    }
    public void Cancel()
    {
        Talking = false;
        Target = 0;
        source = null;
    }
}
