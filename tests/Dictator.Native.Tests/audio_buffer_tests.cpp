#include <iostream>
#include <thread>
#include <atomic>
#include <limits>
#include "audio_buffer.h"
#define CHECK(expression) do { if (!(expression)) { std::cerr << "Failed: " #expression << '\n'; return 1; } } while (false)
int main() {
    using namespace audio_data;
    const unsigned char silence[] = {128}, low16[] = {0,128}, high16[] = {255,127};
    const unsigned char low24[] = {0,0,128}, low32[] = {0,0,0,128};
    CHECK(sample(silence, 8, false) == 0 && sample(low16, 16, false) == -1);
    CHECK(sample(high16, 16, false) > .999f && sample(low24, 24, false) == -1 && sample(low32, 32, false) == -1);
    float invalid = std::numeric_limits<float>::quiet_NaN(), loud = 3, stereo[] = {.5f, -.25f};
    CHECK(sample(reinterpret_cast<const unsigned char*>(&invalid), 32, true) == 0);
    CHECK(sample(reinterpret_cast<const unsigned char*>(&loud), 32, true) == 1);
    CHECK(mono(reinterpret_cast<const unsigned char*>(stereo), 2, 32, true) == .125f);
    ring_buffer<8> bounded; float input[] = {1,2,3,4,5,6,7,8}, output[8]{};
    CHECK(bounded.write(input, 8) == 8 && bounded.write(input, 1) == 0 && bounded.buffered() == 8);
    CHECK(bounded.read(output, 3, 0) == 3 && output[0] == 1 && output[2] == 3);
    const auto next_session = bounded.head();
    CHECK(bounded.write(input, 2) == 2);
    CHECK(bounded.read(output, 8, next_session) == 2 && output[0] == 1 && output[1] == 2);
    CHECK(bounded.buffered() == 0);
    ring_buffer<128> concurrent; std::atomic<bool> bad{};
    constexpr uint32_t total = 1000000;
    std::thread producer([&] {
        uint32_t next{}; float block[32]{};
        while (next < total) {
            const auto count = std::min(uint32_t{32}, total - next);
            for (uint32_t i = 0; i < count; ++i) block[i] = static_cast<float>(next + i);
            next += static_cast<uint32_t>(concurrent.write(block, count));
            std::this_thread::yield();
        }
    });
    uint32_t consumed{};
    while (consumed < total) {
        const auto count = concurrent.read(output, 8, 0);
        for (std::size_t i = 0; i < count; ++i) if (output[i] != static_cast<float>(consumed++)) bad.store(true);
        std::this_thread::yield();
    }
    producer.join(); CHECK(!bad.load() && concurrent.buffered() == 0);
    std::cout << "PCM/float normalization, stereo downmix, invalid samples, bounded drop-new policy, stale-session discard and one-million-frame concurrent ordering passed.\n";
}
