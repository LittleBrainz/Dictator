using System.Net;
using System.Net.WebSockets;
using System.Text.Json;
using System.Threading.Channels;

namespace Dictator.Core.Transcription;

public sealed class OpenAiTranscriptionProvider : ITranscriptionProvider
{
    // Official Realtime transcription guide verified 6 October 2026.
    public const string SupportedModel = "gpt-live-transcribe";
    public static readonly Uri Endpoint = new("wss://api.openai.com/v1/realtime?intent=transcription");
    public string Name => "OpenAI";
    public string Model => SupportedModel;
    private readonly Uri endpoint;
    public OpenAiTranscriptionProvider() : this(Endpoint) { }
    internal OpenAiTranscriptionProvider(Uri endpoint) => this.endpoint = endpoint;
    public ITranscriptionSession CreateSession() => new Session(endpoint);

    private sealed class Session(Uri endpoint) : ITranscriptionSession
    {
        private const int MaxTranscript = 65536, MaxMessage = 262144;
        private readonly Channel<byte[]> audio = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(256) {
            SingleReader = true, SingleWriter = false, FullMode = BoundedChannelFullMode.Wait });
        private readonly Channel<TranscriptEvent> notices = Channel.CreateBounded<TranscriptEvent>(128);
        private readonly CancellationTokenSource cancel = new();
        private readonly ClientWebSocket socket = new();
        private readonly TaskCompletionSource ready = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource<string> final = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private Task? run;
        private int state, started, completing, committed, deltaCharacters;
        private TranscriptionFailure? failure;
        private string? itemId;
        public Guid SessionId { get; } = Guid.NewGuid();
        public TranscriptionState State => (TranscriptionState)Volatile.Read(ref state);
        public TranscriptionFailure? Failure => Volatile.Read(ref failure);
        public Task StartAsync(string apiKey, CancellationToken cancellationToken = default)
        {
            if (Interlocked.Exchange(ref started, 1) != 0) throw new InvalidOperationException("Session already started.");
            return run = RunAsync(apiKey, cancellationToken);
        }
        public bool TryAppend(byte[] pcm)
        {
            if (pcm.Length == 0) { Array.Clear(pcm); return true; }
            if (pcm.Length > 48000 || (pcm.Length & 1) != 0 || Volatile.Read(ref completing) != 0 ||
                State is TranscriptionState.Completed or TranscriptionState.Failed or TranscriptionState.Cancelled) return false;
            if (audio.Writer.TryWrite(pcm)) return true;
            Fail(new("send_backlog", "The connection could not keep up with audio. Check your network and start Talking again."));
            CancelTransport(); return false;
        }
        public bool TryRead(out TranscriptEvent? value) => notices.Reader.TryRead(out value);
        public void CompleteAudio() { Interlocked.Exchange(ref completing, 1); audio.Writer.TryComplete(); }
        public void Cancel()
        {
            if (State is not (TranscriptionState.Completed or TranscriptionState.Failed)) Volatile.Write(ref state, (int)TranscriptionState.Cancelled);
            CancelTransport();
        }
        private void CancelTransport() { cancel.Cancel(); socket.Abort(); audio.Writer.TryComplete(); }
        private void Fail(TranscriptionFailure problem)
        {
            if (State is TranscriptionState.Cancelled or TranscriptionState.Completed) return;
            Interlocked.CompareExchange(ref failure, problem, null);
            Volatile.Write(ref state, (int)TranscriptionState.Failed);
        }
        private async Task RunAsync(string apiKey, CancellationToken external)
        {
            using var lifetime = CancellationTokenSource.CreateLinkedTokenSource(cancel.Token, external);
            var token = lifetime.Token;
            Task? receiving = null;
            try
            {
                Volatile.Write(ref state, (int)TranscriptionState.Connecting);
                socket.Options.SetRequestHeader("Authorization", "Bearer " + apiKey);
                socket.Options.CollectHttpResponseDetails = true;
                socket.Options.KeepAliveInterval = TimeSpan.FromSeconds(15);
                socket.Options.KeepAliveTimeout = TimeSpan.FromSeconds(10);
                using (var connect = CancellationTokenSource.CreateLinkedTokenSource(token)) {
                    connect.CancelAfter(TimeSpan.FromSeconds(10));
                    await socket.ConnectAsync(endpoint, connect.Token).ConfigureAwait(false);
                }
                receiving = ReceiveAsync(token);
                await SendAsync(new { type = "session.update", session = new {
                    type = "transcription", audio = new { input = new {
                        format = new { type = "audio/pcm", rate = Pcm16Resampler.OutputRate },
                        transcription = new { model = SupportedModel, delay = "low" },
                        turn_detection = (object?)null
                    } } } }, token).ConfigureAwait(false);
                await ready.Task.WaitAsync(TimeSpan.FromSeconds(10), token).ConfigureAwait(false);
                Volatile.Write(ref state, (int)TranscriptionState.Listening);
                long frames = 0;
                await foreach (var pcm in audio.Reader.ReadAllAsync(token).ConfigureAwait(false)) {
                    try {
                        await SendAsync(new { type = "input_audio_buffer.append", audio = Convert.ToBase64String(pcm) }, token).ConfigureAwait(false);
                        frames += pcm.Length / 2;
                    } finally { Array.Clear(pcm); }
                }
                Volatile.Write(ref state, (int)TranscriptionState.Finalizing);
                if (frames != 0) {
                    // Realtime commits require at least 100 ms; pad a very short tap.
                    if (frames < 2400) await SendAsync(new { type = "input_audio_buffer.append", audio = Convert.ToBase64String(new byte[(2400 - frames) * 2]) }, token).ConfigureAwait(false);
                    Volatile.Write(ref committed, 1);
                    await SendAsync(new { type = "input_audio_buffer.commit" }, token).ConfigureAwait(false);
                    var text = await final.Task.WaitAsync(TimeSpan.FromSeconds(15), token).ConfigureAwait(false);
                    Publish(new(TranscriptEventKind.Final, text));
                } else Publish(new(TranscriptEventKind.Final, ""));
                Volatile.Write(ref state, (int)TranscriptionState.Completed);
            }
            catch (TranscriptionException error) { Fail(new(error.Code, error.Message)); }
            catch (OperationCanceledException) when (token.IsCancellationRequested) {
                Volatile.Write(ref state, (int)(Failure is null ? TranscriptionState.Cancelled : TranscriptionState.Failed));
            }
            catch (Exception error) when (error is TimeoutException or OperationCanceledException) {
                Fail(new("timeout", "Transcription timed out. Check your connection and start Talking again."));
            }
            catch (WebSocketException) {
                Fail(socket.HttpStatusCode switch {
                    HttpStatusCode.Unauthorized => new("authentication", "OpenAI rejected the API key. Update it in Speech settings."),
                    HttpStatusCode.Forbidden => new("access", "The OpenAI key lacks access. Check its project permissions and model access."),
                    HttpStatusCode.TooManyRequests => new("limit", "OpenAI rejected the connection limit. Check API limits and billing, then retry."),
                    _ => new("network", "The transcription connection was interrupted. Check your network and start Talking again.")
                });
            }
            catch (Exception) {
                Fail(new("protocol", "Transcription received an unexpected response. Start Talking again; check model access if this repeats."));
            }
            finally
            {
                lifetime.Cancel(); socket.Abort();
                if (receiving is not null) try { await receiving.ConfigureAwait(false); } catch (Exception) { /* Classified on the main path. */ }
                while (audio.Reader.TryRead(out var pcm)) Array.Clear(pcm);
                audio.Writer.TryComplete(); notices.Writer.TryComplete(); socket.Dispose();
                if (ready.Task.IsFaulted) _ = ready.Task.Exception;
                if (final.Task.IsFaulted) _ = final.Task.Exception;
            }
        }
        private async Task SendAsync<T>(T message, CancellationToken token)
        {
            var bytes = JsonSerializer.SerializeToUtf8Bytes(message);
            try {
                using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
                timeout.CancelAfter(TimeSpan.FromSeconds(5));
                await socket.SendAsync(bytes.AsMemory(), WebSocketMessageType.Text, true, timeout.Token).ConfigureAwait(false);
            } finally { Array.Clear(bytes); }
        }
        private void Publish(TranscriptEvent value)
        {
            if (!notices.Writer.TryWrite(value)) throw new TranscriptionException("display_backlog", "Live text could not keep up. Start a new Talk session.");
        }
        private async Task ReceiveAsync(CancellationToken token)
        {
            var bytes = new byte[MaxMessage];
            try {
                while (!token.IsCancellationRequested) {
                    var used = 0; ValueWebSocketReceiveResult part;
                    do {
                        if (used == bytes.Length) throw new TranscriptionException("response_size", "The transcription response exceeded its size limit. Start a new Talk session.");
                        part = await socket.ReceiveAsync(bytes.AsMemory(used), token).ConfigureAwait(false);
                        if (part.MessageType != WebSocketMessageType.Text) throw new WebSocketException("Connection ended.");
                        used += part.Count;
                    } while (!part.EndOfMessage);
                    using (var document = JsonDocument.Parse(bytes.AsMemory(0, used))) Handle(document.RootElement);
                    Array.Clear(bytes, 0, used);
                    if (final.Task.IsCompletedSuccessfully) return;
                }
            } catch (Exception error) {
                if (token.IsCancellationRequested) {
                    ready.TrySetCanceled(token); final.TrySetCanceled(token);
                } else {
                    var problem = error is TranscriptionException known ? new TranscriptionFailure(known.Code, known.Message) :
                        error is WebSocketException ? new("network", "The transcription connection was interrupted. Check your network and start Talking again.") :
                        new("protocol", "Transcription received an unexpected response. Start Talking again.");
                    Fail(problem); ready.TrySetException(error); final.TrySetException(error);
                    CancelTransport();
                }
                audio.Writer.TryComplete(error); throw;
            } finally { Array.Clear(bytes); }
        }
        private void Handle(JsonElement message)
        {
            var type = message.GetProperty("type").GetString();
            if (type == "session.updated") {
                var session = message.GetProperty("session");
                var input = session.GetProperty("audio").GetProperty("input");
                if (session.GetProperty("type").GetString() != "transcription" ||
                    input.GetProperty("transcription").GetProperty("model").GetString() != SupportedModel ||
                    input.GetProperty("format").GetProperty("type").GetString() != "audio/pcm" ||
                    input.GetProperty("format").GetProperty("rate").GetInt32() != Pcm16Resampler.OutputRate ||
                    (input.TryGetProperty("turn_detection", out var turn) && turn.ValueKind != JsonValueKind.Null))
                    throw new TranscriptionException("configuration", "OpenAI did not accept the transcription configuration. Check model access and retry.");
                ready.TrySetResult(); return;
            }
            if (type == "error" || type == "conversation.item.input_audio_transcription.failed") {
                var code = message.TryGetProperty("error", out var error) && error.TryGetProperty("code", out var value) ? value.GetString() : null;
                throw code switch {
                    "invalid_api_key" => new TranscriptionException("authentication", "OpenAI rejected the API key. Update it in Speech settings."),
                    "insufficient_quota" => new TranscriptionException("quota", "OpenAI API credits or your spend limit are exhausted. Check OpenAI billing and limits."),
                    "rate_limit_exceeded" => new TranscriptionException("rate_limit", "OpenAI rate limits were reached. Wait briefly, then start Talking again."),
                    "model_not_found" or "permission_denied" => new TranscriptionException("access", "The selected OpenAI project lacks access to the transcription model. Check key permissions and model access."),
                    "input_audio_buffer_commit_empty" => new TranscriptionException("empty_audio", "No usable audio reached transcription. Check the microphone and start Talking again."),
                    _ => new TranscriptionException("provider_error", "OpenAI could not transcribe this session. Check service status and try again.")
                };
            }
            if (type is not ("conversation.item.input_audio_transcription.delta" or "conversation.item.input_audio_transcription.completed")) return;
            var id = message.GetProperty("item_id").GetString();
            if (string.IsNullOrEmpty(id) || (itemId is not null && id != itemId)) throw new TranscriptionException("item_order", "Unexpected transcription turn. Start a new Talk session.");
            itemId = id;
            var text = message.GetProperty(type.EndsWith("delta", StringComparison.Ordinal) ? "delta" : "transcript").GetString() ?? "";
            if (text.Contains('\0') || text.Length > MaxTranscript) throw new TranscriptionException("transcript_size", "This Talk session exceeded the transcript limit. Start a new Talk session.");
            if (type.EndsWith("delta", StringComparison.Ordinal)) {
                deltaCharacters = checked(deltaCharacters + text.Length);
                if (deltaCharacters > MaxTranscript) throw new TranscriptionException("transcript_size", "This Talk session exceeded the transcript limit. Start a new Talk session.");
                for (var start = 0; start < text.Length;) {
                    var length = Math.Min(1024, text.Length - start);
                    if (start + length < text.Length && char.IsHighSurrogate(text[start + length - 1])) --length;
                    Publish(new(TranscriptEventKind.Delta, text.Substring(start, length))); start += length;
                }
            } else {
                if (Volatile.Read(ref committed) == 0) throw new TranscriptionException("early_final", "Unexpected early transcription completion. Start a new Talk session.");
                final.TrySetResult(text);
            }
        }
        public async ValueTask DisposeAsync()
        {
            Cancel(); if (run is not null) await run.ConfigureAwait(false);
            else { while (audio.Reader.TryRead(out var bytes)) Array.Clear(bytes); socket.Dispose(); }
            while (notices.Reader.TryRead(out _)) { }
            cancel.Dispose();
        }
    }
}
