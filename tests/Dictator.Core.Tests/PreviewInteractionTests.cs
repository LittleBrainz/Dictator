using Dictator.Core;

namespace Dictator.Core.Tests;

public class PreviewInteractionTests
{
    [Theory]
    [InlineData(499ul, true)]
    [InlineData(500ul, false)]
    [InlineData(501ul, false)]
    public void ExactBoundaryForBothSources(ulong duration, bool expected)
    {
        foreach (var source in Enum.GetValues<PreviewInput>())
        {
            var preview = new PreviewInteraction();
            preview.Press(source, 1000, 42);
            Assert.True(preview.Talking);
            preview.Release(source, 1000 + duration);
            Assert.Equal(expected, preview.Talking);
        }
    }
    [Theory]
    [InlineData(10ul)]
    [InlineData(600ul)]
    public void AlreadyOnStopsOnlyAtRelease(ulong duration)
    {
        var preview = new PreviewInteraction();
        preview.Press(PreviewInput.Hotkey, 1000, 42);
        preview.Release(PreviewInput.Hotkey, 1010);
        preview.Press(PreviewInput.Microphone, 2000, 42);
        Assert.True(preview.Talking);
        preview.Release(PreviewInput.Microphone, 2000 + duration);
        Assert.False(preview.Talking);
    }
    [Fact]
    public void RepeatAndOtherSourceCannotResetOrFinishGesture()
    {
        var preview = new PreviewInteraction();
        preview.Press(PreviewInput.Hotkey, 1000, 42);
        preview.Press(PreviewInput.Hotkey, 1400, 42);
        preview.Press(PreviewInput.Microphone, 1400, 42);
        preview.Release(PreviewInput.Microphone, 1410);
        Assert.True(preview.Talking);
        preview.Release(PreviewInput.Hotkey, 1500);
        Assert.False(preview.Talking);
    }
    [Fact]
    public void CaptureCancellationStopsItsGestureAndPreservesTheOtherSource()
    {
        foreach (var source in Enum.GetValues<PreviewInput>())
        {
            var other = source == PreviewInput.Hotkey ? PreviewInput.Microphone : PreviewInput.Hotkey;
            var preview = new PreviewInteraction();
            preview.Press(source, 1000, 42);
            preview.CancelInput(other);
            Assert.True(preview.Talking);
            preview.CancelInput(source);
            Assert.False(preview.Talking);
            preview.Release(source, 1010);
            Assert.False(preview.Talking);
        }
    }
    [Fact]
    public void IneligibleOrChangedTargetCancelsAndOldReleaseCannotRestart()
    {
        var preview = new PreviewInteraction();
        preview.Press(PreviewInput.Hotkey, 1000, 0);
        Assert.False(preview.Talking);
        preview.Press(PreviewInput.Hotkey, 2000, 42);
        preview.RefreshTarget(43);
        Assert.False(preview.Talking);
        preview.Release(PreviewInput.Hotkey, 2010);
        Assert.False(preview.Talking);
        preview.Press(PreviewInput.Microphone, 3000, 43);
        preview.RefreshTarget(0);
        Assert.False(preview.Talking);
    }
}
