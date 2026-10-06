#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "dictator_native.h"

// UI Automation providers run off the UI thread. Fixed-capacity queues fail
// closed on overflow. Only focus/editability/identity properties are requested.
class target_probe {
public:
    explicit target_probe(HWND notification);
    ~target_probe();
    void input(uint32_t source, uint32_t down, uint64_t timestamp) noexcept;
    dictator_target snapshot() noexcept;
    bool pop(dictator_input& output) noexcept;
    void cancel_inputs() noexcept;
private:
    void run() noexcept;
    HWND notification_{};
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_{};
    uint64_t epoch_{};
    std::deque<dictator_input> requests_, inputs_;
    dictator_target target_{};
    std::thread worker_;
};
