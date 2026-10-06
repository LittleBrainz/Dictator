#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "target_probe.h"
#include <objbase.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <UIAutomation.h>
#include <chrono>
#include <string>
#include <iterator>

using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT target_message = WM_APP + 18;
struct identity {
    HWND foreground{}, focus{};
    DWORD process{};
    std::wstring application;
    uint64_t element{};
    HMONITOR monitor{};
    bool operator==(const identity&) const = default;
};
bool cached_bool(IUIAutomationElement* element, PROPERTYID id, bool& output) noexcept {
    VARIANT value{};
    if (FAILED(element->GetCachedPropertyValue(id, &value))) return false;
    const bool valid = value.vt == VT_BOOL;
    output = valid && value.boolVal != VARIANT_FALSE;
    VariantClear(&value);
    return valid;
}
uint64_t runtime_identity(IUIAutomationElement* element) noexcept {
    SAFEARRAY* array{};
    if (FAILED(element->GetRuntimeId(&array)) || !array) return 0;
    LONG first{}, last{};
    uint64_t hash = 1469598103934665603ull;
    if (FAILED(SafeArrayGetLBound(array, 1, &first)) || FAILED(SafeArrayGetUBound(array, 1, &last))) hash = 0;
    else for (LONG index = first; index <= last; ++index) {
        int value{};
        if (FAILED(SafeArrayGetElement(array, &index, &value))) { hash = 0; break; }
        hash = (hash ^ static_cast<uint32_t>(value)) * 1099511628211ull;
    }
    SafeArrayDestroy(array);
    return hash;
}
bool read_target(IUIAutomation* automation, IUIAutomationCacheRequest* cache, identity& result) {
    result.foreground = GetForegroundWindow();
    if (!result.foreground) return false;
    const auto thread = GetWindowThreadProcessId(result.foreground, &result.process);
    if (!thread) return false;
    GUITHREADINFO gui{sizeof(gui)};
    if (!GetGUIThreadInfo(thread, &gui) || !gui.hwndFocus) return false;
    result.focus = gui.hwndFocus;
    // Dictator Settings deliberately activates; it is never a dictation target.
    if (result.process == GetCurrentProcessId()) return false;
    ComPtr<IUIAutomationElement> element;
    if (!automation || FAILED(automation->GetFocusedElementBuildCache(cache, &element)) || !element) return false;
    bool enabled{}, focused{}, value_available{}, readonly{true}, text_available{};
    if (!cached_bool(element.Get(), UIA_IsEnabledPropertyId, enabled) || !enabled ||
        !cached_bool(element.Get(), UIA_HasKeyboardFocusPropertyId, focused) || !focused) return false;
    int process{};
    if (FAILED(element->get_CachedProcessId(&process)) || static_cast<DWORD>(process) != result.process) return false;
    cached_bool(element.Get(), UIA_IsValuePatternAvailablePropertyId, value_available);
    const bool readonly_known = cached_bool(element.Get(), UIA_ValueIsReadOnlyPropertyId, readonly);
    bool editable = value_available && readonly_known && !readonly;
    // Browser contenteditable and rich document controls can expose TextPattern2
    // without ValuePattern. GetCaretRange examines insertion metadata, never text.
    cached_bool(element.Get(), UIA_IsTextPattern2AvailablePropertyId, text_available);
    if (!editable && text_available) {
        ComPtr<IUIAutomationTextPattern2> text;
        BOOL active{};
        ComPtr<IUIAutomationTextRange> range;
        if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text))) && text &&
            SUCCEEDED(text->GetCaretRange(&active, &range)) && active && range) {
            VARIANT attr{};
            if (SUCCEEDED(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &attr))) {
                editable = attr.vt == VT_BOOL && attr.boolVal == VARIANT_FALSE;
                VariantClear(&attr);
            }
        }
    }
    if (!editable) return false;
    result.element = runtime_identity(element.Get());
    if (!result.element || GetForegroundWindow() != result.foreground) return false;
    GUITHREADINFO after{sizeof(after)};
    if (!GetGUIThreadInfo(thread, &after) || after.hwndFocus != result.focus) return false;
    result.monitor = MonitorFromWindow(result.foreground, MONITOR_DEFAULTTONEAREST);
    HANDLE process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, result.process);
    if (!process_handle) return false;
    wchar_t executable[32768]{};
    DWORD length = static_cast<DWORD>(std::size(executable));
    const bool success = QueryFullProcessImageNameW(process_handle, 0, executable, &length) != FALSE;
    CloseHandle(process_handle);
    if (!success) return false;
    result.application.assign(executable, length);
    return true;
}
}

target_probe::target_probe(HWND notification) : notification_(notification), worker_([this] { run(); }) {}
target_probe::~target_probe() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    wake_.notify_one();
    worker_.join();
}
void target_probe::input(uint32_t source, uint32_t down, uint64_t timestamp) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (requests_.size() >= 64) { ++epoch_; requests_.clear(); inputs_.clear(); target_ = {}; }
        else requests_.push_back({timestamp, 0, source, down});
        wake_.notify_one();
    } catch (...) { /* fail closed; no exception crosses the input hook */ }
}
dictator_target target_probe::snapshot() noexcept { std::lock_guard lock(mutex_); return target_; }
bool target_probe::pop(dictator_input& output) noexcept {
    std::lock_guard lock(mutex_);
    if (inputs_.empty()) return false;
    output = inputs_.front(); inputs_.pop_front(); return true;
}
void target_probe::cancel_inputs() noexcept {
    std::lock_guard lock(mutex_); ++epoch_; requests_.clear(); inputs_.clear();
}
void target_probe::run() noexcept {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        ComPtr<IUIAutomation> automation;
        ComPtr<IUIAutomationCacheRequest> cache;
        CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
        if (automation) {
            ComPtr<IUIAutomation2> automation2;
            if (SUCCEEDED(automation.As(&automation2))) {
                automation2->put_ConnectionTimeout(200);
                automation2->put_TransactionTimeout(200);
            }
            automation->CreateCacheRequest(&cache);
            if (cache) {
                cache->AddProperty(UIA_ProcessIdPropertyId);
                cache->AddProperty(UIA_IsEnabledPropertyId);
                cache->AddProperty(UIA_HasKeyboardFocusPropertyId);
                cache->AddProperty(UIA_IsValuePatternAvailablePropertyId);
                cache->AddProperty(UIA_ValueIsReadOnlyPropertyId);
                cache->AddProperty(UIA_IsTextPattern2AvailablePropertyId);
            }
        }
        identity previous{};
        uint64_t generation{};
        while (true) {
            dictator_input input{};
            bool has_input{};
            uint64_t epoch{};
            {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(60), [this] { return stopping_ || !requests_.empty(); });
                if (stopping_) break;
                epoch = epoch_;
                if (!requests_.empty()) { input = requests_.front(); requests_.pop_front(); has_input = true; }
            }
            identity current{};
            const bool eligible = cache && read_target(automation.Get(), cache.Get(), current);
            if (current != previous) { ++generation; previous = current; }
            dictator_target snapshot{eligible ? generation : 0, reinterpret_cast<uintptr_t>(current.foreground),
                reinterpret_cast<uintptr_t>(current.focus), GetTickCount64(), current.process, eligible ? 1u : 0u};
            {
                std::lock_guard lock(mutex_);
                target_ = snapshot;
                if (has_input && epoch == epoch_) {
                    input.target = snapshot.token;
                    if (inputs_.size() >= 64) { inputs_.clear(); target_ = {}; }
                    else inputs_.push_back(input);
                }
            }
            PostMessageW(notification_, target_message, 0, 0);
        }
    } catch (...) {
        std::lock_guard lock(mutex_); target_ = {}; inputs_.clear();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
}
