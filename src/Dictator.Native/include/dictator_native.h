#ifndef DICTATOR_NATIVE_H
#define DICTATOR_NATIVE_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(DICTATOR_NATIVE_EXPORTS)
#    define DICTATOR_API __declspec(dllexport)
#  else
#    define DICTATOR_API __declspec(dllimport)
#  endif
#  define DICTATOR_CALL __cdecl
#else
// Portable contract-test build only. The product ships exclusively on Windows x64.
#  define DICTATOR_API __attribute__((visibility("default")))
#  define DICTATOR_CALL
#endif
#ifdef __cplusplus
#  define DICTATOR_NOEXCEPT noexcept
extern "C" {
#else
#  define DICTATOR_NOEXCEPT
#endif

#define DICTATOR_ABI_VERSION UINT32_C(1)
typedef struct dictator_context dictator_context;
typedef uint32_t dictator_result;
#define DICTATOR_OK UINT32_C(0)
#define DICTATOR_INVALID_ARGUMENT UINT32_C(1)
#define DICTATOR_ABI_MISMATCH UINT32_C(2)
#define DICTATOR_BUFFER_TOO_SMALL UINT32_C(3)
#define DICTATOR_OUT_OF_MEMORY UINT32_C(4)
#define DICTATOR_PLATFORM_ERROR UINT32_C(5)
#define DICTATOR_WRONG_THREAD UINT32_C(6)

// Natural 8-byte alignment; exactly 24 bytes on the supported x64 ABI.
// Initialize struct_size to sizeof(dictator_snapshot) before calling poll.
typedef struct dictator_snapshot {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t sequence;
    uint32_t owner_thread_id;
    uint32_t poll_thread_id;
} dictator_snapshot;

DICTATOR_API uint32_t DICTATOR_CALL dictator_get_abi_version(void) DICTATOR_NOEXCEPT;
// Borrowed diagnostic value. Tests compare before/after; this is not an allocator API.
DICTATOR_API uint32_t DICTATOR_CALL dictator_get_live_context_count(void) DICTATOR_NOEXCEPT;
// On failure *out_context is NULL. Caller owns the returned handle.
DICTATOR_API dictator_result DICTATOR_CALL dictator_create_context(
    uint32_t requested_abi, dictator_context** out_context) DICTATOR_NOEXCEPT;
// Destroy exactly once. NULL is permitted. Never use a handle after destruction.
DICTATOR_API void DICTATOR_CALL dictator_destroy_context(dictator_context* context) DICTATOR_NOEXCEPT;
// Polling runs synchronously on the calling non-real-time consumer thread.
// Calls for a given context must be serialized; separate contexts are independent.
// These context/probe operations start no callbacks, workers, UI, audio or I/O.
// Resident surface operations below explicitly create native UI on the caller thread.
DICTATOR_API dictator_result DICTATOR_CALL dictator_poll(
    dictator_context* context, dictator_snapshot* snapshot) DICTATOR_NOEXCEPT;
// UTF-16 code units, including surrogate pairs and embedded NULs, are copied verbatim.
// Both buffers are borrowed for this call only; they must not overlap.
// input_count excludes the terminator. required_count includes a trailing NUL.
// Query size with output=NULL/capacity=0. Too-small output is left untouched.
DICTATOR_API dictator_result DICTATOR_CALL dictator_copy_text(
    dictator_context* context, const uint16_t* input, uint32_t input_count,
    uint16_t* output, uint32_t output_capacity, uint32_t* required_count) DICTATOR_NOEXCEPT;

#if defined(_WIN32)
// Additive ABI 1 resident surface. All calls, including destruction, must run on
// the creating UI thread. Windows messages run on that thread's existing pump.
// No managed callbacks. poll_events transfers a bitset: Widget=1, Settings=2,
// Restart=4, Quit=8, Hotkey failure=16, Cancel preview=32. Native owns HWNDs, tray icon, menu and drawing resources.
typedef struct dictator_host dictator_host;
typedef struct dictator_audio dictator_audio;
typedef struct dictator_audio_device {
    uint16_t id[512];
    uint16_t name[256];
    uint32_t is_default;
} dictator_audio_device;
// Exactly 64 bytes. State: idle=0, starting=1, capturing=2, error=3, draining=4 (stopped capture; consumer drains final chunks).
// Error: none=0, no device=1, permissions=2, device changed=3,
// unsupported format=4, platform failure=5. Metadata only; no PCM in diagnostics.
typedef struct dictator_audio_snapshot {
    uint64_t session, device_revision, dropped_frames, captured_frames;
    uint32_t state, error;
    int32_t status;
    uint32_t sample_rate, channels;
    float peak, rms;
    uint32_t buffered_frames;
} dictator_audio_snapshot;
// UI-thread configuration. Empty ID follows Windows' default capture endpoint.
// Selecting a device cancels any current session. No capture until an eligible gesture.
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_configure_audio(dictator_host* host, const uint16_t* device_id) DICTATOR_NOEXCEPT;
// Borrowed audio service valid until host destruction. Caller must stop/join all
// background consumers/catalog tasks before destroying the host.
DICTATOR_API dictator_audio* DICTATOR_CALL dictator_host_audio_handle(dictator_host* host) DICTATOR_NOEXCEPT;
// Non-real-time calls, any thread. Catalog enumeration never opens a microphone.
DICTATOR_API dictator_result DICTATOR_CALL dictator_audio_devices(dictator_audio* audio, dictator_audio_device* devices,
    uint32_t capacity, uint32_t* count) DICTATOR_NOEXCEPT;
DICTATOR_API dictator_result DICTATOR_CALL dictator_audio_status(dictator_audio* audio, dictator_audio_snapshot* snapshot) DICTATOR_NOEXCEPT;
// Any non-real-time thread: stop/purge capture on provider failure without
// waiting for UI dispatch. Borrowed service ownership rules still apply.
DICTATOR_API dictator_result DICTATOR_CALL dictator_audio_cancel(dictator_audio* audio) DICTATOR_NOEXCEPT;
// Exactly one serialized non-real-time consumer. Normalized mono float samples
// at the source sample rate. Max 8192 frames/call. Native and managed copies are
// transient; consumed/stale native slots are zeroed. Drop-new overflow is counted.
DICTATOR_API dictator_result DICTATOR_CALL dictator_audio_read(dictator_audio* audio, float* samples, uint32_t capacity,
    uint32_t* count, uint64_t* session) DICTATOR_NOEXCEPT;
typedef struct dictator_target {
    uint64_t token;
    uintptr_t foreground;
    uintptr_t focus;
    uint64_t checked_at;
    uint32_t process_id;
    uint32_t eligible;
    uint32_t reason; // Metadata-only diagnostic stage; 0 means ready.
    int32_t status; // HRESULT, never target text.
} dictator_target;
typedef struct dictator_input {
    uint64_t timestamp;
    uint64_t target;
    uint32_t source; // Hotkey=1, microphone=2
    uint32_t down; // Release=0, press=1, source cancellation=2.
} dictator_input;
// Additive Phase 2 metadata/input contract; UI thread only, caller-owned outputs.
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_bind_hotkey(
    dictator_host* host, uint32_t modifiers, uint32_t key, uint32_t layout_backslash, const uint16_t* display) DICTATOR_NOEXCEPT;
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_target(
    dictator_host* host, dictator_target* output) DICTATOR_NOEXCEPT;
DICTATOR_API uint32_t DICTATOR_CALL dictator_host_input(
    dictator_host* host, dictator_input* output) DICTATOR_NOEXCEPT;
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_preview(
    dictator_host* host, uint64_t target) DICTATOR_NOEXCEPT;
// UI-thread raw text presentation bridge, for the future transcription provider.
// Snapshot the current presentation session at start; 0 means inactive/wrong thread.
// An old session is rejected even if Talking restarts in the same target field.
DICTATOR_API uint64_t DICTATOR_CALL dictator_host_live_text_session(dictator_host* host) DICTATOR_NOEXCEPT;
// Append UTF-16 deltas only to the currently Talking target/session. The input is borrowed
// for this call and copied. Max 2048 units per delta, 4096 queued units/128 deltas.
// Invalid/stale/inactive targets return INVALID_ARGUMENT. A full queue returns
// BUFFER_TOO_SMALL without accepting the delta. Stop/focus loss/close clears it.
// This function captures, formats, inserts and persists nothing.
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_append_live_text(
    dictator_host* host, uint64_t target, uint64_t session, const uint16_t* input, uint32_t count) DICTATOR_NOEXCEPT;
// Graceful UI-thread Talk stop: close the capture device, preserve only its
// bounded final chunks for the current consumer. Eligibility/error/close/abort
// continue to invalidate and purge immediately. Next start invalidates old data.
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_finish_preview(dictator_host* host) DICTATOR_NOEXCEPT;
// UI-thread presentation-only clear for bounded ticker backpressure. Session must
// still be active; this does not change its epoch or touch audio/transcription.
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_clear_live_text(dictator_host* host, uint64_t target, uint64_t session) DICTATOR_NOEXCEPT;
// UI-thread sanitized user error notice. No transcript or credential content.
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_notify_error(dictator_host* host, const uint16_t* message) DICTATOR_NOEXCEPT;
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_create(
    uint32_t requested_abi, dictator_host** out_host) DICTATOR_NOEXCEPT;
DICTATOR_API void DICTATOR_CALL dictator_host_destroy(dictator_host* host) DICTATOR_NOEXCEPT;
DICTATOR_API uint32_t DICTATOR_CALL dictator_host_poll_events(dictator_host* host) DICTATOR_NOEXCEPT;
DICTATOR_API dictator_result DICTATOR_CALL dictator_host_set_widget(
    dictator_host* host, uint32_t visible, double zoom, uint32_t theme) DICTATOR_NOEXCEPT;
// Borrowed HWND; never destroy or retain after host destruction. Diagnostic only.
DICTATOR_API uintptr_t DICTATOR_CALL dictator_host_widget_handle(dictator_host* host) DICTATOR_NOEXCEPT;
DICTATOR_API uint32_t DICTATOR_CALL dictator_host_tray_ready(dictator_host* host) DICTATOR_NOEXCEPT;
#endif

#ifdef __cplusplus
}
#endif
#endif
