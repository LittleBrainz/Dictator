#pragma once
#include <windows.h>

// Drawing and hit testing share the same 270 x 56 DIP design coordinates.
namespace widget_design {
constexpr int width = 270, height = 56;
int hit_test(int client_width, int client_height, int x, int y) noexcept;
void paint(HDC destination, int client_width, int client_height,
    bool talking, bool eligible, int hover, int pressed, ULONGLONG timestamp) noexcept;
}
