#include "dictator_native.h"
#include <cstddef>
#include <iostream>
#include <thread>

#define CHECK(expression) do { if (!(expression)) { \
  std::cerr << "Failed at line " << __LINE__ << ": " #expression << '\n'; return 1; \
} } while (false)

int main() {
    static_assert(sizeof(dictator_snapshot) == 24);
    static_assert(offsetof(dictator_snapshot, sequence) == 8);
    static_assert(offsetof(dictator_snapshot, poll_thread_id) == 20);
    CHECK(dictator_get_abi_version() == 1);
    CHECK(dictator_create_context(1, nullptr) == DICTATOR_INVALID_ARGUMENT);
    dictator_context* context = nullptr;
    CHECK(dictator_create_context(2, &context) == DICTATOR_ABI_MISMATCH);
    CHECK(context == nullptr);
    CHECK(dictator_create_context(1, &context) == DICTATOR_OK);
    CHECK(dictator_get_live_context_count() == 1);
    dictator_snapshot snapshot{};
    CHECK(dictator_poll(context, &snapshot) == DICTATOR_ABI_MISMATCH);
    snapshot.struct_size = sizeof(snapshot);
    CHECK(dictator_poll(nullptr, &snapshot) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_poll(context, nullptr) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_poll(context, &snapshot) == DICTATOR_OK);
    CHECK(snapshot.abi_version == 1 && snapshot.sequence == 1);
    CHECK(snapshot.owner_thread_id == snapshot.poll_thread_id);
    dictator_result worker_result = DICTATOR_INVALID_ARGUMENT;
    std::thread worker([&] { worker_result = dictator_poll(context, &snapshot); });
    worker.join();
    CHECK(worker_result == DICTATOR_OK && snapshot.sequence == 2);
    CHECK(snapshot.owner_thread_id != snapshot.poll_thread_id);
    const uint16_t input[] = {0x0041, 0, 0x4e2d, 0xd83d, 0xde03};
    uint32_t required = 0;
    CHECK(dictator_copy_text(context, input, 5, nullptr, 0, &required) == DICTATOR_BUFFER_TOO_SMALL);
    CHECK(required == 6);
    uint16_t output[6] = {42, 42, 42, 42, 42, 42};
    CHECK(dictator_copy_text(context, input, 5, output, 5, &required) == DICTATOR_BUFFER_TOO_SMALL);
    CHECK(output[0] == 42 && output[5] == 42);
    CHECK(dictator_copy_text(context, input, 5, output, 6, &required) == DICTATOR_OK);
    for (size_t i = 0; i < 5; ++i) CHECK(output[i] == input[i]);
    CHECK(output[5] == 0);
    CHECK(dictator_copy_text(context, nullptr, 1, output, 6, &required) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_copy_text(context, input, UINT32_MAX, output, 6, &required) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_copy_text(context, input, 5, output, 6, nullptr) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_copy_text(nullptr, input, 5, output, 6, &required) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_copy_text(context, nullptr, 0, output, 1, &required) == DICTATOR_OK);
    CHECK(required == 1 && output[0] == 0);
    dictator_destroy_context(context);
    dictator_destroy_context(nullptr);
    for (int i = 0; i < 10000; ++i) {
        CHECK(dictator_create_context(1, &context) == DICTATOR_OK);
        dictator_destroy_context(context);
    }
    CHECK(dictator_get_live_context_count() == 0);
    std::cout << "Native ABI contract passed (including 10,000 balanced allocations).\n";
}
