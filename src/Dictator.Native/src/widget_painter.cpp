#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cmath>
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
void stroke(Graphics& g, GraphicsPath& path, Color color, REAL width) noexcept {
    Pen pen(color, width); pen.SetLineJoin(LineJoinRound); g.DrawPath(&pen, &path);
}
void line(Graphics& g, Color color, REAL width, REAL x1, REAL y1, REAL x2, REAL y2) noexcept {
    Pen pen(color, width); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
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
}
int hit_test(int client_width, int client_height, int x, int y) noexcept {
    if (client_width <= 0 || client_height <= 0 || x < 0 || y < 0 || x >= client_width || y >= client_height) return -1;
    const double dx = static_cast<double>(x) * width / client_width;
    const double dy = static_cast<double>(y) * height / client_height;
    if (dy < 21) return 0; // The transcript strip is also a drag surface.
    if (dx < 43) return 1;
    if (dx >= 241) return 3;
    if (dx >= 215) return 2;
    return 0;
}
void paint(HDC destination, int client_width, int client_height,
    bool talking, bool eligible, int hover, int pressed, ULONGLONG timestamp) noexcept {
    if (client_width <= 0 || client_height <= 0) return;
    // Off-screen GDI+ rendering keeps animation flicker-free and the HWND non-activating.
    Bitmap frame(client_width, client_height, PixelFormat32bppPARGB);
    Graphics g(&frame);
    g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.Clear(Color(255, 4, 24, 53));
    g.ScaleTransform(static_cast<REAL>(client_width) / width, static_cast<REAL>(client_height) / height);

    GraphicsPath outer; rounded(outer, 1, 1, 268, 54, 12);
    LinearGradientBrush bezel(PointF(0, 0), PointF(0, 56), Color(255, 159, 215, 255), Color(255, 17, 95, 171));
    const Color bezel_colors[] = {Color(255, 183, 221, 255), Color(255, 1, 35, 88), Color(255, 40, 210, 255),
        Color(255, 11, 59, 123), Color(255, 138, 231, 255), Color(255, 213, 241, 255)};
    const REAL stops[] = {0, .09f, .23f, .70f, .94f, 1};
    bezel.SetInterpolationColors(bezel_colors, stops, 6); g.FillPath(&bezel, &outer);
    stroke(g, outer, Color(255, 73, 171, 247), .7f);
    GraphicsPath inner; rounded(inner, 3.4f, 3.4f, 263.2f, 49.2f, 9.3f);
    LinearGradientBrush panel(PointF(0, 4), PointF(0, 52), Color(255, 6, 42, 91), Color(255, 4, 24, 53));
    g.FillPath(&panel, &inner); stroke(g, inner, Color(240, 158, 225, 255), .7f);
    GraphicsPath top; top.AddLine(13.f, 2.8f, 255.f, 2.8f);
    stroke(g, top, Color(85, 36, 196, 255), 4);
    stroke(g, top, Color(255, 199, 247, 255), .65f);

    GraphicsPath transcript; rounded(transcript, 4.7f, 4.7f, 260.6f, 15.7f, 7.6f);
    LinearGradientBrush strip(PointF(0, 5), PointF(0, 20), Color(255, 1, 8, 17), Color(255, 1, 17, 28));
    g.FillPath(&strip, &transcript); stroke(g, transcript, Color(110, 111, 181, 214), .45f);
    // Actual streaming transcription will populate this strip in Phase 4.
    // Phase 2 has no audio or transcript; never show the mock-up's example as live text.
    GraphicsPath row; rounded(row, 5, 21, 260, 30.5f, 8.5f);
    LinearGradientBrush row_glass(PointF(0, 21), PointF(0, 52), Color(105, 47, 93, 150), Color(20, 2, 16, 35));
    g.FillPath(&row_glass, &row); stroke(g, row, Color(75, 141, 192, 228), .5f);

    const bool active = talking && eligible;
    const Color mic_color = active ? Color(255, 25, 255, 123) : Color(255, 255, 57, 77);
    button(g, 25, 36.5f, 13.8f, true, pressed == 1, mic_color);
    GraphicsPath mic; rounded(mic, 23, 30.2f, 4, 8.4f, 2);
    SolidBrush white(Color(255, 242, 255, 255)); g.FillPath(&white, &mic);
    GraphicsPath cradle; cradle.AddArc(21, 33.5f, 8, 8, 0, 180);
    stroke(g, cradle, Color(255, 242, 255, 255), .95f);
    line(g, Color(255, 242, 255, 255), .95f, 25, 41.5f, 25, 44);
    line(g, Color(255, 242, 255, 255), .95f, 22.5f, 44, 27.5f, 44);
    if (!active) {
        line(g, Color(255, 8, 21, 35), 2.7f, 19.5f, 42, 30.5f, 31);
        line(g, mic_color, 1.4f, 19.5f, 42, 30.5f, 31);
    }

    const Color wave = eligible ? Color(255, 35, 204, 255) : Color(255, 255, 57, 77);
    if (active) {
        constexpr int bars = 35;
        for (int i = 0; i < bars; ++i) {
            const REAL x = 49 + static_cast<REAL>(i) * 4.65f;
            const double phase = static_cast<double>(timestamp) / 210 + i * .42;
            const double envelope = std::pow(std::sin(i * 3.14159265358979323846 / (bars - 1)), 2);
            const REAL half = static_cast<REAL>(.45 + 12.1 * envelope * (.2 + .8 * std::abs(std::sin(phase))));
            line(g, Color(22, 0, 144, 255), 7, x, 36.5f - half, x, 36.5f + half);
            line(g, Color(70, 0, 163, 255), 4, x, 36.5f - half, x, 36.5f + half);
            line(g, wave, 1.2f, x, 36.5f - half, x, 36.5f + half);
            line(g, Color(170, 182, 252, 255), .45f, x, 36.5f - half, x, 36.5f + half);
        }
    } else {
        line(g, Color(25, wave.GetR(), wave.GetG(), wave.GetB()), 7, 49, 36.5f, 207, 36.5f);
        line(g, Color(70, wave.GetR(), wave.GetG(), wave.GetB()), 3.4f, 49, 36.5f, 207, 36.5f);
        line(g, wave, 1.1f, 49, 36.5f, 207, 36.5f);
    }
    button(g, 229, 36.5f, 9, hover == 2, pressed == 2, Color(255, 50, 194, 255));
    gear(g, 229, 36.5f);
    button(g, 253, 36.5f, 9, hover == 3, pressed == 3, Color(255, 50, 194, 255));
    line(g, Color(255, 242, 250, 255), 1.3f, 249.7f, 33.2f, 256.3f, 39.8f);
    line(g, Color(255, 242, 250, 255), 1.3f, 249.7f, 39.8f, 256.3f, 33.2f);

    g.Flush(FlushIntentionSync);
    Graphics output(destination); output.DrawImage(&frame, 0, 0, client_width, client_height);
}
}
