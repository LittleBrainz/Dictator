#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace audio_data {
// One native producer, one serialized non-real-time consumer. Drop-new overflow;
// consumed/stale samples are zeroed before publishing free slots to the producer.
#ifdef _MSC_VER
#pragma warning(push)
// Deliberate cache-line separation of the two independently written cursors.
#pragma warning(disable: 4324)
#endif
template<std::size_t Capacity> class ring_buffer {
    std::array<float, Capacity> samples_{};
    alignas(64) std::atomic<uint64_t> head_{};
    alignas(64) std::atomic<uint64_t> tail_{};
public:
    std::size_t write(const float* data, std::size_t count) noexcept {
        auto head = head_.load(std::memory_order_relaxed);
        const auto available = Capacity - static_cast<std::size_t>(head - tail_.load(std::memory_order_acquire));
        const auto accepted = std::min(count, available);
        for (std::size_t i = 0; i < accepted; ++i) samples_[(head + i) % Capacity] = data[i];
        head_.store(head + accepted, std::memory_order_release); return accepted;
    }
    std::size_t read(float* output, std::size_t count, uint64_t valid_from) noexcept {
        auto tail = tail_.load(std::memory_order_relaxed);
        const auto head = head_.load(std::memory_order_acquire);
        while (tail < std::min(head, valid_from)) { samples_[tail % Capacity] = 0; ++tail; }
        const auto available = std::min(count, static_cast<std::size_t>(head - tail));
        for (std::size_t i = 0; i < available; ++i) {
            output[i] = samples_[(tail + i) % Capacity]; samples_[(tail + i) % Capacity] = 0;
        }
        tail_.store(tail + available, std::memory_order_release); return available;
    }
    uint64_t head() const noexcept { return head_.load(std::memory_order_acquire); }
    uint32_t buffered() const noexcept {
        const auto tail = tail_.load(std::memory_order_acquire);
        // Diagnostic readers do not own either cursor; clamp an interleaved
        // snapshot while the producer and consumer are both advancing.
        return static_cast<uint32_t>(std::min(uint64_t{Capacity}, head_.load(std::memory_order_acquire) - tail));
    }
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
inline float sample(const unsigned char* data, unsigned bits, bool floating) noexcept {
    if (floating) {
        float value{}; std::memcpy(&value, data, sizeof(value));
        return std::isfinite(value) ? std::clamp(value, -1.f, 1.f) : 0;
    }
    if (bits == 8) return (static_cast<float>(*data) - 128) / 128;
    int32_t value{};
    if (bits == 16) { int16_t value16{}; std::memcpy(&value16, data, 2); return static_cast<float>(value16) / 32768; }
    if (bits == 24) {
        uint32_t raw = data[0] | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16);
        if (raw & 0x800000) raw |= 0xFF000000;
        std::memcpy(&value, &raw, 4); return static_cast<float>(value) / 8388608;
    }
    std::memcpy(&value, data, 4); return static_cast<float>(static_cast<double>(value) / 2147483648.0);
}
inline float mono(const unsigned char* frame, unsigned channels, unsigned bits, bool floating) noexcept {
    float value{};
    for (unsigned channel = 0; channel < channels; ++channel) value += sample(frame + channel * (bits / 8), bits, floating);
    return value / static_cast<float>(channels);
}
}
