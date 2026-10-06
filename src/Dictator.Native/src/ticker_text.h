#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <deque>
#include <string>
#include <utility>

// UI-thread presentation state only. No provider, formatting, persistence or logging.
namespace widget_design {
inline constexpr wchar_t hint_separator[] = L"   ·   ";
inline float hint_scroll_x(float period, uint64_t elapsed) noexcept {
    // One initial reading pause; repeated copies then move without a wrap pause.
    if (period <= 0 || elapsed <= 1200) return 9;
    return 9 - std::fmod(static_cast<float>(elapsed - 1200) * .024f, period);
}
inline float hover_opacity(uint64_t elapsed) noexcept {
    return elapsed > 1000 ? std::min(1.f, static_cast<float>(elapsed - 1000) / 240) : 0;
}
class ticker_text {
public:
    struct segment { std::wstring text; float width; };
    static constexpr std::size_t max_characters = 4096, max_segments = 128;
    static constexpr float entry_x = 260, exit_x = 9, speed = 64; // DIP / second

    void clear(uint64_t now) noexcept {
        segments_.clear(); characters_ = 0; x_ = entry_x; updated_at_ = now;
    }
    void advance(uint64_t now) noexcept {
        if (!segments_.empty() && now >= updated_at_)
            x_ -= speed * static_cast<float>(now - updated_at_) / 1000;
        updated_at_ = now;
        while (!segments_.empty() && x_ + segments_.front().width < exit_x) {
            x_ += segments_.front().width;
            characters_ -= segments_.front().text.size(); segments_.pop_front();
        }
        if (segments_.empty()) x_ = entry_x;
    }
    bool append(std::wstring text, float measured_width, uint64_t now) {
        advance(now);
        if (text.size() > max_characters - characters_ || segments_.size() == max_segments) return false;
        if (segments_.empty()) x_ = entry_x;
        const auto count = text.size();
        segments_.push_back({std::move(text), measured_width}); characters_ += count;
        return true;
    }
    const std::deque<segment>& segments() const noexcept { return segments_; }
    float x() const noexcept { return x_; }
    std::size_t characters() const noexcept { return characters_; }
private:
    std::deque<segment> segments_;
    float x_{entry_x};
    uint64_t updated_at_{};
    std::size_t characters_{};
};
}
