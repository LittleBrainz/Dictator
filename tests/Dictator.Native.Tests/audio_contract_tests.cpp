#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <vector>
#include <iostream>
#include "dictator_native.h"
#define CHECK(expression) do { if (!(expression)) { std::cerr << "Failed: " #expression << '\n'; return 1; } } while (false)
int main() {
    static_assert(sizeof(dictator_audio_snapshot) == 64 && sizeof(dictator_audio_device) == 1540);
    CHECK(dictator_host_configure_audio(nullptr, nullptr) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_host_finish_preview(nullptr) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_host_notify_error(nullptr, nullptr) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_host_clear_live_text(nullptr, 1, 1) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_audio_status(nullptr, nullptr) == DICTATOR_INVALID_ARGUMENT);
    CHECK(dictator_audio_read(nullptr, nullptr, 0, nullptr, nullptr) == DICTATOR_INVALID_ARGUMENT);
    for (int cycle = 0; cycle < 6; ++cycle) {
        dictator_host* host{}; CHECK(dictator_host_create(1, &host) == DICTATOR_OK);
        const uint16_t empty[] = {0};
        CHECK(dictator_host_configure_audio(host, empty) == DICTATOR_OK);
        auto* audio = dictator_host_audio_handle(host); CHECK(audio);
        uint32_t count{}; std::vector<dictator_audio_device> catalog(128);
        const auto result = dictator_audio_devices(audio, catalog.data(), 128, &count);
        CHECK(result == DICTATOR_OK || result == DICTATOR_PLATFORM_ERROR);
        CHECK(count <= 128);
        for (uint32_t i = 0; i < count; ++i) CHECK(catalog[i].id[0] && catalog[i].name[0]);
        dictator_audio_snapshot snapshot{}; CHECK(dictator_audio_status(audio, &snapshot) == DICTATOR_OK);
        CHECK(snapshot.state != 2 && snapshot.captured_frames == 0); // Enumeration never captures.
        uint64_t session{}; float samples[32]{};
        CHECK(dictator_audio_read(audio, samples, 32, &count, &session) == DICTATOR_OK && count == 0);
        dictator_result thread_result{};
        std::thread other([&] { thread_result = dictator_host_configure_audio(host, empty); }); other.join();
        CHECK(thread_result == DICTATOR_WRONG_THREAD);
        dictator_host_destroy(host);
    }
    std::cout << "Audio ABI layouts, metadata enumeration without capture, bounded consumer, thread contract and repeated worker/device-notification ownership passed.\n";
}
