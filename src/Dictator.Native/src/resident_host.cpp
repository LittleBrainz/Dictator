#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <iterator>
#include "dictator_native.h"
#include "target_probe.h"

struct dictator_host {
    HWND owner{}, widget{}, tooltip{};
    HICON icon{};
    HHOOK keyboard{};
    DWORD thread{GetCurrentThreadId()};
    uint32_t events{}, theme{}, modifiers{}, key{}, registered_key{};
    int hotkey_id{1}, hover{-1}, pressed_region{-1};
    double zoom{1.0};
    bool tray{}, key_down{}, dragging{}, positioned{}, dragged{}, placing{}, talking{};
    POINT drag_origin{}, window_origin{};
    uint64_t target{}, hover_at{};
    dictator_target current{};
    wchar_t hotkey[128]{L"Ctrl-Alt-\\"};
    std::unique_ptr<target_probe> probe;
};
namespace {
constexpr UINT tray_message = WM_APP + 17;
constexpr UINT target_message = WM_APP + 18;
constexpr wchar_t owner_class[] = L"Dictator.Resident.1";
constexpr wchar_t widget_class[] = L"Dictator.Widget.1";
constexpr wchar_t tooltip_class[] = L"Dictator.Tooltip.2";
thread_local dictator_host* input_host{};
static_assert(sizeof(dictator_target) == 40);
static_assert(sizeof(dictator_input) == 24);
bool correct_thread(dictator_host* h) noexcept { return h && h->thread == GetCurrentThreadId(); }
void hide_tooltip(dictator_host* h) noexcept { ShowWindow(h->tooltip, SW_HIDE); }
void close_widget(dictator_host* h) noexcept {
    h->talking = false; h->target = 0; h->hover = -1; h->dragging = false;
    if (h->probe) h->probe->cancel_inputs();
    if (GetCapture() == h->widget) ReleaseCapture();
    hide_tooltip(h); ShowWindow(h->widget, SW_HIDE); h->events |= 32;
}
HMONITOR relevant_monitor(dictator_host* h) noexcept {
    if (h->dragged) return MonitorFromWindow(h->widget, MONITOR_DEFAULTTONEAREST);
    const auto foreground = GetForegroundWindow();
    if (foreground && foreground != h->owner && foreground != h->widget)
        return MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    POINT cursor{}; GetCursorPos(&cursor); return MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
}
void place_widget(dictator_host* h, bool show) noexcept {
    if (h->placing) return;
    h->placing = true;
    MONITORINFO monitor{sizeof(monitor)};
    const auto selected = relevant_monitor(h);
    if (GetMonitorInfoW(selected, &monitor)) {
        UINT dpi_x{96}, dpi_y{96};
        if (FAILED(GetDpiForMonitor(selected, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) dpi_x = GetDpiForWindow(h->widget);
        const auto scale = h->zoom * dpi_x / 96.0;
        const auto& work = monitor.rcWork;
        const int width = std::min(static_cast<int>(std::lround(270 * scale)), static_cast<int>(work.right - work.left));
        const int height = std::min(static_cast<int>(std::lround(48 * scale)), static_cast<int>(work.bottom - work.top));
        RECT old{}; GetWindowRect(h->widget, &old);
        int x = h->dragged && h->positioned ? old.left : work.left + (work.right - work.left - width) / 2;
        int y = h->dragged && h->positioned ? old.top : work.bottom - height - height / 2 - static_cast<int>(16 * scale);
        x = std::clamp(x, static_cast<int>(work.left), static_cast<int>(work.right - width));
        y = std::clamp(y, static_cast<int>(work.top), static_cast<int>(work.bottom - height));
        SetWindowPos(h->widget, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : 0));
        auto region = CreateRoundRectRgn(0, 0, width + 1, height + 1, height / 3, height / 3);
        if (region && !SetWindowRgn(h->widget, region, TRUE)) DeleteObject(region);
        h->positioned = true;
        InvalidateRect(h->widget, nullptr, FALSE);
    }
    h->placing = false;
}
bool dark_theme(const dictator_host* h) noexcept {
    if (h->theme != 0) return h->theme == 2;
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}
int hit_region(HWND hwnd, LPARAM point) noexcept {
    RECT r{}; GetClientRect(hwnd, &r);
    const int x = GET_X_LPARAM(point), y = GET_Y_LPARAM(point);
    if (x < 0 || y < 0 || x >= r.right || y >= r.bottom) return -1;
    if (x >= r.right - r.bottom) return 3;
    if (x >= r.right - 2 * r.bottom) return 2;
    if (x >= r.right - 3 * r.bottom) return 1;
    return 0;
}
struct tooltip_copy { const wchar_t* lines[4]{}; int count{}, separator{-1}; };
tooltip_copy tooltip_text(dictator_host* h, wchar_t* binding, size_t capacity) noexcept {
    swprintf_s(binding, capacity, L"Hotkey: %s", h->hotkey);
    const bool eligible = h->current.eligible != 0;
    switch (h->hover) {
    case 0: return {{eligible ? L"Ready to Insert Text" : L"No Text Insertion Cursor", L"Hold to Drag Widget"}, 2, 1};
    case 1:
        if (!eligible) return {{L"No Text Insertion Cursor"}, 1, -1};
        if (h->talking) return {{L"Click to Stop Talking", binding}, 2, 1};
        return {{L"Click to Start Talking", L"Hold to Talk - Release to Stop", binding}, 3, 2};
    case 2: return {{L"Open Dictator Settings"}, 1, -1};
    case 3: return {{L"Close Widget", L"Reopen from Windows System Tray", binding}, 3, 1};
    default: return {};
    }
}
HFONT widget_font(dictator_host* h, int size) noexcept {
    return CreateFontW(-static_cast<int>(std::lround(size * h->zoom * GetDpiForWindow(h->widget) / 96.0)),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}
void show_tooltip(dictator_host* h) noexcept {
    if (h->hover < 0 || !IsWindowVisible(h->widget) || h->dragging || h->pressed_region >= 0) { hide_tooltip(h); return; }
    if (GetTickCount64() - h->hover_at < 1000) return;
    wchar_t binding[160]{};
    const auto copy = tooltip_text(h, binding, std::size(binding));
    auto dc = GetDC(h->tooltip);
    auto font = widget_font(h, 12); auto old = SelectObject(dc, font);
    int width{}, line_height{};
    for (int i = 0; i < copy.count; ++i) {
        SIZE size{}; GetTextExtentPoint32W(dc, copy.lines[i], static_cast<int>(wcslen(copy.lines[i])), &size);
        width = std::max(width, static_cast<int>(size.cx)); line_height = std::max(line_height, static_cast<int>(size.cy));
    }
    SelectObject(dc, old); DeleteObject(font); ReleaseDC(h->tooltip, dc);
    const int pad = std::max(8, line_height / 2), gap = pad;
    const int height = copy.count * (line_height + pad / 2) + pad * 2 + (copy.separator >= 0 ? gap : 0);
    width += 2 * pad;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(h->widget, MONITOR_DEFAULTTONEAREST), &monitor);
    const auto& work = monitor.rcWork;
    width = std::min(width, static_cast<int>(work.right - work.left));
    RECT r{}; GetWindowRect(h->widget, &r);
    int x = std::clamp(static_cast<int>(r.left + (r.right - r.left - width) / 2), static_cast<int>(work.left), static_cast<int>(work.right - width));
    int y = r.top - height - gap;
    if (y < work.top) y = r.bottom + gap;
    y = std::clamp(y, static_cast<int>(work.top), std::max(static_cast<int>(work.top), static_cast<int>(work.bottom - height)));
    // Compute final coordinates before first visibility: no tooltip origin flash.
    SetWindowPos(h->tooltip, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(h->tooltip, nullptr, FALSE);
}
LRESULT CALLBACK tooltip_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
    auto* h = reinterpret_cast<dictator_host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        h = static_cast<dictator_host*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(h));
    }
    if (!h) return DefWindowProcW(hwnd, message, wp, lp);
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; auto dc = BeginPaint(hwnd, &paint);
        RECT r{}; GetClientRect(hwnd, &r);
        const bool dark = dark_theme(h);
        auto brush = CreateSolidBrush(dark ? RGB(36, 40, 48) : RGB(255, 255, 255));
        FillRect(dc, &r, brush); DeleteObject(brush);
        auto font = widget_font(h, 12); auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, dark ? RGB(242, 244, 248) : RGB(30, 34, 42));
        TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
        const int pad = std::max(8, static_cast<int>(metrics.tmHeight / 2));
        int y = pad;
        wchar_t binding[160]{}; const auto copy = tooltip_text(h, binding, std::size(binding));
        for (int i = 0; i < copy.count; ++i) {
            if (i == copy.separator) {
                auto pen = CreatePen(PS_SOLID, 1, dark ? RGB(90, 96, 110) : RGB(210, 215, 225));
                auto old_pen = SelectObject(dc, pen);
                MoveToEx(dc, pad, y + pad / 2, nullptr); LineTo(dc, r.right - pad, y + pad / 2);
                SelectObject(dc, old_pen); DeleteObject(pen); y += pad;
            }
            TextOutW(dc, pad, y, copy.lines[i], static_cast<int>(wcslen(copy.lines[i])));
            y += metrics.tmHeight + pad / 2;
        }
        SelectObject(dc, old); DeleteObject(font); EndPaint(hwnd, &paint); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
uint32_t resolved_key(dictator_host* h) noexcept {
    if (h->key != 0xDC || h->modifiers != 3) return h->key;
    const auto foreground = GetForegroundWindow();
    const auto layout = GetKeyboardLayout(GetWindowThreadProcessId(foreground, nullptr));
    const auto mapped = VkKeyScanExW(L'\\', layout);
    return mapped == -1 ? h->key : static_cast<uint32_t>(LOBYTE(mapped));
}
bool reserve_hotkey(dictator_host* h, uint32_t key) noexcept {
    const int candidate = h->hotkey_id == 1 ? 2 : 1;
    if (!RegisterHotKey(h->owner, candidate, h->modifiers | MOD_NOREPEAT, key)) return false;
    UnregisterHotKey(h->owner, h->hotkey_id);
    h->hotkey_id = candidate; h->registered_key = key; return true;
}
void input_press(dictator_host* h, uint32_t source, uint32_t down, uint64_t timestamp) noexcept {
    if (down && !IsWindowVisible(h->widget)) place_widget(h, true);
    h->probe->input(source, down, timestamp);
}
LRESULT CALLBACK keyboard_proc(int code, WPARAM wp, LPARAM lp) noexcept {
    auto* h = input_host;
    if (code == HC_ACTION && h && h->registered_key) {
        const auto* input = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        const bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
        if (input->vkCode == h->registered_key && (down || up)) {
            const auto modifiers = ((GetAsyncKeyState(VK_MENU) & 0x8000) ? 1u : 0u) |
                ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 2u : 0u) |
                ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 4u : 0u) |
                (((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) ? 8u : 0u);
            if (down && !h->key_down && modifiers == h->modifiers) {
                h->key_down = true;
                const auto now = GetTickCount64();
                input_press(h, 1, 1, now - static_cast<DWORD>(static_cast<DWORD>(now) - input->time)); return 1;
            }
            if (h->key_down) {
                if (up) { h->key_down = false;
                    const auto now = GetTickCount64();
                    input_press(h, 1, 0, now - static_cast<DWORD>(static_cast<DWORD>(now) - input->time)); }
                return 1; // Only the configured gesture; modifier order is irrelevant to release.
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
void refresh_target(dictator_host* h) noexcept {
    auto target = h->probe->snapshot();
    const auto now = GetTickCount64();
    GUITHREADINFO gui{sizeof(gui)};
    const auto thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    if (now - target.checked_at > 400 || reinterpret_cast<HWND>(target.foreground) != GetForegroundWindow() ||
        !GetGUIThreadInfo(thread, &gui) || reinterpret_cast<HWND>(target.focus) != gui.hwndFocus) {
        target.token = 0; target.eligible = 0;
    }
    const bool changed = h->current.token != target.token || h->current.eligible != target.eligible;
    h->current = target;
    if (h->talking && (!target.eligible || h->target != target.token)) { h->talking = false; h->target = 0; }
    if (changed || h->talking) InvalidateRect(h->widget, nullptr, FALSE);
    if (IsWindowVisible(h->tooltip)) show_tooltip(h);
}
HICON make_icon() noexcept {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        owner_class, &module)) return nullptr;
    return static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
}
void add_tray(dictator_host* h) noexcept {
    NOTIFYICONDATAW info{}; info.cbSize = sizeof(info); info.hWnd = h->owner; info.uID = 1;
    info.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP; info.uCallbackMessage = tray_message; info.hIcon = h->icon;
    wcscpy_s(info.szTip, L"Dictator"); h->tray = Shell_NotifyIconW(NIM_ADD, &info) != FALSE;
    if (h->tray) { info.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &info); }
}
LRESULT CALLBACK owner_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
    auto* h = reinterpret_cast<dictator_host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        h = static_cast<dictator_host*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(h));
    }
    if (!h) return DefWindowProcW(hwnd, message, wp, lp);
    if (message == RegisterWindowMessageW(L"TaskbarCreated")) { h->owner = hwnd; add_tray(h); return 0; }
    if (message == target_message && h->probe) { refresh_target(h); return 0; }
    if (message == WM_HOTKEY) return 0; // Reservation/conflict detection; hook supplies both edges.
    if (message == WM_TIMER && h->probe) {
        refresh_target(h); show_tooltip(h);
        const auto key = resolved_key(h);
        if (h->registered_key && !h->key_down && key != h->registered_key) {
            if (!reserve_hotkey(h, key)) { h->events |= 16; UnregisterHotKey(h->owner, h->hotkey_id); h->registered_key = 0; }
        }
        return 0;
    }
    if (message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE) {
        if (h->widget) place_widget(h, IsWindowVisible(h->widget) != FALSE);
        return 0;
    }
    if (message == tray_message) {
        const auto event = LOWORD(lp);
        if (event == NIN_SELECT || event == NIN_KEYSELECT) { h->events |= 1; return 0; }
        if (event == WM_CONTEXTMENU) {
            auto menu = CreatePopupMenu(); if (!menu) return 0;
            AppendMenuW(menu, MF_STRING, 1, L"Open Widget"); AppendMenuW(menu, MF_STRING, 2, L"Open Settings");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, 4, L"Restart Dictator");
            AppendMenuW(menu, MF_STRING, 8, L"Quit"); SetMenuDefaultItem(menu, 1, FALSE);
            POINT point{static_cast<short>(LOWORD(wp)), static_cast<short>(HIWORD(wp))};
            if (point.x == -1 && point.y == -1) GetCursorPos(&point);
            const auto foreground = GetForegroundWindow(); SetForegroundWindow(hwnd);
            const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                point.x, point.y, 0, hwnd, nullptr);
            h->events |= command; DestroyMenu(menu); PostMessageW(hwnd, WM_NULL, 0, 0);
            if (command != 2 && foreground && IsWindow(foreground)) SetForegroundWindow(foreground);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
void paint_widget(dictator_host* h, HWND hwnd) noexcept {
    PAINTSTRUCT paint{}; auto surface = BeginPaint(hwnd, &paint);
    RECT r{}; GetClientRect(hwnd, &r);
    auto dc = CreateCompatibleDC(surface); auto bitmap = CreateCompatibleBitmap(surface, r.right, r.bottom);
    auto old_bitmap = SelectObject(dc, bitmap);
    const bool dark = dark_theme(h);
    auto background = CreateSolidBrush(dark ? RGB(28, 32, 40) : RGB(245, 247, 251));
    FillRect(dc, &r, background); DeleteObject(background);
    const COLORREF state = h->talking ? RGB(35, 190, 105) : h->current.eligible ?
        (dark ? RGB(164, 175, 195) : RGB(88, 102, 126)) : RGB(218, 65, 78);
    const int height = r.bottom, body_right = r.right - 3 * height;
    auto pen = CreatePen(PS_SOLID, std::max(2, height / 20), state); auto old_pen = SelectObject(dc, pen);
    for (int i = 0; i < 17; ++i) {
        const int x = height / 3 + i * std::max(1, (body_right - 2 * height / 3) / 16);
        const double wave = h->talking ? std::abs(std::sin(static_cast<double>(GetTickCount64()) / 190.0 + i * .65)) : 0.0;
        const int length = h->talking ? static_cast<int>((.12 + .5 * wave) * height) : std::max(2, height / 16);
        MoveToEx(dc, x, (height - length) / 2, nullptr); LineTo(dc, x, (height + length) / 2);
    }
    // Microphone silhouette, scaled with the complete Widget.
    const int cx = r.right - 5 * height / 2, cy = height / 2;
    auto fill = CreateSolidBrush(state); auto old_brush = SelectObject(dc, fill);
    RoundRect(dc, cx - height / 12, cy - height / 4, cx + height / 12, cy + height / 10, height / 6, height / 6);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Arc(dc, cx - height / 6, cy - height / 8, cx + height / 6, cy + height / 4,
        cx - height / 6, cy, cx + height / 6, cy);
    MoveToEx(dc, cx, cy + height / 4, nullptr); LineTo(dc, cx, cy + height / 3);
    MoveToEx(dc, cx - height / 9, cy + height / 3, nullptr); LineTo(dc, cx + height / 9, cy + height / 3);
    if (!h->current.eligible) { MoveToEx(dc, cx - height / 4, cy + height / 4, nullptr); LineTo(dc, cx + height / 4, cy - height / 4); }
    SelectObject(dc, old_brush); DeleteObject(fill); SelectObject(dc, old_pen); DeleteObject(pen);
    auto font = widget_font(h, 18); auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, dark ? RGB(235, 240, 248) : RGB(45, 54, 70));
    RECT tools{r.right - 2 * height, 0, r.right - height, height};
    DrawTextW(dc, L"⚙", -1, &tools, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT close{r.right - height, 0, r.right, height};
    DrawTextW(dc, L"×", -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old_font); DeleteObject(font);
    BitBlt(surface, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap); DeleteObject(bitmap); DeleteDC(dc); EndPaint(hwnd, &paint);
}
LRESULT CALLBACK widget_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
    auto* h = reinterpret_cast<dictator_host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        h = static_cast<dictator_host*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(h));
    }
    if (!h) return DefWindowProcW(hwnd, message, wp, lp);
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: close_widget(h); return 0;
    case WM_DPICHANGED: place_widget(h, IsWindowVisible(hwnd) != FALSE); return 0;
    case WM_SETTINGCHANGE: InvalidateRect(hwnd, nullptr, FALSE); place_widget(h, IsWindowVisible(hwnd) != FALSE); return 0;
    case WM_MOUSEMOVE: {
        if (h->dragging) {
            POINT cursor{}; GetCursorPos(&cursor);
            SetWindowPos(hwnd, nullptr, h->window_origin.x + cursor.x - h->drag_origin.x,
                h->window_origin.y + cursor.y - h->drag_origin.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
            h->dragged = true; place_widget(h, true); hide_tooltip(h); return 0;
        }
        const int region = hit_region(hwnd, lp);
        if (region != h->hover) { h->hover = region; h->hover_at = GetTickCount64(); hide_tooltip(h); }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&tracking); return 0;
    }
    case WM_MOUSELEAVE: h->hover = -1; hide_tooltip(h); return 0;
    case WM_LBUTTONDOWN: {
        h->pressed_region = hit_region(hwnd, lp); hide_tooltip(h); SetCapture(hwnd);
        if (h->pressed_region == 0) {
            h->dragging = true; GetCursorPos(&h->drag_origin);
            RECT r{}; GetWindowRect(hwnd, &r); h->window_origin = {r.left, r.top};
        }
        if (h->pressed_region == 1) input_press(h, 2, 1, GetTickCount64());
        return 0;
    }
    case WM_LBUTTONUP: {
        const auto pressed = h->pressed_region; h->pressed_region = -1; h->dragging = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (pressed == 1) input_press(h, 2, 0, GetTickCount64());
        else if (pressed == hit_region(hwnd, lp)) {
            if (pressed == 3) close_widget(h);
            if (pressed == 2) h->events |= 2;
        }
        h->hover_at = GetTickCount64(); return 0;
    }
    case WM_CAPTURECHANGED:
        if (h->pressed_region == 1) { h->talking = false; h->target = 0; h->events |= 32; }
        h->pressed_region = -1; h->dragging = false; return 0;
    case WM_PAINT: paint_widget(h, hwnd); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}

dictator_result DICTATOR_CALL dictator_host_create(uint32_t abi, dictator_host** output) noexcept {
    if (!output) return DICTATOR_INVALID_ARGUMENT;
    *output = nullptr;
    if (abi != DICTATOR_ABI_VERSION) return DICTATOR_ABI_MISMATCH;
    if (input_host) return DICTATOR_PLATFORM_ERROR;
    auto* h = new (std::nothrow) dictator_host;
    if (!h) return DICTATOR_OUT_OF_MEMORY;
    auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW owner{}; owner.lpfnWndProc = owner_proc; owner.hInstance = instance; owner.lpszClassName = owner_class;
    WNDCLASSW widget{}; widget.lpfnWndProc = widget_proc; widget.hInstance = instance; widget.lpszClassName = widget_class;
    widget.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    WNDCLASSW tooltip{}; tooltip.lpfnWndProc = tooltip_proc; tooltip.hInstance = instance; tooltip.lpszClassName = tooltip_class;
    if ((!RegisterClassW(&owner) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) ||
        (!RegisterClassW(&widget) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) ||
        (!RegisterClassW(&tooltip) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)) { delete h; return DICTATOR_PLATFORM_ERROR; }
    h->owner = CreateWindowExW(WS_EX_TOOLWINDOW, owner_class, L"Dictator Resident", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, h);
    h->widget = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        widget_class, L"Dictator Widget", WS_POPUP, 0, 0, 270, 48, h->owner, nullptr, instance, h);
    h->tooltip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        tooltip_class, L"Dictator Widget Tooltip", WS_POPUP | WS_BORDER, 0, 0, 0, 0, h->owner, nullptr, instance, h);
    h->icon = make_icon();
    if (h->owner && h->widget && h->tooltip && h->icon) add_tray(h);
    if (!h->tray) { dictator_host_destroy(h); return DICTATOR_PLATFORM_ERROR; }
    try { h->probe = std::make_unique<target_probe>(h->owner); }
    catch (...) { dictator_host_destroy(h); return DICTATOR_PLATFORM_ERROR; }
    input_host = h;
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, owner_class, &module);
    h->keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, module, 0);
    if (!h->keyboard || !SetTimer(h->owner, 1, 40, nullptr)) { dictator_host_destroy(h); return DICTATOR_PLATFORM_ERROR; }
    *output = h; return DICTATOR_OK;
}
void DICTATOR_CALL dictator_host_destroy(dictator_host* h) noexcept {
    if (!correct_thread(h)) return;
    KillTimer(h->owner, 1); UnregisterHotKey(h->owner, h->hotkey_id);
    if (h->keyboard) UnhookWindowsHookEx(h->keyboard);
    if (input_host == h) input_host = nullptr;
    h->probe.reset();
    NOTIFYICONDATAW info{}; info.cbSize = sizeof(info); info.hWnd = h->owner; info.uID = 1;
    if (h->tray) Shell_NotifyIconW(NIM_DELETE, &info);
    if (h->tooltip) DestroyWindow(h->tooltip);
    if (h->widget) DestroyWindow(h->widget);
    if (h->owner) DestroyWindow(h->owner);
    if (h->icon) DestroyIcon(h->icon);
    delete h;
}
uint32_t DICTATOR_CALL dictator_host_poll_events(dictator_host* h) noexcept {
    if (!correct_thread(h)) return 0;
    auto events = h->events; h->events = 0; return events;
}
dictator_result DICTATOR_CALL dictator_host_set_widget(dictator_host* h, uint32_t visible, double zoom, uint32_t theme) noexcept {
    if (!correct_thread(h)) return h ? DICTATOR_WRONG_THREAD : DICTATOR_INVALID_ARGUMENT;
    if (visible > 1 || theme > 2 || !(zoom == .750 || zoom == .866 || zoom == 1.000 || zoom == 1.155 || zoom == 1.333))
        return DICTATOR_INVALID_ARGUMENT;
    h->zoom = zoom; h->theme = theme;
    if (!visible) close_widget(h);
    else place_widget(h, true);
    return DICTATOR_OK;
}
dictator_result DICTATOR_CALL dictator_host_bind_hotkey(dictator_host* h, uint32_t modifiers, uint32_t key, const uint16_t* display) noexcept {
    if (!correct_thread(h)) return h ? DICTATOR_WRONG_THREAD : DICTATOR_INVALID_ARGUMENT;
    if (!display || modifiers < 1 || modifiers > 15 || key < 0x20 || key > 0xFE ||
        key == VK_LWIN || key == VK_RWIN || (key >= VK_LSHIFT && key <= VK_RMENU) || (modifiers == 3 && key == VK_SPACE))
        return DICTATOR_INVALID_ARGUMENT;
    const auto old_modifiers = h->modifiers, old_key = h->key;
    h->modifiers = modifiers; h->key = key;
    const auto resolved = resolved_key(h);
    if (old_modifiers == modifiers && h->registered_key == resolved) {
        wcsncpy_s(h->hotkey, reinterpret_cast<const wchar_t*>(display), _TRUNCATE); return DICTATOR_OK;
    }
    if (!reserve_hotkey(h, resolved)) { h->modifiers = old_modifiers; h->key = old_key; return DICTATOR_PLATFORM_ERROR; }
    h->key_down = false; h->talking = false; h->target = 0; h->events |= 32;
    h->probe->cancel_inputs();
    wcsncpy_s(h->hotkey, reinterpret_cast<const wchar_t*>(display), _TRUNCATE);
    if (IsWindowVisible(h->tooltip)) show_tooltip(h);
    return DICTATOR_OK;
}
dictator_result DICTATOR_CALL dictator_host_target(dictator_host* h, dictator_target* output) noexcept {
    if (!correct_thread(h)) return h ? DICTATOR_WRONG_THREAD : DICTATOR_INVALID_ARGUMENT;
    if (!output) return DICTATOR_INVALID_ARGUMENT;
    refresh_target(h); *output = h->current; return DICTATOR_OK;
}
uint32_t DICTATOR_CALL dictator_host_input(dictator_host* h, dictator_input* output) noexcept {
    return correct_thread(h) && output && h->probe->pop(*output) ? 1u : 0u;
}
dictator_result DICTATOR_CALL dictator_host_preview(dictator_host* h, uint64_t target) noexcept {
    if (!correct_thread(h)) return h ? DICTATOR_WRONG_THREAD : DICTATOR_INVALID_ARGUMENT;
    refresh_target(h);
    const bool before = h->talking;
    h->talking = target && h->current.eligible && target == h->current.token;
    h->target = h->talking ? target : 0;
    if (before != h->talking) {
        InvalidateRect(h->widget, nullptr, FALSE);
        if (IsWindowVisible(h->tooltip)) show_tooltip(h);
    }
    return DICTATOR_OK;
}
uintptr_t DICTATOR_CALL dictator_host_widget_handle(dictator_host* h) noexcept {
    return correct_thread(h) ? reinterpret_cast<uintptr_t>(h->widget) : 0;
}
uint32_t DICTATOR_CALL dictator_host_tray_ready(dictator_host* h) noexcept { return correct_thread(h) && h->tray ? 1u : 0u; }
