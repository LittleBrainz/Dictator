#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include "audio_buffer.h"
#include "dictator_native.h"

struct dictator_audio {
    static constexpr std::size_t capacity = 131072;
    dictator_audio();
    ~dictator_audio();
    void start(const wchar_t* selected, uint64_t target, uintptr_t foreground, uintptr_t focus,
        const std::atomic<uint64_t>* eligibility, const std::atomic<uint64_t>* checked_at) noexcept;
    void stop() noexcept;
    void changed(const wchar_t* id, bool unavailable, bool default_changed) noexcept;
    void snapshot(dictator_audio_snapshot& output) const noexcept;
    uint32_t read(float* output, uint32_t capacity, uint64_t& session) noexcept;
    std::atomic<uint64_t> revision{1};
private:
    void run() noexcept;
    HANDLE shutdown_{}, control_{}, packets_{};
    std::thread worker_;
    mutable std::mutex commands_, selection_, consumer_;
    wchar_t requested_[512]{}, active_[512]{};
    bool follow_default_{};
    uint64_t request_number_{};
    bool requested_on_{};
    uint64_t requested_target_{};
    uintptr_t requested_foreground_{}, requested_focus_{};
    const std::atomic<uint64_t>* eligibility_{};
    const std::atomic<uint64_t>* checked_at_{};
    std::atomic<uint32_t> state_{}, error_{};
    std::atomic<int32_t> status_{};
    std::atomic<uint32_t> sample_rate_{}, channels_{};
    std::atomic<float> peak_{}, rms_{};
    std::atomic<uint64_t> session_{}, valid_from_{}, dropped_{}, frames_{};
    std::atomic<bool> removed_{};
    std::atomic<bool> fatal_{};
    audio_data::ring_buffer<capacity> buffer_;
};
