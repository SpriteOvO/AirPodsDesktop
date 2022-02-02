//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#pragma once

#include <QDir>
#include <QString>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace Logger {

namespace Details {

enum class Level : uint32_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
};

template <Level level, class... Args>
inline void Log(const spdlog::source_loc &srcloc, Args &&...args)
{
    constexpr auto spdlogLevel = []() {
        if constexpr (level == Level::Trace) {
            return spdlog::level::trace;
        }
        else if constexpr (level == Level::Debug) {
            return spdlog::level::debug;
        }
        else if constexpr (level == Level::Info) {
            return spdlog::level::info;
        }
        else if constexpr (level == Level::Warn) {
            return spdlog::level::warn;
        }
        else if constexpr (level == Level::Error) {
            return spdlog::level::err;
        }
        else if constexpr (level == Level::Critical) {
            return spdlog::level::critical;
        }
        else {
            static_assert(false);
        }
    }();

    spdlog::default_logger_raw()->log(srcloc, spdlogLevel, std::forward<Args>(args)...);
}

} // namespace Details

bool Initialize(bool enableTrace);

QDir GetLogFilePath();

void CleanUpOldLogFiles();

} // namespace Logger

template <class OutStream>
inline OutStream &operator<<(OutStream &outStream, const QString &qstr)
{
    return outStream << qstr.toStdString().c_str();
}

#define LOG(level, ...)                                                                            \
    Logger::Details::Log<Logger::Details::Level::level>(                                           \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, __VA_ARGS__)
