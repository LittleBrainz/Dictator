#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::wstring windows_error(DWORD code) {
    wchar_t* buffer = nullptr;
    const auto count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = count ? std::wstring(buffer, count) : L"Windows error " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    return result;
}

std::filesystem::path executable_path() {
    std::vector<wchar_t> buffer(256);
    for (;;) {
        const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (count == 0) throw std::runtime_error("Cannot find the launcher executable path.");
        if (count < buffer.size()) return std::wstring(buffer.data(), count);
        if (buffer.size() >= 32768) throw std::runtime_error("The launcher path is too long.");
        buffer.resize(buffer.size() * 2);
    }
}

bool is_smoke_test() {
    int count = 0;
    auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) throw std::runtime_error("Cannot read launcher arguments.");
    const bool result = count > 1 && (std::wstring(arguments[1]) == L"--smoke-test" ||
        std::wstring(arguments[1]) == L"--ui-smoke-test");
    LocalFree(arguments);
    return result;
}

int report_error(const std::wstring& detail, bool headless) {
    const auto message = L"Dictator could not start.\n\n" + detail +
        L"\n\nExtract the entire Dictator artifact again, keeping lib next to Dictator.exe.";
    if (headless) std::fwprintf(stderr, L"%ls\n", message.c_str());
    else MessageBoxW(nullptr, message.c_str(), L"Dictator launch error", MB_OK | MB_ICONERROR);
    return 1;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command_line, int) {
    bool headless = false;
    try {
        headless = is_smoke_test();
        const auto host = executable_path().parent_path() / L"lib" / L"WinUI" / L"Dictator.App.exe";
        for (const auto* file : {L"Dictator.App.exe", L"Dictator.App.runtimeconfig.json", L"hostfxr.dll", L"coreclr.dll"}) {
            const auto dependency = host.parent_path() / file;
            const DWORD attributes = GetFileAttributesW(dependency.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
                return report_error(L"Required application file is missing: " + dependency.wstring(), headless);
        }

        // Forward the original argument tail verbatim, including quotes and Unicode.
        // An explicit absolute application name avoids cwd/PATH executable discovery.
        auto command = L"\"" + host.wstring() + L"\" " + command_line;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                nullptr, nullptr, &startup, &process)) {
            const auto error = GetLastError();
            return report_error(L"Windows could not launch " + host.wstring() + L":\n" + windows_error(error), headless);
        }
        CloseHandle(process.hThread);

        // Normal launches leave only the managed application running. The launcher
        // waits only in CI smoke mode so the actual application's exit code survives.
        DWORD exit_code = 0;
        if (headless) {
            if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
                !GetExitCodeProcess(process.hProcess, &exit_code)) {
                const auto error = GetLastError();
                CloseHandle(process.hProcess);
                return report_error(L"Cannot verify application exit:\n" + windows_error(error), true);
            }
        }
        CloseHandle(process.hProcess);
        return static_cast<int>(exit_code);
    }
    catch (const std::exception&) {
        return report_error(L"The launcher could not resolve or start its application under lib/WinUI.", headless);
    }
}
