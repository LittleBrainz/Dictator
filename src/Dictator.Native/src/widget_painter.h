#pragma once
#include <windows.h>
#include "ticker_text.h"

// Two separate capsules, with an excluded desktop-visible gap between them.
namespace widget_design {
constexpr int width = 270, height = 63, text_height = 24, controls_top = 26;
constexpr float text_font_size = 9.8f; // 70% of the previous 14 DIP caption.
HRGN window_region(int client_width, int client_height) noexcept;
int hit_test(int client_width, int client_height, int x, int y) noexcept;
float measure_text(const wchar_t* text, int count) noexcept;
void paint(HDC destination, int client_width, int client_height,
    bool talking, bool eligible, int hover, int pressed, ULONGLONG timestamp,
    const ticker_text& ticker, const wchar_t* hint, float hint_alpha, float hint_x,
    float hint_width, float hint_period) noexcept;
}
