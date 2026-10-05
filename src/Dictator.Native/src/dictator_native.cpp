#include "dictator_native.h"
#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <functional>
#  include <thread>
#endif

static_assert(sizeof(dictator_snapshot) == 24);
static_assert(alignof(dictator_snapshot) == 8);
static_assert(sizeof(uint16_t) == 2);

namespace {
std::atomic<uint32_t> live_contexts{0};
uint32_t current_thread_id() noexcept {
#if defined(_WIN32)
    return GetCurrentThreadId();
#else
    return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}
}

struct dictator_context {
    uint64_t sequence{0};
    uint32_t owner_thread_id{current_thread_id()};
};

uint32_t DICTATOR_CALL dictator_get_abi_version() noexcept { return DICTATOR_ABI_VERSION; }
uint32_t DICTATOR_CALL dictator_get_live_context_count() noexcept { return live_contexts.load(); }

dictator_result DICTATOR_CALL dictator_create_context(
    uint32_t requested_abi, dictator_context** out_context) noexcept {
    if (!out_context) return DICTATOR_INVALID_ARGUMENT;
    *out_context = nullptr;
    if (requested_abi != DICTATOR_ABI_VERSION) return DICTATOR_ABI_MISMATCH;
    auto* context = new (std::nothrow) dictator_context;
    if (!context) return DICTATOR_OUT_OF_MEMORY;
    live_contexts.fetch_add(1);
    *out_context = context;
    return DICTATOR_OK;
}

void DICTATOR_CALL dictator_destroy_context(dictator_context* context) noexcept {
    if (context) {
        delete context;
        live_contexts.fetch_sub(1);
    }
}

dictator_result DICTATOR_CALL dictator_poll(
    dictator_context* context, dictator_snapshot* snapshot) noexcept {
    if (!context || !snapshot) return DICTATOR_INVALID_ARGUMENT;
    if (snapshot->struct_size != sizeof(dictator_snapshot)) return DICTATOR_ABI_MISMATCH;
    snapshot->abi_version = DICTATOR_ABI_VERSION;
    snapshot->sequence = ++context->sequence;
    snapshot->owner_thread_id = context->owner_thread_id;
    snapshot->poll_thread_id = current_thread_id();
    return DICTATOR_OK;
}

dictator_result DICTATOR_CALL dictator_copy_text(
    dictator_context* context, const uint16_t* input, uint32_t input_count,
    uint16_t* output, uint32_t output_capacity, uint32_t* required_count) noexcept {
    if (!context || !required_count || (!input && input_count != 0) ||
        input_count == std::numeric_limits<uint32_t>::max()) return DICTATOR_INVALID_ARGUMENT;
    *required_count = input_count + 1;
    if (!output || output_capacity < *required_count) return DICTATOR_BUFFER_TOO_SMALL;
    if (input_count) std::memcpy(output, input, static_cast<size_t>(input_count) * sizeof(uint16_t));
    output[input_count] = 0;
    return DICTATOR_OK;
}
