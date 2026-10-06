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
// Restart=4, Quit=8. Native owns HWNDs, tray icon, menu and drawing resources.
typedef struct dictator_host dictator_host;
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
