#pragma once

#include <windows.h>
#include <mutex>
#include <string>
#include <string_view>
#include <cstdio>
#include <utility>

namespace media {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERR,
};

class Logger {
public:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void SetLevel(LogLevel level);
    void SetFileEnabled(bool enabled);
    void SetFilePath(std::wstring path);

    template<typename... Args>
    void Log(LogLevel level, std::string_view format, Args&&... args) {
        const std::string terminated(format);
        const int length = std::snprintf(nullptr, 0, terminated.c_str(), std::forward<Args>(args)...);
        if (length < 0) return;
        std::string buffer(static_cast<size_t>(length) + 1, '\0');
        std::snprintf(buffer.data(), buffer.size(), terminated.c_str(), std::forward<Args>(args)...);
        buffer.resize(static_cast<size_t>(length));
        Write(level, std::move(buffer));
    }

    void Flush();

private:
    void Write(LogLevel level, std::string message);

    LogLevel level_ = LogLevel::INFO;
    bool fileEnabled_ = false;
    std::wstring filePath_;
    std::mutex mutex_;
    HANDLE fileHandle_ = INVALID_HANDLE_VALUE;
};

} // namespace media
