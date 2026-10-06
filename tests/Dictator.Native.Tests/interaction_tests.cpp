#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "dictator_native.h"

namespace {
HWND editable{}, readonly_edit{}, button{};
WNDPROC original_edit{};
WNDPROC original_widget{};
bool observe_moves{};
RECT previous_position{};
std::vector<RECT> applied_positions;
int escapes{}, unrelated{};
constexpr wchar_t sentinel[] = L"Phase 2 target text stays unchanged.";
LRESULT CALLBACK edit_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_KEYDOWN && wp == VK_ESCAPE) ++escapes;
    if (message == WM_KEYDOWN && wp == VK_F6) ++unrelated;
    return CallWindowProcW(original_edit, hwnd, message, wp, lp);
}
LRESULT CALLBACK observe_widget(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_WINDOWPOSCHANGED && observe_moves) {
        RECT position{}; GetWindowRect(hwnd, &position);
        if (!EqualRect(&position, &previous_position)) {
            applied_positions.push_back(position); previous_position = position;
        }
    }
    return CallWindowProcW(original_widget, hwnd, message, wp, lp);
}
struct position_observer {
    HWND widget;
    explicit position_observer(HWND window) : widget(window) {
        original_widget = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(widget, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(observe_widget)));
    }
    ~position_observer() { SetWindowLongPtrW(widget, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_widget)); }
};
LRESULT CALLBACK fixture_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CREATE) {
        editable = CreateWindowExW(0, L"EDIT", sentinel, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            20, 20, 340, 32, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        readonly_edit = CreateWindowExW(0, L"EDIT", L"Read only", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY,
            20, 70, 340, 32, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        button = CreateWindowExW(0, L"BUTTON", L"Non text", WS_CHILD | WS_VISIBLE,
            20, 120, 100, 32, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        original_edit = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(editable, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(edit_proc)));
        SetFocus(editable); SendMessageW(editable, EM_SETSEL, 7, 7); return 0;
    }
    if (message == WM_APP + 1) { EnableWindow(editable, TRUE); SetFocus(editable); return 0; }
    if (message == WM_APP + 2) { SetFocus(readonly_edit); return 0; }
    if (message == WM_APP + 3) { SetFocus(button); return 0; }
    if (message == WM_APP + 4) { EnableWindow(editable, FALSE); SetFocus(button); return 0; }
    if (message == WM_APP + 5) { ActivateKeyboardLayout(LoadKeyboardLayoutW(wp ? L"00000809" : L"00000409", KLF_ACTIVATE), 0); return 0; }
    if (message == WM_APP + 10) {
        wchar_t text[128]{}; GetWindowTextW(editable, text, 128);
        DWORD start{}, end{}; SendMessageW(editable, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        return wcscmp(text, sentinel) == 0 && start == 7 && end == 7;
    }
    if (message == WM_APP + 11) return escapes;
    if (message == WM_APP + 12) return unrelated;
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wp, lp);
}
int fixture() {
    WNDCLASSW wc{}; wc.lpfnWndProc = fixture_proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"Dictator.Phase2.Target";
    if (!RegisterClassW(&wc)) return 2;
    const auto window = CreateWindowExW(0, wc.lpszClassName, L"Dictator Phase 2 target fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, 420, 240, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 3;
    SetForegroundWindow(window); SetFocus(editable);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    GdiFlush();
}
template<class Predicate> bool until(Predicate predicate, int timeout = 3000) {
    const auto deadline = GetTickCount64() + static_cast<ULONGLONG>(timeout);
    do { pump(); if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    while (GetTickCount64() < deadline);
    return false;
}
void key(WORD code, bool up = false) {
    INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = code; input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (SendInput(1, &input, sizeof(input)) != 1) throw std::runtime_error("SendInput failed");
    pump();
}
bool capture(HWND widget, const wchar_t* name, bool quiet, bool eligible) {
    RECT r{}; GetClientRect(widget, &r);
    const auto screen = GetDC(widget), dc = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, r.right, r.bottom);
    const auto old = SelectObject(dc, bitmap);
    SendMessageW(widget, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    GdiFlush();
    COLORREF center{};
    // Sample across the antialiased line rather than assuming a pixel-center phase.
    for (int y = r.bottom * 35 / 56; y <= r.bottom * 38 / 56; ++y) {
        const auto pixel = GetPixel(dc, r.right * 128 / 270, y);
        if (eligible ? GetGValue(pixel) > GetGValue(center) : GetRValue(pixel) > GetRValue(center)) center = pixel;
    }
    const auto above = GetPixel(dc, r.right * 128 / 270, r.bottom * 28 / 56);
    bool ok = !quiet || (eligible ? GetGValue(center) > 110 && GetBValue(center) > 160 :
        GetRValue(center) > 150 && GetGValue(center) < 120);
    // A quiet waveform must remain a single horizontal line, with no vertical bars.
    if (quiet) ok = ok && GetGValue(above) < 100 && GetRValue(above) < 100;
    wchar_t folder[32768]{};
    if (GetEnvironmentVariableW(L"DICTATOR_WIDGET_EVIDENCE", folder, 32768)) {
        std::filesystem::create_directories(folder);
        const auto path = std::filesystem::path(folder) / (std::wstring(name) + L".png");
        Gdiplus::Bitmap image(bitmap, nullptr);
        const CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0, 0, 0xf8, 0x1e, 0xf3, 0x2e}};
        ok = image.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok && ok;
    }
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(widget, screen);
    return ok;
}
struct session {
    PROCESS_INFORMATION child{};
    dictator_host* host{};
    ~session() {
        if (host) dictator_host_destroy(host);
        if (child.hProcess) { TerminateProcess(child.hProcess, 0); WaitForSingleObject(child.hProcess, 3000); CloseHandle(child.hProcess); CloseHandle(child.hThread); }
    }
};
#define CHECK(expression) do { if (!(expression)) { std::cerr << "Failed at line " << __LINE__ << ": " #expression << '\n'; return 1; } } while (false)
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && wcscmp(argv[1], L"--target-fixture") == 0) return fixture();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    session test;
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --target-fixture";
    STARTUPINFOW startup{sizeof(startup)};
    CHECK(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &test.child));
    HWND target{};
    CHECK(until([&] { target = FindWindowW(L"Dictator.Phase2.Target", nullptr); return target != nullptr; }));
    SetForegroundWindow(target); SendMessageW(target, WM_APP + 1, 0, 0);
    CHECK(dictator_host_create(1, &test.host) == DICTATOR_OK);
    auto* host = test.host;
    const auto widget = reinterpret_cast<HWND>(dictator_host_widget_handle(host));
    CHECK(dictator_host_set_widget(host, 1, 1., 0) == DICTATOR_OK);
    CHECK(GetForegroundWindow() == target);
    auto snapshot = [&] { dictator_target result{}; dictator_host_target(host, &result); return result; };
    CHECK(until([&] { return snapshot().eligible != 0; }));
    const auto initial_identity = snapshot();
    CHECK(initial_identity.process_id == test.child.dwProcessId && initial_identity.token != 0);
    CHECK(capture(widget, L"ready", true, true));
    RECT default_position{}; GetWindowRect(widget, &default_position);
    MONITORINFO monitor{sizeof(monitor)};
    CHECK(GetMonitorInfoW(MonitorFromWindow(widget, MONITOR_DEFAULTTONEAREST), &monitor));
    const int default_height = default_position.bottom - default_position.top;
    const int extra_clearance = static_cast<int>(24 * GetDpiForWindow(widget) / 96);
    CHECK(default_position.bottom <= monitor.rcWork.bottom - default_height / 2 - extra_clearance);
    const uint16_t binding[] = {'C','t','r','l','-','A','l','t','-','F','8',0};
    CHECK(dictator_host_bind_hotkey(host, 3, VK_F8, 0, binding) == DICTATOR_OK);
    dictator_host_poll_events(host);
    CHECK(until([&] { return snapshot().token != 0 && snapshot().token != initial_identity.token; }));
    const auto identity = snapshot();
    CHECK(RegisterHotKey(nullptr, 77, MOD_CONTROL | MOD_SHIFT, VK_F9));
    CHECK(dictator_host_bind_hotkey(host, 6, VK_F9, 0, binding) == DICTATOR_PLATFORM_ERROR);
    UnregisterHotKey(nullptr, 77);
    key(VK_CONTROL); key(VK_MENU); key(VK_F8);
    dictator_input pressed{};
    CHECK(until([&] { return dictator_host_input(host, &pressed) != 0; }));
    CHECK(pressed.down == 1 && pressed.source == 1 && pressed.target == identity.token);
    key(VK_F8); // Repeat does not generate another press.
    dictator_input unexpected{};
    CHECK(dictator_host_input(host, &unexpected) == 0);
    CHECK(dictator_host_preview(host, identity.token) == DICTATOR_OK);
    CHECK(capture(widget, L"talking", false, true));
    const auto held_until = GetTickCount64() + 550;
    CHECK(until([&] { return GetTickCount64() >= held_until; }, 1000));
    key(VK_MENU, true); key(VK_CONTROL, true); key(VK_F8, true);
    dictator_input released{};
    CHECK(until([&] { return dictator_host_input(host, &released) != 0; }));
    CHECK(released.down == 0 && released.source == 1 && released.timestamp - pressed.timestamp >= 500);
    CHECK(GetForegroundWindow() == target && SendMessageW(target, WM_APP + 10, 0, 0) == 1);
    key(VK_ESCAPE); key(VK_ESCAPE, true); key(VK_F6); key(VK_F6, true);
    CHECK(until([&] { return SendMessageW(target, WM_APP + 11, 0, 0) == 1 && SendMessageW(target, WM_APP + 12, 0, 0) == 1; }));
    SendMessageW(target, WM_APP + 2, 0, 0);
    CHECK(until([&] { return snapshot().eligible == 0; }));
    CHECK(capture(widget, L"no-cursor", true, false));
    key(VK_CONTROL); key(VK_MENU); key(VK_F8); key(VK_F8, true); key(VK_MENU, true); key(VK_CONTROL, true);
    CHECK(until([&] { return dictator_host_input(host, &pressed) != 0; }));
    CHECK(pressed.target == 0);
    CHECK(until([&] { return dictator_host_input(host, &released) != 0; }));
    SendMessageW(target, WM_APP + 3, 0, 0);
    CHECK(until([&] { return snapshot().eligible == 0; }));
    SendMessageW(target, WM_APP + 4, 0, 0);
    CHECK(snapshot().eligible == 0);
    SendMessageW(widget, WM_CLOSE, 0, 0);
    CHECK(!IsWindowVisible(widget));
    key(VK_CONTROL); key(VK_MENU); key(VK_F8); key(VK_F8, true); key(VK_MENU, true); key(VK_CONTROL, true);
    CHECK(until([&] { return IsWindowVisible(widget) != FALSE; }));
    CHECK(GetForegroundWindow() == target);
    while (dictator_host_input(host, &unexpected)) { }
    // Default backslash follows the focused application's UK or US layout.
    const uint16_t slash[] = {'C','t','r','l','-','A','l','t','-','\\',0};
    for (WPARAM uk : {WPARAM{0}, WPARAM{1}}) {
        SendMessageW(target, WM_APP + 5, uk, 0);
        SendMessageW(target, WM_APP + 1, 0, 0);
        CHECK(until([&] { return snapshot().eligible != 0; }));
        CHECK(dictator_host_bind_hotkey(host, 3, 0xDC, 1, slash) == DICTATOR_OK);
        const auto layout = GetKeyboardLayout(GetWindowThreadProcessId(target, nullptr));
        const auto slash_key = LOBYTE(VkKeyScanExW(L'\\', layout));
        key(VK_CONTROL); key(VK_MENU); key(slash_key); key(slash_key, true); key(VK_MENU, true); key(VK_CONTROL, true);
        CHECK(until([&] { return dictator_host_input(host, &pressed) != 0; }));
        CHECK(pressed.down == 1 && pressed.target != 0);
        CHECK(until([&] { return dictator_host_input(host, &released) != 0; }));
        CHECK(released.down == 0);
    }
    // UK OEM5 (#) can be captured literally rather than aliasing the default slash.
    const uint16_t literal[] = {'C','t','r','l','-','A','l','t','-','K','e','y',' ','0','x','D','C',0};
    CHECK(dictator_host_bind_hotkey(host, 3, 0xDC, 0, literal) == DICTATOR_OK);
    key(VK_CONTROL); key(VK_MENU); key(0xDC); key(0xDC, true); key(VK_MENU, true); key(VK_CONTROL, true);
    CHECK(until([&] { return dictator_host_input(host, &pressed) != 0; }));
    CHECK(pressed.down == 1 && pressed.target != 0);
    CHECK(until([&] { return dictator_host_input(host, &released) != 0; }));
    CHECK(released.down == 0);
    CHECK(dictator_host_bind_hotkey(host, 3, 0xDC, 1, slash) == DICTATOR_OK);
    // Drag via the body, zoom, then close/reopen: focus and session position survive.
    RECT original{}; GetWindowRect(widget, &original);
    POINT cursor{}; GetCursorPos(&cursor);
    SetCursorPos(original.left + 15, original.top + 15);
    SendMessageW(widget, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(15, 15));
    SetCursorPos(original.left + 85, original.top - 55);
    SendMessageW(widget, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(15, 15));
    SendMessageW(widget, WM_LBUTTONUP, 0, MAKELPARAM(15, 15));
    SetCursorPos(cursor.x, cursor.y);
    RECT dragged{}; GetWindowRect(widget, &dragged);
    CHECK(dragged.left != original.left || dragged.top != original.top);
    {
        // Observe actual applied positions, not just the final rectangle: a move
        // outside the work area followed by a corrective move is still a failure.
        position_observer observer(widget);
        auto bounds = monitor.rcWork;
        APPBARDATA taskbar{sizeof(taskbar)};
        if (SHAppBarMessage(ABM_GETTASKBARPOS, &taskbar) && taskbar.uEdge == ABE_BOTTOM &&
            MonitorFromRect(&taskbar.rc, MONITOR_DEFAULTTONEAREST) == MonitorFromWindow(widget, MONITOR_DEFAULTTONEAREST))
            bounds.bottom = std::min(bounds.bottom, monitor.rcMonitor.bottom - (taskbar.rc.bottom - taskbar.rc.top));
        const LONG mid_x = (monitor.rcMonitor.left + monitor.rcMonitor.right) / 2;
        const LONG mid_y = (monitor.rcMonitor.top + monitor.rcMonitor.bottom) / 2;
        const POINT edges[] = {{monitor.rcMonitor.left, mid_y}, {monitor.rcMonitor.right - 1, mid_y},
            {mid_x, monitor.rcMonitor.top}, {mid_x, monitor.rcMonitor.bottom - 1}};
        for (double zoom : {.750, .866, 1., 1.155, 1.333}) {
            CHECK(dictator_host_set_widget(host, 1, zoom, 0) == DICTATOR_OK);
            for (const auto& edge : edges) {
                RECT start{}; GetWindowRect(widget, &start);
                const int grab_x = (start.right - start.left) / 2;
                const auto grab = MAKELPARAM(grab_x, 10); // Transcript/body drag surface.
                CHECK(SetCursorPos(start.left + grab_x, start.top + 10));
                SendMessageW(widget, WM_LBUTTONDOWN, MK_LBUTTON, grab);
                CHECK(SetCursorPos(edge.x, edge.y));
                GetWindowRect(widget, &previous_position);
                applied_positions.clear(); observe_moves = true;
                SendMessageW(widget, WM_MOUSEMOVE, MK_LBUTTON, grab);
                observe_moves = false;
                SendMessageW(widget, WM_LBUTTONUP, 0, grab);
                CHECK(applied_positions.size() == 1);
                const auto& applied = applied_positions.front();
                CHECK(applied.left >= bounds.left && applied.top >= bounds.top &&
                    applied.right <= bounds.right && applied.bottom <= bounds.bottom);
                CHECK(GetForegroundWindow() == target && SendMessageW(target, WM_APP + 10, 0, 0) == 1);
            }
        }
    }
    GetWindowRect(widget, &dragged);
    SendMessageW(widget, WM_CLOSE, 0, 0);
    CHECK(dictator_host_set_widget(host, 1, 1.333, 0) == DICTATOR_OK);
    RECT reopened{}; GetWindowRect(widget, &reopened);
    CHECK(reopened.left == dragged.left && reopened.top == dragged.top);
    CHECK(GetForegroundWindow() == target && SendMessageW(target, WM_APP + 10, 0, 0) == 1);
    // One-second region delay, exact contextual copy and live eligibility updates.
    const auto tooltip = FindWindowW(L"Dictator.Tooltip.2", nullptr);
    CHECK(tooltip && (GetWindowLongPtrW(tooltip, GWL_EXSTYLE) & WS_EX_NOACTIVATE));
    SetCursorPos(reopened.left + 10, reopened.top + 10);
    SendMessageW(widget, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
    const auto early = GetTickCount64() + 900;
    CHECK(until([&] { return GetTickCount64() >= early; }, 1500));
    CHECK(!IsWindowVisible(tooltip));
    CHECK(until([&] { return IsWindowVisible(tooltip) != FALSE; }));
    wchar_t copy[512]{}; GetWindowTextW(tooltip, copy, 512);
    CHECK(wcscmp(copy, L"Ready to Insert Text\n────────────────\nHold to Drag Widget") == 0);
    SendMessageW(target, WM_APP + 2, 0, 0);
    CHECK(until([&] {
        GetWindowTextW(tooltip, copy, 512);
        return wcscmp(copy, L"No Text Insertion Cursor\n────────────────\nHold to Drag Widget") == 0;
    }));
    CHECK(IsWindowVisible(tooltip) && GetForegroundWindow() == target);
    SendMessageW(target, WM_APP + 1, 0, 0);
    CHECK(until([&] { return snapshot().eligible != 0; }));
    // Microphone edges remain source-specific and never activate the Widget.
    RECT client{}; GetClientRect(widget, &client);
    const auto point = MAKELPARAM(client.right * 25 / 270, client.bottom * 37 / 56);
    SetCursorPos(reopened.left + GET_X_LPARAM(point), reopened.top + GET_Y_LPARAM(point));
    SendMessageW(widget, WM_MOUSEMOVE, 0, point);
    CHECK(!IsWindowVisible(tooltip));
    CHECK(until([&] { return IsWindowVisible(tooltip) != FALSE; }));
    GetWindowTextW(tooltip, copy, 512);
    CHECK(wcscmp(copy, L"Click to Start Talking\nHold to Talk - Release to Stop\n────────────────\nHotkey: Ctrl-Alt-\\") == 0);
    CHECK(dictator_host_preview(host, snapshot().token) == DICTATOR_OK);
    CHECK(until([&] {
        GetWindowTextW(tooltip, copy, 512);
        return wcscmp(copy, L"Click to Stop Talking\n────────────────\nHotkey: Ctrl-Alt-\\") == 0;
    }));
    CHECK(dictator_host_preview(host, 0) == DICTATOR_OK);
    SendMessageW(widget, WM_LBUTTONDOWN, MK_LBUTTON, point);
    CHECK(until([&] { return dictator_host_input(host, &pressed) != 0; }));
    CHECK(pressed.source == 2 && pressed.down == 1 && pressed.target != 0);
    SendMessageW(widget, WM_LBUTTONUP, 0, point);
    CHECK(until([&] { return dictator_host_input(host, &released) != 0; }));
    CHECK(released.source == 2 && released.down == 0);
    SendMessageW(widget, WM_LBUTTONDOWN, MK_LBUTTON, point);
    CHECK(until([&] { return dictator_host_input(host, &pressed) != 0; }));
    CHECK(pressed.source == 2 && pressed.down == 1);
    CHECK(ReleaseCapture());
    CHECK(until([&] { return dictator_host_input(host, &released) != 0; }));
    CHECK(released.source == 2 && released.down == 2);
    const auto tools_point = MAKELPARAM(client.right * 229 / 270, client.bottom * 37 / 56);
    dictator_host_poll_events(host);
    SendMessageW(widget, WM_LBUTTONDOWN, MK_LBUTTON, tools_point);
    SendMessageW(widget, WM_LBUTTONUP, 0, tools_point);
    CHECK(dictator_host_poll_events(host) == 2);
    const auto close_point = MAKELPARAM(client.right * 253 / 270, client.bottom * 37 / 56);
    SendMessageW(widget, WM_LBUTTONDOWN, MK_LBUTTON, close_point);
    SendMessageW(widget, WM_LBUTTONUP, 0, close_point);
    CHECK(!IsWindowVisible(widget) && dictator_host_poll_events(host) == 32);
    CHECK(GetForegroundWindow() == target);
    CHECK(SendMessageW(widget, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
    SetCursorPos(cursor.x, cursor.y);
    std::cout << "Editable/read-only/disabled/non-text metadata; real global input, UK/US backslash, repeat, modifier release, Escape passthrough, unchanged target text/focus, recovery, drag without transient out-of-bounds moves at every zoom, tooltip delay/copy/live update and microphone edges passed.\n";
}
