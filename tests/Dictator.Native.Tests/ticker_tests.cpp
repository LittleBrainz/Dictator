#include <cmath>
#include <iostream>
#include "ticker_text.h"
#define CHECK(expression) do { if (!(expression)) { std::cerr << "Failed: " #expression << '\n'; return 1; } } while (false)
int main() {
    CHECK(widget_design::hover_opacity(900) == 0 && widget_design::hover_opacity(1000) == 0);
    CHECK(std::abs(widget_design::hover_opacity(1120) - .5f) < .001f);
    CHECK(widget_design::hover_opacity(1240) == 1 && widget_design::hover_opacity(5000) == 1);
    // A long hover message repeats immediately, with just its measured separator.
    // Its phase is continuous across the wrap: moving to the next copy leaves
    // every visible glyph in the same position instead of restarting the message.
    CHECK(widget_design::hint_scroll_x(0, 100000) == 9);
    CHECK(widget_design::hint_scroll_x(300, 1200) == 9);
    CHECK(std::abs(widget_design::hint_scroll_x(300, 2200) + 15) < .001f);
    const auto before_wrap = widget_design::hint_scroll_x(300, 13690);
    const auto after_wrap = widget_design::hint_scroll_x(300, 13710);
    CHECK(std::abs((before_wrap + 300) - after_wrap - .48f) < .001f);
    CHECK(std::abs(widget_design::hint_scroll_x(300, 26200) - 9) < .001f);
    widget_design::ticker_text ticker;
    CHECK(ticker.append(L"First ", 30, 1000));
    ticker.advance(1250);
    CHECK(std::abs(ticker.x() - 244) < .001f);
    CHECK(ticker.append(L"second", 50, 1250));
    CHECK(std::abs(ticker.x() - 244) < .001f); // New deltas never rewind existing text.
    CHECK(ticker.characters() == 12 && ticker.segments().size() == 2);
    ticker.advance(2250); CHECK(std::abs(ticker.x() - 180) < .001f);
    ticker.advance(9000); CHECK(ticker.segments().empty() && ticker.characters() == 0);
    const std::wstring large(2048, L'x');
    CHECK(ticker.append(large, 10000, 9000));
    CHECK(ticker.append(large, 10000, 9000));
    CHECK(!ticker.append(L"overflow", 100, 9000) && ticker.characters() == 4096);
    ticker.clear(10000); CHECK(ticker.characters() == 0 && ticker.segments().empty());
    for (int i = 0; i < 128; ++i) CHECK(ticker.append(L"字", 10, 10000));
    CHECK(!ticker.append(L"overflow", 100, 10000)); // Segment overhead is bounded too.
    ticker.advance(40000); CHECK(ticker.characters() == 0);
    CHECK(ticker.append(L"Bonjour, 你好 — microphone 🎤", 120, 40000));
    CHECK(ticker.segments().front().text == L"Bonjour, 你好 — microphone 🎤");
    ticker.advance(40016); CHECK(std::abs(ticker.x() - 258.976f) < .001f);
    ticker.advance(40032); CHECK(std::abs(ticker.x() - 257.952f) < .001f);
    ticker.clear(40032); CHECK(ticker.x() == 260 && ticker.characters() == 0);
    std::cout << "Continuous right-to-left motion, append continuity, Unicode, pruning, bounded storage and reset passed.\n";
}
