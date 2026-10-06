#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <iterator>
#include "dictator_native.h"
#include "target_probe.h"
#include "widget_painter.h"

struct dictator_host {
    HWND owner{}, widget{};
    HICON icon{};
    HHOOK keyboard{};
    ULONG_PTR graphics_token{};
    DWORD thread{GetCurrentThreadId()};
    uint32_t events{}, theme{}, modifiers{}, key{}, registered_key{};
    int hotkey_id{1}, hover{-1}, pressed_region{-1};
    double zoom{1.0};
    bool layout_backslash{true};
    bool tray{}, key_down{}, dragging{}, positioned{}, dragged{}, placing{}, talking{};
    POINT drag_origin{}, window_origin{};
    uint64_t target{}, hover_at{}, hint_at{}, text_epoch{};
    wchar_t hint[768]{};
    float hint_width{};
    widget_design::ticker_text ticker;
    dictator_target current{};
    wchar_t hotkey[128]{L"Ctrl-Alt-\\"};
    std::unique_ptr<target_probe> probe;
};
namespace {
constexpr UINT tray_message = WM_APP + 17;
constexpr UINT target_message = WM_APP + 18;
constexpr wchar_t owner_class[] = L"Dictator.Resident.1";
constexpr wchar_t widget_class[] = L"Dictator.Widget.1";
thread_local dictator_host* input_host{};
static_assert(sizeof(dictator_target) == 48);
static_assert(sizeof(dictator_input) == 24);
bool correct_thread(dictator_host* h) noexcept { return h && h->thread == GetCurrentThreadId(); }
uint64_t mouse_timestamp() noexcept {
    const auto now = GetTickCount64();
    return now - static_cast<DWORD>(static_cast<DWORD>(now) - static_cast<DWORD>(GetMessageTime()));
}
void hide_tooltip(dictator_host* h) noexcept {
    if (!h->hint[0]) return;
    h->hint[0] = 0; h->hint_width = 0;
    SetWindowTextW(h->widget, L"Dictator Widget"); InvalidateRect(h->widget, nullptr, FALSE);
}
void close_widget(dictator_host* h) noexcept {
    h->talking = false; h->target = 0; h->hover = -1; h->dragging = false; h->pressed_region = -1;
    if (h->probe) h->probe->cancel_inputs();
    if (GetCapture() == h->widget) ReleaseCapture();
    hide_tooltip(h); ++h->text_epoch; h->ticker.clear(GetTickCount64()); ShowWindow(h->widget, SW_HIDE); h->events |= 32;
}
HMONITOR relevant_monitor(dictator_host* h) noexcept {
    if (h->dragged) return MonitorFromWindow(h->widget, MONITOR_DEFAULTTONEAREST);
    const auto foreground = GetForegroundWindow();
    if (foreground && foreground != h->owner && foreground != h->widget)
        return MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    POINT cursor{}; GetCursorPos(&cursor); return MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
}
struct taskbar_bounds { HMONITOR monitor{}; RECT screen{}; int height{}; };
BOOL CALLBACK find_taskbar(HWND window, LPARAM parameter) noexcept {
    auto& result = *reinterpret_cast<taskbar_bounds*>(parameter);
    wchar_t name[64]{}; GetClassNameW(window, name, 64);
    if (wcscmp(name, L"Shell_TrayWnd") != 0 && wcscmp(name, L"Shell_SecondaryTrayWnd") != 0) return TRUE;
    if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) != result.monitor) return TRUE;
    RECT rect{};
    if (GetWindowRect(window, &rect) && rect.bottom >= result.screen.bottom - 2 &&
        rect.right - rect.left >= (result.screen.right - result.screen.left) / 2 &&
        rect.bottom - rect.top < (result.screen.bottom - result.screen.top) / 3)
        result.height = std::max(result.height, static_cast<int>(rect.bottom - rect.top));
    return TRUE;
}
int bottom_taskbar_height(HMONITOR monitor, const RECT& screen) noexcept {
    taskbar_bounds result{monitor, screen};
    // ABM_GETTASKBARPOS reports the full primary taskbar even when it is auto-hidden.
    APPBARDATA bar{sizeof(bar)};
    if (SHAppBarMessage(ABM_GETTASKBARPOS, &bar) && bar.uEdge == ABE_BOTTOM &&
        MonitorFromRect(&bar.rc, MONITOR_DEFAULTTONEAREST) == monitor)
        result.height = static_cast<int>(bar.rc.bottom - bar.rc.top);
    EnumWindows(find_taskbar, reinterpret_cast<LPARAM>(&result));
    return result.height;
}
void place_widget(dictator_host* h, bool show, const POINT* drag_position = nullptr) noexcept {
    if (h->placing) return;
    h->placing = true;
    MONITORINFO monitor{sizeof(monitor)};
    RECT old{}; GetWindowRect(h->widget, &old);
    RECT candidate = old;
    if (drag_position) OffsetRect(&candidate, drag_position->x - old.left, drag_position->y - old.top);
    // Select the destination monitor from the requested rectangle without first
    // moving the HWND there. Otherwise Windows can paint the unclamped position.
    const auto selected = drag_position ? MonitorFromRect(&candidate, MONITOR_DEFAULTTONEAREST) : relevant_monitor(h);
    if (GetMonitorInfoW(selected, &monitor)) {
        UINT dpi_x{96}, dpi_y{96};
        if (FAILED(GetDpiForMonitor(selected, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) dpi_x = GetDpiForWindow(h->widget);
        const auto scale = h->zoom * dpi_x / 96.0;
        auto work = monitor.rcWork;
        const int taskbar_height = bottom_taskbar_height(selected, monitor.rcMonitor);
        if (taskbar_height > 0) work.bottom = std::min(work.bottom, monitor.rcMonitor.bottom - taskbar_height);
        const int clearance = taskbar_height > 0 ? taskbar_height / 2 : static_cast<int>(std::lround(24 * dpi_x / 96.0));
        const int width = std::min(static_cast<int>(std::lround(widget_design::width * scale)), static_cast<int>(work.right - work.left));
        const int height = std::min(static_cast<int>(std::lround(widget_design::height * scale)), static_cast<int>(work.bottom - work.top));
        int x = drag_position ? drag_position->x : h->dragged && h->positioned ? old.left : work.left + (work.right - work.left - width) / 2;
        int y = drag_position ? drag_position->y : h->dragged && h->positioned ? old.top : work.bottom - height - height / 2 - static_cast<int>(16 * scale) - clearance;
        x = std::clamp(x, static_cast<int>(work.left), static_cast<int>(work.right - width));
        y = std::clamp(y, static_cast<int>(work.top), static_cast<int>(work.bottom - height));
        SetWindowPos(h->widget, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : 0));
        auto region = widget_design::window_region(width, height);
        if (region && !SetWindowRgn(h->widget, region, TRUE)) DeleteObject(region);
        h->positioned = true;
        InvalidateRect(h->widget, nullptr, FALSE);
    }
    h->placing = false;
}
int hit_region(HWND hwnd, LPARAM point) noexcept {
    RECT r{}; GetClientRect(hwnd, &r);
    return widget_design::hit_test(r.right, r.bottom, GET_X_LPARAM(point), GET_Y_LPARAM(point));
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
void show_tooltip(dictator_host* h) noexcept {
    if (h->talking || h->hover < 0 || !IsWindowVisible(h->widget) || h->dragging || h->pressed_region >= 0) {
        hide_tooltip(h); return;
    }
    if (GetTickCount64() - h->hover_at < 1000) return;
    wchar_t binding[160]{}, content[768]{};
    const auto copy = tooltip_text(h, binding, std::size(binding));
    for (int i = 0; i < copy.count; ++i) {
        if (i > 0) wcscat_s(content, L"   ·   ");
        wcscat_s(content, copy.lines[i]);
    }
    if (wcscmp(content, h->hint) == 0) return;
    wcscpy_s(h->hint, content); h->hint_at = GetTickCount64();
    h->hint_width = widget_design::measure_text(h->hint, static_cast<int>(wcslen(h->hint)));
    // Expose hover instructions to accessibility clients, never live transcript text.
    SetWindowTextW(h->widget, h->hint); InvalidateRect(h->widget, nullptr, FALSE);
}
uint32_t resolved_key(dictator_host* h) noexcept {
    if (!h->layout_backslash || h->key != 0xDC) return h->key;
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
    if (down == 1 && !IsWindowVisible(h->widget)) place_widget(h, true);
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
        target.token = 0; target.eligible = 0; if (!target.reason) target.reason = 50;
    }
    const bool changed = h->current.token != target.token || h->current.eligible != target.eligible;
    h->current = target;
    if (h->talking && (!target.eligible || h->target != target.token)) {
        h->talking = false; h->target = 0; ++h->text_epoch; h->ticker.clear(now); hide_tooltip(h); h->hover_at = now;
    }
    if (changed || h->talking) InvalidateRect(h->widget, nullptr, FALSE);
    show_tooltip(h);
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
        if (h->hint[0] && IsWindowVisible(h->widget) &&
            (GetTickCount64() - h->hover_at < 1240 || h->hint_width > 252))
            InvalidateRect(h->widget, nullptr, FALSE);
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
void draw_widget(dictator_host* h, HWND hwnd, HDC destination) noexcept {
    RECT rect{}; GetClientRect(hwnd, &rect);
    const auto now = GetTickCount64(); h->ticker.advance(now);
    const auto elapsed = now - h->hover_at;
    const float alpha = widget_design::hover_opacity(elapsed);
    float hint_x = 9;
    if (h->hint_width > 252 && now - h->hint_at > 1200) {
        const auto distance = static_cast<float>(now - h->hint_at - 1200) * .024f;
        hint_x -= std::fmod(distance, h->hint_width + 40);
    }
    widget_design::paint(destination, rect.right, rect.bottom, h->talking, h->current.eligible != 0,
        h->hover, h->pressed_region, now, h->ticker, h->hint, alpha, hint_x);
}
void paint_widget(dictator_host* h, HWND hwnd) noexcept {
    PAINTSTRUCT paint{}; auto surface = BeginPaint(hwnd, &paint);
    draw_widget(h, hwnd, surface); EndPaint(hwnd, &paint);
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
            const POINT requested{h->window_origin.x + cursor.x - h->drag_origin.x,
                h->window_origin.y + cursor.y - h->drag_origin.y};
            h->dragged = true; place_widget(h, true, &requested); hide_tooltip(h); return 0;
        }
        const int region = hit_region(hwnd, lp);
        if (region != h->hover) {
            h->hover = region; h->hover_at = GetTickCount64(); hide_tooltip(h);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&tracking); return 0;
    }
    case WM_MOUSELEAVE: h->hover = -1; hide_tooltip(h); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        h->pressed_region = hit_region(hwnd, lp); hide_tooltip(h); SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        if (h->pressed_region == 0) {
            h->dragging = true; GetCursorPos(&h->drag_origin);
            RECT r{}; GetWindowRect(hwnd, &r); h->window_origin = {r.left, r.top};
        }
        if (h->pressed_region == 1) input_press(h, 2, 1, mouse_timestamp());
        return 0;
    }
    case WM_LBUTTONUP: {
        const auto pressed = h->pressed_region; h->pressed_region = -1; h->dragging = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (pressed == 1) input_press(h, 2, 0, mouse_timestamp());
        else if (pressed == hit_region(hwnd, lp)) {
            if (pressed == 3) close_widget(h);
            if (pressed == 2) h->events |= 2;
        }
        h->hover_at = GetTickCount64(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    }
    case WM_CAPTURECHANGED:
        if (h->pressed_region == 1) input_press(h, 2, 2, GetTickCount64());
        h->pressed_region = -1; h->dragging = false; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_PAINT: paint_widget(h, hwnd); return 0;
    case WM_PRINTCLIENT: draw_widget(h, hwnd, reinterpret_cast<HDC>(wp)); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}

dictator_result DICTATOR_CALL dictator_host_create(uint32_t abi, dictator_host** output) noexcept {
    if (!output) return DICTATOR_INVALID_ARGUMENT;
    *output = nullptr;
    if (abi != DICTATOR_ABI_VERSION) return DICTATOR_ABI_MISMATCH;
    if (input_host) return DICTATOR_PLATFORM_ERROR;
    dictator_host* h{};
    try { h = new dictator_host; } catch (...) { return DICTATOR_OUT_OF_MEMORY; }
    Gdiplus::GdiplusStartupInput graphics;
    if (Gdiplus::GdiplusStartup(&h->graphics_token, &graphics, nullptr) != Gdiplus::Ok) { delete h; return DICTATOR_PLATFORM_ERROR; }
    auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW owner{}; owner.lpfnWndProc = owner_proc; owner.hInstance = instance; owner.lpszClassName = owner_class;
    WNDCLASSW widget{}; widget.lpfnWndProc = widget_proc; widget.hInstance = instance; widget.lpszClassName = widget_class;
    widget.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if ((!RegisterClassW(&owner) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) ||
        (!RegisterClassW(&widget) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)) { Gdiplus::GdiplusShutdown(h->graphics_token); delete h; return DICTATOR_PLATFORM_ERROR; }
    h->owner = CreateWindowExW(WS_EX_TOOLWINDOW, owner_class, L"Dictator Resident", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, h);
    h->widget = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        widget_class, L"Dictator Widget", WS_POPUP, 0, 0, widget_design::width, widget_design::height, h->owner, nullptr, instance, h);
    h->icon = make_icon();
    if (h->owner && h->widget && h->icon) add_tray(h);
    if (!h->tray) { dictator_host_destroy(h); return DICTATOR_PLATFORM_ERROR; }
    try { h->probe = std::make_unique<target_probe>(h->owner); }
    catch (...) { dictator_host_destroy(h); return DICTATOR_PLATFORM_ERROR; }
    input_host = h;
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, owner_class, &module);
    h->keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, module, 0);
    if (!h->keyboard || !SetTimer(h->owner, 1, 16, nullptr)) { dictator_host_destroy(h); return DICTATOR_PLATFORM_ERROR; }
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
    if (h->widget) DestroyWindow(h->widget);
    if (h->owner) DestroyWindow(h->owner);
    if (h->icon) DestroyIcon(h->icon);
    if (h->graphics_token) Gdiplus::GdiplusShutdown(h->graphics_token);
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
dictator_result DICTATOR_CALL dictator_host_bind_hotkey(dictator_host* h, uint32_t modifiers, uint32_t key, uint32_t layout_backslash, const uint16_t* display) noexcept {
    if (!correct_thread(h)) return h ? DICTATOR_WRONG_THREAD : DICTATOR_INVALID_ARGUMENT;
    if (!display || layout_backslash > 1 || modifiers < 1 || modifiers > 15 || key < 0x20 || key > 0xFE ||
        key == VK_LWIN || key == VK_RWIN || (key >= VK_LSHIFT && key <= VK_RMENU) || (modifiers == 3 && key == VK_SPACE))
        return DICTATOR_INVALID_ARGUMENT;
    const auto old_modifiers = h->modifiers, old_key = h->key;
    const auto old_layout_backslash = h->layout_backslash;
    h->modifiers = modifiers; h->key = key; h->layout_backslash = layout_backslash != 0;
    const auto resolved = resolved_key(h);
    if (old_modifiers == modifiers && h->registered_key == resolved) {
        wcsncpy_s(h->hotkey, reinterpret_cast<const wchar_t*>(display), _TRUNCATE); return DICTATOR_OK;
    }
    if (!reserve_hotkey(h, resolved)) {
        h->modifiers = old_modifiers; h->key = old_key; h->layout_backslash = old_layout_backslash;
        if (!old_key) wcsncpy_s(h->hotkey, reinterpret_cast<const wchar_t*>(display), _TRUNCATE);
        return DICTATOR_PLATFORM_ERROR;
    }
    h->key_down = false; h->talking = false; h->target = 0; h->events |= 32;
    ++h->text_epoch; h->ticker.clear(GetTickCount64()); hide_tooltip(h);
    h->probe->cancel_inputs();
    wcsncpy_s(h->hotkey, reinterpret_cast<const wchar_t*>(display), _TRUNCATE);
    show_tooltip(h);
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
        ++h->text_epoch;
        const auto now = GetTickCount64(); h->ticker.clear(now); hide_tooltip(h); h->hover_at = now;
        InvalidateRect(h->widget, nullptr, FALSE);
    }
    return DICTATOR_OK;
}
uintptr_t DICTATOR_CALL dictator_host_widget_handle(dictator_host* h) noexcept {
    return correct_thread(h) ? reinterpret_cast<uintptr_t>(h->widget) : 0;
}
uint64_t DICTATOR_CALL dictator_host_live_text_session(dictator_host* h) noexcept {
    return correct_thread(h) && h->talking ? h->text_epoch : 0;
}
dictator_result DICTATOR_CALL dictator_host_append_live_text(dictator_host* h, uint64_t target, uint64_t session,
    const uint16_t* input, uint32_t count) noexcept {
    if (!correct_thread(h)) return h ? DICTATOR_WRONG_THREAD : DICTATOR_INVALID_ARGUMENT;
    if (!input || count == 0 || count > 2048) return DICTATOR_INVALID_ARGUMENT;
    refresh_target(h);
    if (!h->talking || !target || target != h->target || target != h->current.token ||
        !session || session != h->text_epoch) return DICTATOR_INVALID_ARGUMENT;
    try {
        std::wstring text(reinterpret_cast<const wchar_t*>(input), count);
        for (auto& character : text) {
            if (!character) return DICTATOR_INVALID_ARGUMENT;
            if (character == L'\r' || character == L'\n' || character == L'\t') character = L' ';
        }
        const auto measured = widget_design::measure_text(text.data(), static_cast<int>(text.size()));
        if (!(measured > 0)) return DICTATOR_PLATFORM_ERROR;
        if (!h->ticker.append(std::move(text), measured, GetTickCount64())) return DICTATOR_BUFFER_TOO_SMALL;
        InvalidateRect(h->widget, nullptr, FALSE); return DICTATOR_OK;
    } catch (...) { return DICTATOR_OUT_OF_MEMORY; }
}
uint32_t DICTATOR_CALL dictator_host_tray_ready(dictator_host* h) noexcept { return correct_thread(h) && h->tray ? 1u : 0u; }
