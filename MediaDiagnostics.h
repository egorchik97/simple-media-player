#pragma once
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <string>
#include <cstdio>

namespace MediaDiagnostics {
// Stage must be a static diagnostic label, never media/user data.
inline void Log(const char* stage, HRESULT hr = S_OK, int sessionCount = -1) noexcept {
    try {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        wchar_t path[32768]{};
        DWORD n = GetModuleFileNameW(nullptr, path, 32768);
        if (!n || n >= 32768) return;
        std::wstring file(path, n);
        auto slash = file.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return;
        file.resize(slash + 1);
        file += L"MediaPopup-" + std::to_wstring(GetCurrentProcessId()) + L".log";
        HANDLE h = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        char line[256]{};
        int size = snprintf(line, sizeof(line), "tick=%llu tid=%lu stage=%.100s sessions=%d HRESULT=0x%08lX\r\n",
            static_cast<unsigned long long>(GetTickCount64()), GetCurrentThreadId(), stage,
            sessionCount, static_cast<unsigned long>(hr));
        DWORD written;
        if (size > 0 && size < static_cast<int>(sizeof(line))) WriteFile(h, line, size, &written, nullptr);
        CloseHandle(h);
    } catch (...) {} // Diagnostics must not terminate a worker.
}

// Use on an explicitly initialized MTA: polling needs no message pump.
// No completion handlers, .get(), detached waiter threads, or GetResults on failure.
// Timeout/cancellation throws; the caller decides whether retry is safe.
template<class Operation>
auto Wait(const Operation& operation, std::chrono::milliseconds timeout,
          const std::atomic<bool>& stop, const char* stage) -> decltype(operation.GetResults()) {
    using winrt::Windows::Foundation::AsyncStatus;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    Log(stage);
    try {
        for (;;) {
            auto status = operation.Status();
            if (status == AsyncStatus::Completed) {
                // All current callers use result-bearing IAsyncOperation<T>.
                auto result = operation.GetResults();
                Log(stage, S_OK);
                return result;
            }
            if (status == AsyncStatus::Error) winrt::throw_hresult(operation.ErrorCode());
            if (status == AsyncStatus::Canceled) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_CANCELLED));
            if (stop.load() || std::chrono::steady_clock::now() >= deadline) {
                const HRESULT reason = HRESULT_FROM_WIN32(stop.load() ? ERROR_CANCELLED : ERROR_TIMEOUT);
                try { operation.Cancel(); } catch (...) { Log("async.cancel", winrt::to_hresult()); }
                winrt::throw_hresult(reason);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    } catch (...) {
        Log(stage, winrt::to_hresult());
        throw;
    }
}
} // namespace MediaDiagnostics
