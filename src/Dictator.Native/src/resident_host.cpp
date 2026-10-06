#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cmath>
#include <new>
#include "dictator_native.h"

struct dictator_host {
    HWND owner{}, widget{};
    HICON icon{};
    DWORD thread{GetCurrentThreadId()};
    uint32_t events{}, theme{};
    double zoom{1.0};
    bool tray{};
};

namespace {
constexpr UINT tray_message = WM_APP + 17;
constexpr wchar_t owner_class[] = L"Dictator.Resident.1";
constexpr wchar_t widget_class[] = L"Dictator.Widget.1";
bool correct_thread(dictator_host* h) noexcept { return h && h->thread == GetCurrentThreadId(); }

HICON make_icon() noexcept {
    auto dc = GetDC(nullptr);
    auto color = CreateCompatibleBitmap(dc, 32, 32);
    auto mask = CreateBitmap(32, 32, 1, 1, nullptr);
    auto canvas = CreateCompatibleDC(dc);
    auto previous = SelectObject(canvas, color);
    RECT r{0, 0, 32, 32};
    auto brush = CreateSolidBrush(RGB(42, 85, 145));
    FillRect(canvas, &r, brush);
    DeleteObject(brush);
    auto font = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto old_font = SelectObject(canvas, font);
    SetBkMode(canvas, TRANSPARENT);
    SetTextColor(canvas, RGB(255, 255, 255));
    DrawTextW(canvas, L"D", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(canvas, old_font);
    DeleteObject(font);
    SelectObject(canvas, mask);
    PatBlt(canvas, 0, 0, 32, 32, BLACKNESS);
    SelectObject(canvas, previous);
    DeleteDC(canvas);
    ReleaseDC(nullptr, dc);
    ICONINFO info{TRUE, 0, 0, mask, color};
    auto icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

void add_tray(dictator_host* h) noexcept {
    NOTIFYICONDATAW info{};
    info.cbSize = sizeof(info);
    info.hWnd = h->owner;
    info.uID = 1;
    info.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    info.uCallbackMessage = tray_message;
    info.hIcon = h->icon;
    wcscpy_s(info.szTip, L"Dictator");
    h->tray = Shell_NotifyIconW(NIM_ADD, &info) != FALSE;
    if (h->tray) {
        info.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &info);
    }
}

LRESULT CALLBACK owner_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
    auto* h = reinterpret_cast<dictator_host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        h = static_cast<dictator_host*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(h));
    }
    if (!h) return DefWindowProcW(hwnd, message, wp, lp);
    if (message == RegisterWindowMessageW(L"TaskbarCreated")) { h->owner = hwnd; add_tray(h); return 0; }
    if (message == tray_message) {
        const auto event = LOWORD(lp);
        if (event == NIN_SELECT || event == NIN_KEYSELECT) { h->events |= 1; return 0; }
        if (event == WM_CONTEXTMENU) {
            auto menu = CreatePopupMenu();
            if (!menu) return 0;
            AppendMenuW(menu, MF_STRING, 1, L"Open Widget");
            AppendMenuW(menu, MF_STRING, 2, L"Open Settings");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 4, L"Restart Dictator");
            AppendMenuW(menu, MF_STRING, 8, L"Quit");
            SetMenuDefaultItem(menu, 1, FALSE);
            POINT point{static_cast<short>(LOWORD(wp)), static_cast<short>(HIWORD(wp))};
            if (point.x == -1 && point.y == -1) GetCursorPos(&point);
            const auto foreground = GetForegroundWindow();
            SetForegroundWindow(hwnd);
            const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                point.x, point.y, 0, hwnd, nullptr);
            h->events |= command;
            DestroyMenu(menu);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            if (command != 2 && foreground && IsWindow(foreground)) SetForegroundWindow(foreground);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

bool dark_theme(const dictator_host* h) noexcept {
    if (h->theme != 0) return h->theme == 2;
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
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
    case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_SETTINGCHANGE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: {
        RECT r{}; GetClientRect(hwnd, &r);
        const auto x = static_cast<short>(LOWORD(lp));
        if (x >= r.right - (r.bottom * 1)) ShowWindow(hwnd, SW_HIDE);
        else if (x >= r.right - (r.bottom * 2)) h->events |= 2;
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(hwnd, &paint);
        RECT r{}; GetClientRect(hwnd, &r);
        const bool dark = dark_theme(h);
        auto brush = CreateSolidBrush(dark ? RGB(28, 32, 40) : RGB(245, 247, 251));
        FillRect(dc, &r, brush); DeleteObject(brush);
        auto font = CreateFontW(-static_cast<int>(15 * h->zoom * GetDpiForWindow(hwnd) / 96.0),
            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dark ? RGB(240, 242, 247) : RGB(28, 32, 40));
        RECT text{r.bottom / 3, 0, r.right - 2 * r.bottom, r.bottom};
        DrawTextW(dc, L"Dictator  ·  Idle", -1, &text, DT_VCENTER | DT_SINGLELINE);
        RECT settings{r.right - 2 * r.bottom, 0, r.right - r.bottom, r.bottom};
        DrawTextW(dc, L"⚙", -1, &settings, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT close{r.right - r.bottom, 0, r.right, r.bottom};
        DrawTextW(dc, L"×", -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old); DeleteObject(font);
        EndPaint(hwnd, &paint); return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}

dictator_result DICTATOR_CALL dictator_host_create(uint32_t abi, dictator_host** output) noexcept {
    if (!output) return DICTATOR_INVALID_ARGUMENT;
    *output = nullptr;
    if (abi != DICTATOR_ABI_VERSION) return DICTATOR_ABI_MISMATCH;
    auto* h = new (std::nothrow) dictator_host;
    if (!h) return DICTATOR_OUT_OF_MEMORY;
    auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW owner{};
    owner.lpfnWndProc = owner_proc; owner.hInstance = instance; owner.lpszClassName = owner_class;
    WNDCLASSW widget{};
    widget.lpfnWndProc = widget_proc; widget.hInstance = instance; widget.lpszClassName = widget_class;
    widget.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Classes intentionally remain registered for the lifetime of this process.
    if ((!RegisterClassW(&owner) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) ||
        (!RegisterClassW(&widget) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)) { delete h; return DICTATOR_PLATFORM_ERROR; }
    h->owner = CreateWindowExW(WS_EX_TOOLWINDOW, owner_class, L"Dictator Resident", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, h);
    h->widget = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        widget_class, L"Dictator Widget", WS_POPUP | WS_BORDER,
        0, 0, 270, 48, h->owner, nullptr, instance, h);
    h->icon = make_icon();
    if (h->owner && h->widget && h->icon) add_tray(h);
    if (!h->tray) { dictator_host_destroy(h); return DICTATOR_PLATFORM_ERROR; }
    *output = h;
    return DICTATOR_OK;
}

void DICTATOR_CALL dictator_host_destroy(dictator_host* h) noexcept {
    if (!correct_thread(h)) return;
    NOTIFYICONDATAW info{}; info.cbSize = sizeof(info); info.hWnd = h->owner; info.uID = 1;
    if (h->tray) Shell_NotifyIconW(NIM_DELETE, &info);
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
    if (visible > 1 || theme > 2 || !std::isfinite(zoom) || zoom < .75 || zoom > 1.333) return DICTATOR_INVALID_ARGUMENT;
    h->zoom = zoom; h->theme = theme;
    if (!visible) { ShowWindow(h->widget, SW_HIDE); return DICTATOR_OK; }
    // Phase 1 placement is bottom-centre of the cursor monitor; Phase 2 adds drag and DPI transitions.
    POINT cursor{}; GetCursorPos(&cursor);
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor)) return DICTATOR_PLATFORM_ERROR;
    const auto dpi = GetDpiForWindow(h->widget);
    const auto width = static_cast<int>(270 * zoom * dpi / 96.0);
    const auto height = static_cast<int>(48 * zoom * dpi / 96.0);
    const auto& work = monitor.rcWork;
    SetWindowPos(h->widget, HWND_TOPMOST, work.left + (work.right - work.left - width) / 2,
        work.bottom - height - 20, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(h->widget, nullptr, FALSE);
    return DICTATOR_OK;
}

uintptr_t DICTATOR_CALL dictator_host_widget_handle(dictator_host* h) noexcept {
    return correct_thread(h) ? reinterpret_cast<uintptr_t>(h->widget) : 0;
}
uint32_t DICTATOR_CALL dictator_host_tray_ready(dictator_host* h) noexcept { return correct_thread(h) && h->tray ? 1u : 0u; }
