using System.Buffers.Binary;

namespace Dictator.Core.Transcription;

// Streaming, windowed-sinc low-pass converter. Fixed history and phase persist
// across chunks; it does not allocate on the native capture thread.
public sealed class Pcm16Resampler
{
    public const int OutputRate = 24000;
    private const int Radius = 16;
    private readonly float[] history = new float[64];
    private readonly double step, cutoff;
    private long received;
    private double position;
    private bool flushed;
    public Pcm16Resampler(int inputRate)
    {
        if (inputRate is < 8000 or > 192000) throw new ArgumentOutOfRangeException(nameof(inputRate));
        step = (double)inputRate / OutputRate;
        cutoff = Math.Min(1, (double)OutputRate / inputRate) * .94;
    }
    public byte[] Convert(ReadOnlySpan<float> samples)
    {
        if (flushed) throw new InvalidOperationException("Audio converter already finalized.");
        var output = new byte[checked(((int)Math.Ceiling(samples.Length / step) + 4) * 2)];
        var used = 0;
        foreach (var sample in samples)
        {
            history[received++ % history.Length] = float.IsFinite(sample) ? Math.Clamp(sample, -1, 1) : 0;
            Emit(output, ref used, received - Radius);
        }
        return Trim(output, used);
    }
    public byte[] Flush()
    {
        if (flushed) return [];
        flushed = true;
        var limit = received;
        var output = new byte[checked(((int)Math.Ceiling(Radius / step) + 4) * 2)];
        var used = 0;
        for (var i = 0; i < Radius; ++i) {
            history[received++ % history.Length] = 0;
            Emit(output, ref used, Math.Min(limit, received - Radius));
        }
        Array.Clear(history); return Trim(output, used);
    }
    private static byte[] Trim(byte[] output, int used)
    {
        if (output.Length == used) return output;
        var result = output.AsSpan(0, used).ToArray(); Array.Clear(output); return result;
    }
    public void Clear() { Array.Clear(history); flushed = true; }
    private void Emit(byte[] output, ref int used, long limit)
    {
        while (position < limit)
        {
            var center = (long)Math.Floor(position);
            double sum = 0, weight = 0;
            for (var i = center - Radius + 1; i <= center + Radius; ++i)
            {
                var distance = position - i;
                if (Math.Abs(distance) >= Radius) continue;
                var argument = Math.PI * cutoff * distance;
                var sinc = Math.Abs(argument) < 1e-9 ? cutoff : cutoff * Math.Sin(argument) / argument;
                var coefficient = sinc * (.5 + .5 * Math.Cos(Math.PI * distance / Radius));
                weight += coefficient;
                if (i >= 0 && i < received) sum += history[i % history.Length] * coefficient;
            }
            var value = Math.Clamp(sum / weight, -1, 1);
            var integer = (short)Math.Clamp((int)Math.Round(value * 32768), short.MinValue, short.MaxValue);
            BinaryPrimitives.WriteInt16LittleEndian(output.AsSpan(used, 2), integer); used += 2;
            position += step;
        }
    }
}
