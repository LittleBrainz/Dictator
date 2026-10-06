#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <thread>
#include "dictator_native.h"

#define CHECK(expression) do { if (!(expression)) { \
  std::cerr << "Failed at line " << __LINE__ << ": " #expression << '\n'; return 1; \
} } while (false)

void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    GdiFlush();
}

int main() {
    CHECK(dictator_host_create(1, nullptr) == DICTATOR_INVALID_ARGUMENT);
    dictator_host* host = nullptr;
    CHECK(dictator_host_create(2, &host) == DICTATOR_ABI_MISMATCH && host == nullptr);
    DWORD warm = 0;
    for (int cycle = 0; cycle < 10; ++cycle) {
        CHECK(dictator_host_create(1, &host) == DICTATOR_OK);
        CHECK(dictator_host_tray_ready(host) == 1);
        const auto widget = reinterpret_cast<HWND>(dictator_host_widget_handle(host));
        CHECK(widget && IsWindow(widget));
        CHECK((GetWindowLongPtrW(widget, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0);
        const auto foreground = GetForegroundWindow();
        CHECK(dictator_host_set_widget(host, 1, 1.0, 0) == DICTATOR_OK);
        CHECK(IsWindowVisible(widget) && GetForegroundWindow() == foreground);
        pump(); // Exercise real WM_PAINT and the caller-owned UI message pump.
        CHECK(SendMessageW(widget, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
        CHECK(dictator_host_set_widget(host, 1, 0.5, 0) == DICTATOR_INVALID_ARGUMENT);
        dictator_result worker_result = DICTATOR_OK;
        std::thread worker([&] { worker_result = dictator_host_set_widget(host, 0, 1.0, 0); });
        worker.join();
        CHECK(worker_result == DICTATOR_WRONG_THREAD && IsWindowVisible(widget));
        SendMessageW(widget, WM_CLOSE, 0, 0);
        CHECK(!IsWindowVisible(widget));
        CHECK(dictator_host_poll_events(host) == 32); // Explicit preview cancellation.
        const auto owner = GetWindow(widget, GW_OWNER);
        CHECK(owner && IsWindow(owner));
        SendMessageW(owner, WM_APP + 17, 0, MAKELPARAM(NIN_SELECT, 1));
        CHECK(dictator_host_poll_events(host) == 1);
        CHECK(dictator_host_poll_events(host) == 0);
        dictator_host_destroy(host);
        pump(); // Flush batched drawing/deletion before measuring ownership.
        CHECK(!IsWindow(widget) && !IsWindow(owner));
        const auto remaining = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        std::cout << "GDI objects after resident cycle " << cycle << ": " << remaining << '\n';
        // Drawing the first icon loads process-wide font/GDI caches. Subsequent
        // create/destroy cycles must not accumulate any additional GDI objects.
        if (cycle == 0) warm = remaining;
        else CHECK(remaining <= warm);
    }
    std::cout << "Resident native ownership, tray event, non-activation and UI-thread contract passed.\n";
}
