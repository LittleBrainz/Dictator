#pragma once
#include <windows.h>
#include <array>
#include "ticker_text.h"

// Two separate capsules, with an excluded desktop-visible gap between them.
namespace widget_design {
HRGN window_region(int client_width, int client_height, bool caption_visible) noexcept;
int hit_test(int client_width, int client_height, int x, int y) noexcept;
float measure_text(const wchar_t* text, int count) noexcept;
void paint(HWND window, HDC destination, int client_width, int client_height,
    bool talking, bool eligible, int hover, int pressed, const std::array<float, 25>& levels,
    const ticker_text& ticker, const wchar_t* hint, float caption_alpha, float hint_x,
    float hint_width, float hint_period) noexcept;
}
