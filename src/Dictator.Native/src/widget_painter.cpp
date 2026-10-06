#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstddef>
#include "widget_painter.h"

namespace widget_design {
namespace {
using namespace Gdiplus;
void rounded(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL radius) noexcept {
    const REAL d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}
void stroke(Graphics& g, GraphicsPath& path, Color color, REAL pen_width) noexcept {
    Pen pen(color, pen_width); pen.SetLineJoin(LineJoinRound); g.DrawPath(&pen, &path);
}
void line(Graphics& g, Color color, REAL pen_width, REAL x1, REAL y1, REAL x2, REAL y2) noexcept {
    Pen pen(color, pen_width); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen, x1, y1, x2, y2);
}
void button(Graphics& g, REAL x, REAL y, REAL radius, bool lit, bool pressed, Color accent) noexcept {
    GraphicsPath circle; circle.AddEllipse(x - radius, y - radius, 2 * radius, 2 * radius);
    if (lit) {
        for (int i = 5; i > 0; --i)
            stroke(g, circle, Color(12, accent.GetR(), accent.GetG(), accent.GetB()), static_cast<REAL>(i * 2));
    }
    const Color tint = accent.GetR() > accent.GetG() ? Color(255, 72, 14, 35) : Color(255, 8, 56, 53);
    LinearGradientBrush glass(PointF(x, y - radius), PointF(x, y + radius),
        lit ? tint : Color(255, 35, 62, 94), Color(255, 3, 14, 32));
    if (pressed) glass.SetLinearColors(Color(255, 3, 14, 32), Color(255, 24, 48, 68));
    g.FillPath(&glass, &circle);
    stroke(g, circle, lit ? accent : Color(180, 110, 154, 200), .7f);
    GraphicsPath shine; shine.AddArc(x - radius + 1, y - radius + 1, 2 * radius - 2, 2 * radius - 2, 208, 96);
    stroke(g, shine, lit ? Color(255, 220, 255, 242) : Color(220, 207, 230, 255), .65f);
    line(g, Color(30, 161, 206, 255), .6f, x - radius * .45f, y + radius * .78f, x + radius * .45f, y + radius * .78f);
}
void gear(Graphics& g, REAL x, REAL y) noexcept {
    // Eight teeth with a circular cut-out. No font fallback or emoji rendering.
    GraphicsPath shape(FillModeAlternate);
    PointF teeth[64]{};
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < 64; ++i) {
        const int edge = i % 8;
        const REAL radius = edge >= 2 && edge <= 5 ? 4.7f : 3.65f;
        const double angle = (i + .5) * 2 * pi / 64;
        teeth[i] = PointF(x + radius * static_cast<REAL>(std::cos(angle)), y + radius * static_cast<REAL>(std::sin(angle)));
    }
    shape.AddPolygon(teeth, 64); shape.AddEllipse(x - 1.85f, y - 1.85f, 3.7f, 3.7f);
    SolidBrush white(Color(255, 242, 250, 255)); g.FillPath(&white, &shape);
}
void capsule(Graphics& g, REAL top, REAL panel_height, bool text) noexcept {
    GraphicsPath rim; rounded(rim, .5f, top + .5f, static_cast<REAL>(width - 1), panel_height - 1, (panel_height - 1) / 2);
    LinearGradientBrush chrome(PointF(0, top), PointF(0, top + panel_height), Color(255, 168, 222, 255), Color(255, 16, 104, 173));
    const Color colors[] = {Color(255, 210, 239, 255), Color(255, 33, 113, 203), Color(255, 7, 32, 65),
        Color(255, 10, 51, 107), Color(255, 20, 163, 231), Color(255, 175, 238, 255)};
    const REAL stops[] = {0, .08f, .24f, .74f, .94f, 1};
    chrome.SetInterpolationColors(colors, stops, 6); g.FillPath(&chrome, &rim);
    stroke(g, rim, Color(255, 71, 164, 229), .45f);
    GraphicsPath inside; rounded(inside, 1.8f, top + 1.8f, width - 3.6f, panel_height - 3.6f, (panel_height - 3.6f) / 2);
    LinearGradientBrush glass(PointF(0, top + 2), PointF(0, top + panel_height - 2),
        text ? Color(255, 9, 23, 42) : Color(255, 9, 36, 78), Color(255, 1, 9, 24));
    g.FillPath(&glass, &inside); stroke(g, inside, Color(125, 144, 215, 255), .4f);
    GraphicsPath highlight; highlight.AddLine(panel_height / 2, top + 1.25f, width - panel_height / 2, top + 1.25f);
    stroke(g, highlight, Color(32, 70, 188, 255), 3);
    stroke(g, highlight, Color(235, 191, 237, 255), .45f);
}
void spotlight(Graphics& g, REAL x, REAL y, REAL w, REAL h, Color center) noexcept {
    GraphicsPath area; area.AddEllipse(x, y, w, h);
    PathGradientBrush light(&area); light.SetCenterColor(center);
    const Color edge(0, center.GetR(), center.GetG(), center.GetB()); int count = 1;
    light.SetSurroundColors(&edge, &count); g.FillPath(&light, &area);
}
void draw_text(Graphics& g, const wchar_t* text, int count, REAL x, Color color) noexcept {
    Font font(L"Segoe UI", text_font_size, FontStyleRegular, UnitPixel);
    StringFormat format(StringFormat::GenericTypographic());
    format.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    SolidBrush ink(color); g.DrawString(text, count, &font, PointF(x, 3.7f), &format, &ink);
}
}
HRGN window_region(int client_width, int client_height, bool caption_visible) noexcept {
    const int text_bottom = static_cast<int>(std::lround(static_cast<double>(client_height) * text_height / height));
    const int control_top = static_cast<int>(std::lround(static_cast<double>(client_height) * controls_top / height));
    const int control_height = client_height - control_top;
    const int text_radius = static_cast<int>(std::lround(static_cast<double>(client_width) * text_height / width));
    const int control_radius = static_cast<int>(std::lround(static_cast<double>(client_width) * (height - controls_top) / width));
    auto top = caption_visible ? CreateRoundRectRgn(0, 0, client_width, text_bottom, text_radius, text_bottom) : CreateRectRgn(0, 0, 0, 0);
    auto bottom = CreateRoundRectRgn(0, control_top, client_width, client_height, control_radius, control_height);
    if (!top || !bottom || CombineRgn(top, top, bottom, RGN_OR) == ERROR) {
        if (top) DeleteObject(top); if (bottom) DeleteObject(bottom); return nullptr;
    }
    DeleteObject(bottom); return top;
}
float measure_text(const wchar_t* text, int count) noexcept {
    Bitmap pixel(1, 1, PixelFormat32bppPARGB); Graphics g(&pixel);
    Font font(L"Segoe UI", text_font_size, FontStyleRegular, UnitPixel);
    StringFormat format(StringFormat::GenericTypographic());
    format.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    RectF size;
    return g.MeasureString(text, count, &font, PointF(0, 0), &format, &size) == Ok ? size.Width : 0;
}
int hit_test(int client_width, int client_height, int x, int y) noexcept {
    if (client_width <= 0 || client_height <= 0 || x < 0 || y < 0 || x >= client_width || y >= client_height) return -1;
    const double dx = static_cast<double>(x) * width / client_width;
    const int text_bottom = static_cast<int>(std::lround(static_cast<double>(client_height) * text_height / height));
    const int control_top = static_cast<int>(std::lround(static_cast<double>(client_height) * controls_top / height));
    if (y < text_bottom) return 4; // Caption never initiates a drag.
    if (y < control_top) return -1;
    if (dx < 40) return 1;
    if (dx >= width - 29) return 3;
    if (dx >= width - 55) return 2;
    return 0;
}
void paint(HWND window, HDC destination, int client_width, int client_height,
    bool talking, bool eligible, int hover, int pressed, ULONGLONG timestamp,
    const ticker_text& ticker, const wchar_t* hint, float caption_alpha, float hint_x,
    float hint_width, float hint_period) noexcept {
    if (client_width <= 0 || client_height <= 0) return;
    Bitmap frame(client_width, client_height, PixelFormat32bppPARGB); Graphics g(&frame);
    g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit); g.Clear(Color(0, 0, 0, 0));
    g.ScaleTransform(static_cast<REAL>(client_width) / width, static_cast<REAL>(client_height) / height);
    const auto controls = g.Save();
    g.TranslateTransform(0, static_cast<REAL>(controls_top - 28)); // Keep the control artwork at its original size.
    capsule(g, 28, 37, false);
    const bool active = talking && eligible;
    const Color mic_color = active ? Color(255, 25, 255, 123) : Color(255, 255, 57, 77);
    {
        const auto clip = g.Save();
        GraphicsPath panel; rounded(panel, 1.8f, 29.8f, width - 3.6f, 33.4f, 16.7f);
        g.SetClip(&panel, CombineModeIntersect);
        spotlight(g, 0, 28, 53, 37, Color(60, mic_color.GetR(), mic_color.GetG(), mic_color.GetB()));
        spotlight(g, 43, 32, waveform_right - 40, 29, Color(55, 0, 90, 248)); g.Restore(clip);
    }
    button(g, 19.5f, 46.5f, 14.7f, true, pressed == 1, mic_color);
    GraphicsPath mic; rounded(mic, 16.8f, 38.9f, 5.4f, 10.2f, 2.7f);
    SolidBrush white(Color(255, 242, 255, 255)); g.FillPath(&white, &mic);
    GraphicsPath cradle; cradle.AddArc(14.5f, 43.f, 10.f, 10.f, 0.f, 180.f);
    stroke(g, cradle, Color(255, 242, 255, 255), 1.1f);
    line(g, Color(255, 242, 255, 255), 1.1f, 19.5f, 53, 19.5f, 55.6f);
    line(g, Color(255, 242, 255, 255), 1.1f, 16.3f, 55.6f, 22.7f, 55.6f);
    if (!active) {
        line(g, Color(255, 8, 21, 35), 3, 12.8f, 53.4f, 26.2f, 40);
        line(g, mic_color, 1.6f, 12.8f, 53.4f, 26.2f, 40);
    }
    const Color wave = eligible ? Color(255, 35, 204, 255) : Color(255, 255, 57, 77);
    if (active) {
        constexpr int bars = 25;
        for (int i = 0; i < bars; ++i) {
            const REAL x = waveform_left + static_cast<REAL>(i) * (waveform_right - waveform_left) / (bars - 1);
            const double phase = static_cast<double>(timestamp) / 210 + i * .42;
            const double envelope = std::pow(std::sin(i * 3.14159265358979323846 / (bars - 1)), 2);
            const REAL half = static_cast<REAL>(.45 + 13.7 * envelope * (.2 + .8 * std::abs(std::sin(phase))));
            line(g, Color(22, 0, 144, 255), 7, x, 46.5f - half, x, 46.5f + half);
            line(g, Color(70, 0, 163, 255), 4, x, 46.5f - half, x, 46.5f + half);
            line(g, wave, 1.3f, x, 46.5f - half, x, 46.5f + half);
            line(g, Color(170, 182, 252, 255), .5f, x, 46.5f - half, x, 46.5f + half);
        }
    } else {
        line(g, Color(25, wave.GetR(), wave.GetG(), wave.GetB()), 7, 44, 46.5f, waveform_right, 46.5f);
        line(g, Color(70, wave.GetR(), wave.GetG(), wave.GetB()), 3.4f, 44, 46.5f, waveform_right, 46.5f);
        line(g, wave, 1.1f, 44, 46.5f, waveform_right, 46.5f);
    }
    const auto right_buttons = g.Save(); g.TranslateTransform(static_cast<REAL>(width - 270), 0);
    button(g, 229, 46.5f, 10.2f, hover == 2, pressed == 2, Color(255, 50, 194, 255));
    gear(g, 229, 46.5f);
    button(g, 253, 46.5f, 11, hover == 3, pressed == 3, Color(255, 50, 194, 255));
    line(g, Color(255, 242, 250, 255), 1.7f, 249.2f, 42.7f, 256.8f, 50.3f);
    line(g, Color(255, 242, 250, 255), 1.7f, 249.2f, 50.3f, 256.8f, 42.7f);
    g.Restore(right_buttons); g.Restore(controls);
    if (caption_alpha > 0) {
        // Render the complete text capsule first, including already-present text.
        // Apply alpha once to this group, leaving the controls fully opaque.
        Bitmap caption(client_width, client_height, PixelFormat32bppPARGB);
        Graphics text(&caption); text.Clear(Color(0, 0, 0, 0));
        text.SetSmoothingMode(SmoothingModeAntiAlias); text.SetPixelOffsetMode(PixelOffsetModeHalf);
        text.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        text.ScaleTransform(static_cast<REAL>(client_width) / width, static_cast<REAL>(client_height) / height);
        capsule(text, 0, text_height, true);
        text.SetClip(RectF(text_left, 2, text_width, 20), CombineModeIntersect);
        if (active) {
            REAL x = ticker.x();
            for (const auto& part : ticker.segments()) {
                draw_text(text, part.text.data(), static_cast<int>(part.text.size()), x, Color(255, 249, 253, 255)); x += part.width;
            }
        } else if (hint && hint[0]) {
            const Color ink(255, 112, 222, 255); // Very bright ice blue, #70DEFF.
            draw_text(text, hint, -1, hint_x, ink);
            if (hint_period > 0) {
                draw_text(text, hint_repeat_separator, -1, hint_x + hint_width, ink);
                draw_text(text, hint, -1, hint_x + hint_period, ink);
            }
        }
        text.Flush(FlushIntentionSync);
        const ColorMatrix fade = {{{1,0,0,0,0}, {0,1,0,0,0}, {0,0,1,0,0},
            {0,0,0,std::clamp(caption_alpha, 0.f, 1.f),0}, {0,0,0,0,1}}};
        ImageAttributes attributes; attributes.SetColorMatrix(&fade);
        g.ResetTransform();
        g.DrawImage(&caption, Rect(0, 0, client_width, client_height), 0, 0, client_width, client_height, UnitPixel, &attributes);
    }
    g.Flush(FlushIntentionSync);
    if (destination) {
        Graphics output(destination); output.DrawImage(&frame, 0, 0, client_width, client_height);
        return;
    }
    // A per-pixel layered surface blends the capsule with the actual desktop.
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = client_width; info.bmiHeader.biHeight = -client_height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    const auto screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    void* pixels{}; const auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (memory && bitmap && pixels) {
        const auto old = SelectObject(memory, bitmap);
        BitmapData data{}; const Rect bounds(0, 0, client_width, client_height);
        if (frame.LockBits(&bounds, ImageLockModeRead, PixelFormat32bppPARGB, &data) == Ok) {
            const auto row_bytes = static_cast<std::size_t>(client_width) * 4;
            for (int y = 0; y < client_height; ++y)
                std::memcpy(static_cast<BYTE*>(pixels) + y * row_bytes,
                    static_cast<const BYTE*>(data.Scan0) + static_cast<std::ptrdiff_t>(y) * data.Stride, row_bytes);
            frame.UnlockBits(&data);
            POINT source{}; SIZE size{client_width, client_height};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(window, screen, nullptr, &size, memory, &source, 0, &blend, ULW_ALPHA);
        }
        SelectObject(memory, old);
    }
    if (bitmap) DeleteObject(bitmap); if (memory) DeleteDC(memory); if (screen) ReleaseDC(nullptr, screen);
}
}
