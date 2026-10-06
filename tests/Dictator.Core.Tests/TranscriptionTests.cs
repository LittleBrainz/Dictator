using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Dictator.Core.Transcription;

namespace Dictator.Core.Tests;

public sealed class TranscriptionTests
{
    [Theory]
    [InlineData(8000)] [InlineData(16000)] [InlineData(24000)] [InlineData(44100)] [InlineData(48000)] [InlineData(192000)]
    public void ResamplingPreservesPhaseAcrossChunksAndFlushesTheTail(int rate)
    {
        var input = Enumerable.Range(0, rate / 10).Select(i => (float)(.5 * Math.Sin(i * 2 * Math.PI * 500 / rate))).ToArray();
        var whole = new Pcm16Resampler(rate);
        var expected = whole.Convert(input).Concat(whole.Flush()).ToArray();
        var parts = new Pcm16Resampler(rate); var actual = new List<byte>();
        for (var offset = 0; offset < input.Length; offset += 37) actual.AddRange(parts.Convert(input.AsSpan(offset, Math.Min(37, input.Length - offset))));
        actual.AddRange(parts.Flush());
        Assert.Equal(expected, actual.ToArray());
        Assert.InRange(expected.Length / 2, 2399, 2401);
        Assert.Empty(parts.Flush());
    }
    [Fact]
    public void ResamplingFiltersAliasingAndRejectsUnsupportedRates()
    {
        static double Rms(int frequency) {
            var converter = new Pcm16Resampler(48000);
            var input = Enumerable.Range(0, 4800).Select(i => (float)(.5 * Math.Sin(i * 2 * Math.PI * frequency / 48000))).ToArray();
            var bytes = converter.Convert(input).Concat(converter.Flush()).ToArray();
            return Math.Sqrt(Enumerable.Range(64, bytes.Length / 2 - 128).Average(i => Math.Pow(BinaryPrimitives.ReadInt16LittleEndian(bytes.AsSpan(i * 2, 2)) / 32768.0, 2)));
        }
        Assert.InRange(Rms(1000), .34, .37);
        Assert.True(Rms(18000) < .02);
        Assert.Throws<ArgumentOutOfRangeException>(() => new Pcm16Resampler(4000));
        var silent = new Pcm16Resampler(24000);
        Assert.All(silent.Convert(Enumerable.Repeat(float.NaN, 50).ToArray()).Concat(silent.Flush()), b => Assert.Equal((byte)0, b));
    }
    [Fact]
    public async Task StreamsDeltasBeforeCommitAndFinalizesWithAnAuthoritativeTranscript()
    {
        using var fixture = new Server();
        var exercise = fixture.Serve(async socket => {
            using var update = await Server.Read(socket);
            var input = update.RootElement.GetProperty("session").GetProperty("audio").GetProperty("input");
            Assert.Equal("session.update", update.RootElement.GetProperty("type").GetString());
            Assert.Equal(OpenAiTranscriptionProvider.SupportedModel, input.GetProperty("transcription").GetProperty("model").GetString());
            Assert.Equal(JsonValueKind.Null, input.GetProperty("turn_detection").ValueKind);
            Assert.False(input.GetProperty("transcription").TryGetProperty("language", out _));
            await Server.Acknowledge(socket, update);
            using var audio = await Server.Read(socket);
            Assert.Equal("input_audio_buffer.append", audio.RootElement.GetProperty("type").GetString());
            Assert.Equal(4800, Convert.FromBase64String(audio.RootElement.GetProperty("audio").GetString()!).Length);
            await Server.Send(socket, new { type = "conversation.item.input_audio_transcription.delta", item_id = "turn", delta = "Bonjour 中文 😃" }, fragmented: true);
            using var commit = await Server.Read(socket);
            Assert.Equal("input_audio_buffer.commit", commit.RootElement.GetProperty("type").GetString());
            await Server.Send(socket, new { type = "conversation.item.input_audio_transcription.completed", item_id = "turn", transcript = "Bonjour 中文 😃." });
        });
        await using var session = new OpenAiTranscriptionProvider(fixture.Endpoint).CreateSession();
        var running = session.StartAsync("fixture-key");
        var pcm = new byte[4800]; pcm[0] = 42;
        Assert.True(session.TryAppend(pcm));
        var live = await ReadNotice(session);
        Assert.Equal(TranscriptEventKind.Delta, live.Kind); Assert.Equal("Bonjour 中文 😃", live.Text);
        session.CompleteAudio();
        await running.WaitAsync(TimeSpan.FromSeconds(5)); await exercise;
        Assert.Equal(TranscriptionState.Completed, session.State); Assert.Null(session.Failure);
        var final = await ReadNotice(session); Assert.Equal(TranscriptEventKind.Final, final.Kind); Assert.Equal("Bonjour 中文 😃.", final.Text);
        Assert.All(pcm, b => Assert.Equal((byte)0, b));
        Assert.Contains("Authorization: Bearer fixture-key", fixture.Headers, StringComparison.OrdinalIgnoreCase);
    }
    [Theory]
    [InlineData("invalid_api_key", "authentication")]
    [InlineData("insufficient_quota", "quota")]
    [InlineData("rate_limit_exceeded", "rate_limit")]
    [InlineData("model_not_found", "access")]
    [InlineData("anything-private", "provider_error")]
    public async Task ProviderErrorsAreActionableAndNeverEchoSecretsOrTranscript(string serverCode, string expected)
    {
        using var fixture = new Server();
        var exercise = fixture.Serve(async socket => {
            using var update = await Server.Read(socket);
            await Server.Acknowledge(socket, update);
            using var chunk = await Server.Read(socket);
            await Server.Send(socket, new { type = "error", error = new { code = serverCode, message = "SECRET fixture-key PRIVATE-TRANSCRIPT" } });
        });
        await using var session = new OpenAiTranscriptionProvider(fixture.Endpoint).CreateSession();
        var running = session.StartAsync("fixture-key");
        Assert.True(session.TryAppend(new byte[4800]));
        await running.WaitAsync(TimeSpan.FromSeconds(5)); await exercise;
        Assert.Equal(TranscriptionState.Failed, session.State); Assert.Equal(expected, session.Failure!.Code);
        Assert.DoesNotContain("SECRET", session.Failure.Message); Assert.DoesNotContain("fixture-key", session.Failure.Message);
        Assert.DoesNotContain("PRIVATE-TRANSCRIPT", session.Failure.Message);
        Assert.False(session.TryRead(out _));
    }
    [Fact]
    public async Task CancellationClosesTransportAndPreventsFinalDelivery()
    {
        using var fixture = new Server();
        var received = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var exercise = fixture.Serve(async socket => {
            using var update = await Server.Read(socket); await Server.Acknowledge(socket, update);
            using var chunk = await Server.Read(socket); received.SetResult();
            try { using var ignored = await Server.Read(socket); } catch (Exception) { }
        });
        await using var session = new OpenAiTranscriptionProvider(fixture.Endpoint).CreateSession();
        var running = session.StartAsync("fixture-key"); Assert.True(session.TryAppend(new byte[4800]));
        await received.Task.WaitAsync(TimeSpan.FromSeconds(5)); session.Cancel();
        await running.WaitAsync(TimeSpan.FromSeconds(5)); await exercise;
        Assert.Equal(TranscriptionState.Cancelled, session.State); Assert.Null(session.Failure); Assert.False(session.TryRead(out _));
    }
    [Fact]
    public async Task SendQueueIsBoundedAndDisposalWipesEveryOwnedChunk()
    {
        var session = new OpenAiTranscriptionProvider().CreateSession();
        var buffers = Enumerable.Range(0, 256).Select(_ => new byte[] { 123, 45 }).ToArray();
        Assert.All(buffers, chunk => Assert.True(session.TryAppend(chunk)));
        Assert.False(session.TryAppend([1, 2])); Assert.Equal("send_backlog", session.Failure!.Code);
        await session.DisposeAsync();
        Assert.All(buffers, chunk => Assert.All(chunk, b => Assert.Equal((byte)0, b)));
    }
    [Fact]
    public async Task MissingAcknowledgmentIsCancelledWithoutSendingAudio()
    {
        using var fixture = new Server(); var received = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var exercise = fixture.Serve(async socket => {
            using var update = await Server.Read(socket); received.SetResult();
            try { using var unexpected = await Server.Read(socket); Assert.Fail("Audio sent before configuration acknowledgment."); } catch (WebSocketException) { }
        });
        await using var session = new OpenAiTranscriptionProvider(fixture.Endpoint).CreateSession();
        var running = session.StartAsync("fixture-key"); Assert.True(session.TryAppend(new byte[4800]));
        await received.Task.WaitAsync(TimeSpan.FromSeconds(5)); session.Cancel();
        await running.WaitAsync(TimeSpan.FromSeconds(5)); await exercise;
        Assert.Equal(TranscriptionState.Cancelled, session.State);
    }
    [Fact]
    public async Task NewPresentationOrCancellationRejectsOldLiveText()
    {
        await using var session = new OpenAiTranscriptionProvider().CreateSession();
        var run = new TranscriptionRun(session, 8, 12);
        Assert.True(run.MustCancelForTarget(0)); Assert.False(run.MustCancelForTarget(8));
        Assert.True(run.CanDisplay(8, 12)); Assert.False(run.CanDisplay(8, 13)); Assert.False(run.CanDisplay(9, 12));
        Assert.True(run.Append(new float[100], 24000));
        run.RequestFinish(); Assert.False(run.CanDisplay(8, 12)); Assert.False(run.MustCancelForTarget(0));
        Assert.True(run.Append(new float[100], 24000)); // Final native drain is accepted.
        run.Finish(); Assert.True(run.Finishing); Assert.False(run.Append(new float[100], 24000));
        run.Cancel(); Assert.True(run.Invalidated); Assert.False(run.CanDisplay(8, 12));
    }
    [Theory]
    [InlineData(401, "authentication")] [InlineData(403, "access")] [InlineData(429, "limit")]
    public async Task FailedHttpUpgradeIsClassifiedWithoutEchoingResponseContent(int status, string code)
    {
        using var fixture = new Server();
        var exercise = fixture.Serve(_ => Task.CompletedTask, status);
        await using var session = new OpenAiTranscriptionProvider(fixture.Endpoint).CreateSession();
        await session.StartAsync("fixture-key").WaitAsync(TimeSpan.FromSeconds(5)); await exercise;
        Assert.Equal(TranscriptionState.Failed, session.State); Assert.Equal(code, session.Failure!.Code);
    }
    [Fact]
    public async Task ShortSourceAudioIsResampledFlushedAndPaddedBeforeCommit()
    {
        using var fixture = new Server(); var frames = 0;
        var exercise = fixture.Serve(async socket => {
            using var update = await Server.Read(socket); await Server.Acknowledge(socket, update);
            while (true) {
                using var message = await Server.Read(socket);
                if (message.RootElement.GetProperty("type").GetString() == "input_audio_buffer.commit") break;
                frames += Convert.FromBase64String(message.RootElement.GetProperty("audio").GetString()!).Length / 2;
            }
            await Server.Send(socket, new { type = "conversation.item.input_audio_transcription.completed", item_id = "short", transcript = "Hi." });
        });
        await using var session = new OpenAiTranscriptionProvider(fixture.Endpoint).CreateSession();
        var running = session.StartAsync("fixture-key");
        var run = new TranscriptionRun(session, 5, 10);
        Assert.True(run.Append(new float[480], 48000)); run.RequestFinish();
        Assert.True(run.Append(new float[480], 48000)); run.Finish();
        await running.WaitAsync(TimeSpan.FromSeconds(5)); await exercise;
        Assert.Equal(2400, frames); Assert.Equal(TranscriptionState.Completed, session.State);
        Assert.Equal("Hi.", (await ReadNotice(session)).Text);
    }
    [Fact]
    public async Task WrongSessionAcknowledgmentFailsBeforeAudioIsSent()
    {
        using var fixture = new Server();
        var exercise = fixture.Serve(async socket => {
            using var update = await Server.Read(socket);
            await Server.Send(socket, new { type = "session.updated", session = new { type = "realtime", audio = new { input = new {
                format = new { type = "audio/pcm", rate = 24000 }, transcription = new { model = "unexpected" }, turn_detection = (object?)null
            } } } });
        });
        await using var session = new OpenAiTranscriptionProvider(fixture.Endpoint).CreateSession();
        var running = session.StartAsync("fixture-key"); Assert.True(session.TryAppend(new byte[4800]));
        await running.WaitAsync(TimeSpan.FromSeconds(5)); await exercise;
        Assert.Equal("configuration", session.Failure!.Code); Assert.False(session.TryRead(out _));
    }
    [Fact]
    public async Task InterruptedConnectionFailsWithoutPublishingAPartialFinal()
    {
        using var fixture = new Server();
        var exercise = fixture.Serve(async socket => {
            using var update = await Server.Read(socket); await Server.Acknowledge(socket, update);
            using var audio = await Server.Read(socket); socket.Abort();
        });
        await using var session = new OpenAiTranscriptionProvider(fixture.Endpoint).CreateSession();
        var running = session.StartAsync("fixture-key"); Assert.True(session.TryAppend(new byte[4800]));
        await running.WaitAsync(TimeSpan.FromSeconds(5)); await exercise;
        Assert.Equal("network", session.Failure!.Code); Assert.False(session.TryRead(out _));
    }
    private static async Task<TranscriptEvent> ReadNotice(ITranscriptionSession session)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (!timeout.IsCancellationRequested) {
            if (session.TryRead(out var value)) return value!;
            if (session.Failure is { } error) Assert.Fail(error.Code);
            await Task.Delay(5, timeout.Token);
        }
        throw new TimeoutException();
    }
    // Actual ClientWebSocket transport, with a local protocol fixture. Never uses
    // a real API key, external service, microphone or persisted recording.
    private sealed class Server : IDisposable
    {
        private readonly TcpListener listener = new(IPAddress.Loopback, 0);
        public Uri Endpoint { get; }
        public string Headers { get; private set; } = "";
        public Server() { listener.Start(); Endpoint = new($"ws://127.0.0.1:{((IPEndPoint)listener.LocalEndpoint).Port}/realtime?intent=transcription"); }
        public async Task Serve(Func<WebSocket, Task> exercise, int status = 101)
        {
            using var client = await listener.AcceptTcpClientAsync();
            using var stream = client.GetStream();
            var header = new List<byte>(); var one = new byte[1];
            while (header.Count < 8192) {
                if (await stream.ReadAsync(one) == 0) throw new IOException();
                header.Add(one[0]);
                if (header.Count >= 4 && header.TakeLast(4).SequenceEqual(new byte[] {13,10,13,10})) break;
            }
            Headers = Encoding.ASCII.GetString(header.ToArray());
            if (status != 101) {
                await stream.WriteAsync(Encoding.ASCII.GetBytes($"HTTP/1.1 {status} Rejected\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")); return;
            }
            var key = Headers.Split("\r\n").Single(s => s.StartsWith("Sec-WebSocket-Key:", StringComparison.OrdinalIgnoreCase)).Split(':', 2)[1].Trim();
            var accept = Convert.ToBase64String(SHA1.HashData(Encoding.ASCII.GetBytes(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")));
            await stream.WriteAsync(Encoding.ASCII.GetBytes($"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n"));
            using var socket = WebSocket.CreateFromStream(stream, true, null, TimeSpan.FromSeconds(20));
            await exercise(socket);
        }
        public static async Task<JsonDocument> Read(WebSocket socket)
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            var bytes = new byte[65536]; var count = 0; WebSocketReceiveResult result;
            do {
                result = await socket.ReceiveAsync(new ArraySegment<byte>(bytes, count, bytes.Length-count), timeout.Token);
                if (result.MessageType != WebSocketMessageType.Text) throw new WebSocketException();
                count += result.Count;
            } while (!result.EndOfMessage);
            return JsonDocument.Parse(bytes.AsMemory(0, count));
        }
        public static Task Acknowledge(WebSocket socket, JsonDocument update)
            => Send(socket, new { type = "session.updated", session = update.RootElement.GetProperty("session") });
        public static async Task Send<T>(WebSocket socket, T message, bool fragmented = false)
        {
            var bytes = JsonSerializer.SerializeToUtf8Bytes(message);
            if (fragmented) {
                await socket.SendAsync(bytes.AsMemory(0, bytes.Length / 2), WebSocketMessageType.Text, false, CancellationToken.None);
                await socket.SendAsync(bytes.AsMemory(bytes.Length / 2), WebSocketMessageType.Text, true, CancellationToken.None);
            } else await socket.SendAsync(bytes.AsMemory(), WebSocketMessageType.Text, true, CancellationToken.None);
        }
        public void Dispose() => listener.Stop();
    }
}
