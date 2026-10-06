#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <new>
#include <stdexcept>
#include <iterator>
#include "audio_capture.h"
using Microsoft::WRL::ComPtr;

namespace {
class notifications final : public IMMNotificationClient {
    std::atomic<ULONG> references_{1};
    std::mutex callback_;
    dictator_audio* owner_;
    void notify(LPCWSTR id, bool unavailable, bool default_changed) noexcept {
        std::lock_guard lock(callback_);
        if (owner_) owner_->changed(id, unavailable, default_changed);
    }
public:
    explicit notifications(dictator_audio& owner) noexcept : owner_(&owner) {}
    // Unregister may leave queued COM notifications; sever their borrowed owner
    // while serializing with any callback already in progress.
    void detach() noexcept { std::lock_guard lock(callback_); owner_ = nullptr; }
    virtual ~notifications() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) {
            *output = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto left = --references_; if (!left) delete this; return left;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD state) override {
        notify(id, state != DEVICE_STATE_ACTIVE, false); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override { notify(id, false, false); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override { notify(id, true, false); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR id) override {
        if (flow == eCapture && role == eConsole) notify(id, false, true); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR id, const PROPERTYKEY) override {
        notify(id, false, false); return S_OK;
    }
};
uint32_t classify(HRESULT hr) noexcept {
    if (hr == E_NOTFOUND || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) return 1;
    if (hr == E_ACCESSDENIED) return 2;
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED) return 3;
    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) return 4;
    return 5;
}
}
static_assert(sizeof(dictator_audio_snapshot) == 64);
static_assert(sizeof(dictator_audio_device) == 1540);
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free);

dictator_audio::dictator_audio() {
    shutdown_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    control_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    packets_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    try {
        if (!shutdown_ || !control_ || !packets_) throw std::runtime_error("Audio events unavailable");
        worker_ = std::thread([this] { run(); });
    } catch (...) {
        if (shutdown_) CloseHandle(shutdown_); if (control_) CloseHandle(control_); if (packets_) CloseHandle(packets_);
        throw;
    }
}
dictator_audio::~dictator_audio() {
    SetEvent(shutdown_); if (worker_.joinable()) worker_.join();
    // Owner has joined its consumer; purge remaining transient audio before free.
    float discard[1024]{};
    while (buffer_.read(discard, 1024, buffer_.head())) { }
    SecureZeroMemory(discard, sizeof(discard));
    CloseHandle(packets_); CloseHandle(control_); CloseHandle(shutdown_);
}
void dictator_audio::start(const wchar_t* selected, uint64_t target, uintptr_t foreground, uintptr_t focus,
    const std::atomic<uint64_t>* eligibility, const std::atomic<uint64_t>* checked_at) noexcept {
    if (fatal_.load(std::memory_order_acquire)) return;
    { std::lock_guard lock(commands_); wcscpy_s(requested_, selected); requested_on_ = true; ++request_number_;
      requested_target_ = target; requested_foreground_ = foreground; requested_focus_ = focus; eligibility_ = eligibility; checked_at_ = checked_at; }
    state_.store(1, std::memory_order_release);
    // Initialization failure can race a first gesture; never overwrite its error.
    if (fatal_.load(std::memory_order_acquire)) state_.store(3, std::memory_order_release);
    SetEvent(control_);
}
void dictator_audio::stop() noexcept {
    { std::lock_guard lock(commands_); requested_on_ = false; ++request_number_; }
    SetEvent(control_);
}
void dictator_audio::changed(const wchar_t* id, bool unavailable, bool default_changed) noexcept {
    revision.fetch_add(1, std::memory_order_relaxed);
    { std::lock_guard lock(selection_);
      if (active_[0] && ((unavailable && id && wcscmp(id, active_) == 0) ||
          (default_changed && follow_default_ && (!id || wcscmp(id, active_) != 0))))
          removed_.store(true, std::memory_order_release);
    }
    SetEvent(control_);
}
void dictator_audio::snapshot(dictator_audio_snapshot& out) const noexcept {
    out = {session_.load(), revision.load(), dropped_.load(), frames_.load(), state_.load(std::memory_order_acquire),
        error_.load(), status_.load(), sample_rate_.load(), channels_.load(), peak_.load(), rms_.load(), buffer_.buffered()};
}
uint32_t dictator_audio::read(float* output, uint32_t capacity_count, uint64_t& session) noexcept {
    std::lock_guard lock(consumer_); // Only non-real-time consumers ever acquire this mutex.
    session = session_.load(std::memory_order_acquire);
    auto count = static_cast<uint32_t>(buffer_.read(output, capacity_count, valid_from_.load(std::memory_order_acquire)));
    if (state_.load(std::memory_order_acquire) != 2 || session != session_.load(std::memory_order_acquire)) {
        SecureZeroMemory(output, count * sizeof(float)); count = 0;
    }
    return count;
}
void dictator_audio::run() noexcept {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) { fatal_.store(true); status_.store(initialized); error_.store(5); state_.store(3); return; }
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        ComPtr<notifications> listener; listener.Attach(new(std::nothrow) notifications(*this));
        if (SUCCEEDED(hr) && listener) hr = enumerator->RegisterEndpointNotificationCallback(listener.Get());
        else if (SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
        if (FAILED(hr)) { fatal_.store(true); status_.store(hr); error_.store(classify(hr)); state_.store(3); }
        else {
            ComPtr<IMMDevice> device; ComPtr<IAudioClient> client; ComPtr<IAudioCaptureClient> capture;
            WAVEFORMATEX* format{}; HANDLE priority{};
            bool floating{}; unsigned bits{}; uint64_t applied{};
            uint64_t bound_target{}; uintptr_t bound_foreground{}, bound_focus{};
            const std::atomic<uint64_t>* eligibility{}; const std::atomic<uint64_t>* checked_at{};
            auto target_valid = [&] {
                GUITHREADINFO gui{sizeof(gui)};
                const auto foreground = GetForegroundWindow();
                const auto thread = GetWindowThreadProcessId(foreground, nullptr);
                return eligibility && checked_at && eligibility->load(std::memory_order_acquire) == bound_target &&
                    GetTickCount64() - checked_at->load(std::memory_order_acquire) <= 400 &&
                    reinterpret_cast<uintptr_t>(foreground) == bound_foreground && GetGUIThreadInfo(thread, &gui) &&
                    reinterpret_cast<uintptr_t>(gui.hwndFocus) == bound_focus;
            };
            auto release = [&] {
                if (client) client->Stop();
                if (priority) { AvRevertMmThreadCharacteristics(priority); priority = nullptr; }
                capture.Reset(); client.Reset(); device.Reset();
                if (format) { CoTaskMemFree(format); format = nullptr; }
                peak_.store(0); rms_.store(0); valid_from_.store(buffer_.head(), std::memory_order_release);
                session_.fetch_add(1, std::memory_order_release);
                { std::lock_guard lock(selection_); active_[0] = 0; }
            };
            auto fail = [&](HRESULT failure, uint32_t reason = 0) {
                release(); status_.store(failure); error_.store(reason ? reason : classify(failure)); state_.store(3, std::memory_order_release);
            };
            const HANDLE waits[] = {shutdown_, control_, packets_};
            for (;;) {
                const auto signaled = WaitForMultipleObjects(3, waits, FALSE, 20);
                if (signaled == WAIT_OBJECT_0) break;
                if (signaled == WAIT_FAILED) { fatal_.store(true); fail(HRESULT_FROM_WIN32(GetLastError())); break; }
                if (client && !target_valid()) {
                    release(); state_.store(0, std::memory_order_release);
                    // A control event may already have been consumed by this wait.
                    // Re-signal so a pending stop/new session cannot be lost.
                    SetEvent(control_); continue;
                }
                if (signaled == WAIT_OBJECT_0 + 1) {
                    wchar_t selected[512]{}; bool enabled{}; uint64_t number{};
                    { std::lock_guard lock(commands_); enabled = requested_on_; number = request_number_; wcscpy_s(selected, requested_);
                      bound_target = requested_target_; bound_foreground = requested_foreground_; bound_focus = requested_focus_; eligibility = eligibility_; checked_at = checked_at_; }
                    if (number != applied) {
                        applied = number; release(); removed_.store(false);
                        if (!enabled) { state_.store(0, std::memory_order_release); continue; }
                        state_.store(1, std::memory_order_release); error_.store(0); status_.store(0);
                        dropped_.store(0); frames_.store(0); sample_rate_.store(0); channels_.store(0);
                        hr = selected[0] ? enumerator->GetDevice(selected, &device) : enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
                        DWORD available{};
                        if (SUCCEEDED(hr)) hr = device->GetState(&available);
                        if (SUCCEEDED(hr) && available != DEVICE_STATE_ACTIVE) hr = AUDCLNT_E_DEVICE_INVALIDATED;
                        if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
                        if (SUCCEEDED(hr)) hr = client->GetMixFormat(&format);
                        if (SUCCEEDED(hr)) {
                            const auto tag = format->wFormatTag;
                            floating = tag == WAVE_FORMAT_IEEE_FLOAT;
                            bool pcm = tag == WAVE_FORMAT_PCM;
                            if (tag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
                                const auto extended = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);
                                floating = extended->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
                                pcm = extended->SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
                            }
                            bits = format->wBitsPerSample;
                            if ((!floating && !pcm) || (floating && bits != 32) ||
                                (pcm && bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
                                format->nChannels == 0 || format->nChannels > 32 || format->nSamplesPerSec < 8000 ||
                                format->nSamplesPerSec > 192000 || format->nBlockAlign != format->nChannels * (bits / 8))
                                hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
                        }
                        if (SUCCEEDED(hr)) hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, 1000000, 0, format, nullptr);
                        if (SUCCEEDED(hr)) hr = client->SetEventHandle(packets_);
                        if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
                        LPWSTR identity{};
                        if (SUCCEEDED(hr)) hr = device->GetId(&identity);
                        if (SUCCEEDED(hr)) {
                            if (wcslen(identity) >= 512) hr = E_INVALIDARG;
                            else { std::lock_guard lock(selection_); wcscpy_s(active_, identity); follow_default_ = !selected[0]; }
                        }
                        if (identity) CoTaskMemFree(identity);
                        // A default change during setup can precede publication of
                        // active_. Recheck after binding notifications to this ID.
                        if (SUCCEEDED(hr) && !selected[0]) {
                            ComPtr<IMMDevice> current_default; LPWSTR current_id{};
                            hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &current_default);
                            if (SUCCEEDED(hr)) hr = current_default->GetId(&current_id);
                            if (SUCCEEDED(hr)) {
                                std::lock_guard lock(selection_);
                                if (wcscmp(current_id, active_) != 0) hr = AUDCLNT_E_DEVICE_INVALIDATED;
                            }
                            if (current_id) CoTaskMemFree(current_id);
                        }
                        if (SUCCEEDED(hr) && removed_.load(std::memory_order_acquire)) hr = AUDCLNT_E_DEVICE_INVALIDATED;
                        DWORD task{};
                        if (SUCCEEDED(hr)) priority = AvSetMmThreadCharacteristicsW(L"Audio", &task);
                        bool cancelled{};
                        { std::lock_guard lock(commands_); cancelled = request_number_ != number || !requested_on_; }
                        if (cancelled || !target_valid()) { release(); state_.store(0, std::memory_order_release); SetEvent(control_); continue; }
                        if (SUCCEEDED(hr)) hr = client->Start();
                        if (FAILED(hr)) { fail(hr); continue; }
                        sample_rate_.store(format->nSamplesPerSec); channels_.store(format->nChannels);
                        state_.store(2, std::memory_order_release);
                    } else if (removed_.exchange(false) && client) fail(AUDCLNT_E_DEVICE_INVALIDATED, 3);
                    continue;
                }
                if (signaled != WAIT_OBJECT_0 + 2 || !capture) continue;
                // Real-time packet path: stack scratch, atomics and bounded writes.
                // No locks, managed calls, allocation, UI, network, disk or logging.
                for (int packet = 0; packet < 8 && capture; ++packet) {
                    UINT32 pending{}; hr = capture->GetNextPacketSize(&pending);
                    if (FAILED(hr)) { fail(hr); break; }
                    if (!pending) break;
                    BYTE* data{}; UINT32 count{}; DWORD flags{};
                    hr = capture->GetBuffer(&data, &count, &flags, nullptr, nullptr);
                    if (FAILED(hr)) { fail(hr); break; }
                    float scratch[1024]{}; double square{}; float peak{};
                    for (UINT32 offset = 0; offset < count; ) {
                        const auto block = std::min(UINT32{1024}, count - offset);
                        for (UINT32 i = 0; i < block; ++i) {
                            const auto value = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0 :
                                audio_data::mono(data + (offset + i) * format->nBlockAlign, format->nChannels, bits, floating);
                            scratch[i] = value; peak = std::max(peak, std::abs(value)); square += static_cast<double>(value) * value;
                        }
                        const auto accepted = buffer_.write(scratch, block);
                        dropped_.fetch_add(block - accepted, std::memory_order_relaxed); offset += block;
                    }
                    SecureZeroMemory(scratch, sizeof(scratch));
                    hr = capture->ReleaseBuffer(count);
                    if (FAILED(hr)) { fail(hr); break; }
                    frames_.fetch_add(count, std::memory_order_relaxed); peak_.store(peak);
                    rms_.store(count ? static_cast<float>(std::sqrt(square / count)) : 0);
                }
            }
            release(); if (!fatal_.load()) state_.store(0); enumerator->UnregisterEndpointNotificationCallback(listener.Get()); listener->detach();
        }
    }
    CoUninitialize();
}

dictator_result DICTATOR_CALL dictator_audio_devices(dictator_audio* audio, dictator_audio_device* output,
    uint32_t capacity_count, uint32_t* count) noexcept {
    if (!audio || !count || (!output && capacity_count) || capacity_count > 128) return DICTATOR_INVALID_ARGUMENT;
    *count = 0;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return DICTATOR_PLATFORM_ERROR;
    dictator_result result = DICTATOR_OK;
    {
        ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDeviceCollection> devices; ComPtr<IMMDevice> default_device;
        auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        LPWSTR default_id{};
        if (SUCCEEDED(hr) && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &default_device))) default_device->GetId(&default_id);
        if (SUCCEEDED(hr)) hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices);
        UINT total{}; if (SUCCEEDED(hr)) hr = devices->GetCount(&total);
        if (FAILED(hr)) result = DICTATOR_PLATFORM_ERROR;
        else if (total > 128) result = DICTATOR_BUFFER_TOO_SMALL;
        else {
            *count = total;
            if (capacity_count < total) result = DICTATOR_BUFFER_TOO_SMALL;
            else for (UINT i = 0; i < total; ++i) {
                ComPtr<IMMDevice> device; ComPtr<IPropertyStore> properties; LPWSTR id{}; PROPVARIANT name{};
                hr = devices->Item(i, &device);
                if (SUCCEEDED(hr)) hr = device->GetId(&id);
                if (SUCCEEDED(hr) && wcslen(id) >= 512) hr = E_INVALIDARG;
                if (SUCCEEDED(hr)) hr = device->OpenPropertyStore(STGM_READ, &properties);
                if (SUCCEEDED(hr)) hr = properties->GetValue(PKEY_Device_FriendlyName, &name);
                if (SUCCEEDED(hr)) {
                    auto& value = output[i]; value = {};
                    wcscpy_s(reinterpret_cast<wchar_t*>(value.id), 512, id);
                    wcsncpy_s(reinterpret_cast<wchar_t*>(value.name), 256,
                        name.vt == VT_LPWSTR && name.pwszVal ? name.pwszVal : L"Microphone", _TRUNCATE);
                    value.is_default = default_id && wcscmp(default_id, id) == 0 ? 1u : 0u;
                }
                PropVariantClear(&name); if (id) CoTaskMemFree(id);
                if (FAILED(hr)) { *count = 0; result = DICTATOR_PLATFORM_ERROR; break; }
            }
        }
        if (default_id) CoTaskMemFree(default_id);
    }
    if (SUCCEEDED(initialized)) CoUninitialize(); return result;
}
dictator_result DICTATOR_CALL dictator_audio_status(dictator_audio* audio, dictator_audio_snapshot* output) noexcept {
    if (!audio || !output) return DICTATOR_INVALID_ARGUMENT;
    audio->snapshot(*output); return DICTATOR_OK;
}
dictator_result DICTATOR_CALL dictator_audio_read(dictator_audio* audio, float* output, uint32_t capacity_count,
    uint32_t* count, uint64_t* session) noexcept {
    if (!audio || !output || !count || !session || !capacity_count || capacity_count > 8192) return DICTATOR_INVALID_ARGUMENT;
    *count = audio->read(output, capacity_count, *session); return DICTATOR_OK;
}
