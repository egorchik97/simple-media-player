#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace media {

Logger::~Logger() {
    Flush();
    if (fileHandle_ != INVALID_HANDLE_VALUE && fileHandle_ != nullptr) {
        CloseHandle(fileHandle_);
    }
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

void Logger::SetFileEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    fileEnabled_ = enabled;
    if (enabled && fileHandle_ == INVALID_HANDLE_VALUE && !filePath_.empty()) {
        fileHandle_ = CreateFileW(
            filePath_.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    }
    if (!enabled && fileHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
    }
}

void Logger::SetFilePath(std::wstring path) {
    std::lock_guard<std::mutex> lock(mutex_);
    filePath_ = std::move(path);
    if (fileEnabled_ && fileHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
    }
    if (fileEnabled_ && !filePath_.empty()) {
        fileHandle_ = CreateFileW(
            filePath_.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    }
}

void Logger::Flush() {
    // No buffering; nothing to flush.
}

void Logger::Write(LogLevel level, std::string message) {
    std::scoped_lock lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(level_)) {
        return;
    }

    const char* levelStr = "UNK";
    switch (level) {
        case LogLevel::DEBUG:   levelStr = "DEBUG"; break;
        case LogLevel::INFO:    levelStr = "INFO"; break;
        case LogLevel::WARNING: levelStr = "WARNING"; break;
        case LogLevel::ERR:     levelStr = "ERROR"; break;
    }

    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &timeT);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    char timestamp[32];
    std::snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        static_cast<long long>(ms.count()));

    std::string line = "[" + std::string(timestamp) + "] [" + levelStr + "] " + message + "\n";

#ifdef _DEBUG
    OutputDebugStringA(line.c_str());
#endif

    if (fileEnabled_ && fileHandle_ != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(fileHandle_, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
}




} // namespace media
