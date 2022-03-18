//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
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

#include "Logger.h"

#include <QUrl>
#include <QDir>
#include <QMessageBox>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/pattern_formatter.h>

#include <Config.h>
#include "Helper.h"
#include "Utils.h"
#include "Application.h"
#include "Error.h"

#if defined APD_OS_WIN
    #include "Core/OS/Windows.h"
#endif

namespace Logger {

QDir GetLogFilePath()
{
    static std::optional<QDir> result;
    if (!result.has_value()) {
        const auto workspace = Utils::File::GetWorkspace();
        result = QDir{workspace.absoluteFilePath(CONFIG_PROGRAM_NAME ".log")};
    }
    return result.value();
}

bool Initialize(bool enableTrace)
{
#if defined APD_DEBUG
    enableTrace = true;
#endif

    try {
        const auto logFilePath = GetLogFilePath().absolutePath().toStdWString();

#if defined APD_OS_WIN
        // clang-format off
        auto logger = std::make_shared<spdlog::logger>(
            "Main", std::initializer_list<spdlog::sink_ptr>{
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true),
                std::make_shared<spdlog::sinks::stdout_color_sink_mt>()
            }
        );
        // clang-format on
#else
        Unimplemented();
        auto logger = std::make_shared<spdlog::logger>("Main");
#endif

        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);

        spdlog::set_level(enableTrace ? spdlog::level::trace : spdlog::level::info);
        spdlog::flush_on(spdlog::level::trace);

#if defined APD_DEBUG
        spdlog::set_error_handler([](const std::string &msg) { Utils::Debug::BreakPoint(); });
#endif
        return true;
    }
    catch (spdlog::spdlog_ex &exception) {
        FatalError(std::string{"spdlog initialize failed.\n\n"} + exception.what(), true);
        return false;
    }
}

// TODO: Remove this function in [v1.0.0]
void CleanUpOldLogFiles()
{
    auto workspace = Utils::File::GetWorkspace();

    for (size_t i = 1; i < 10000; ++i) {
        auto logFile = QString{CONFIG_PROGRAM_NAME ".%1.log"}.arg(i);

        if (!workspace.exists(logFile)) {
            LOG(Info, "Clean up old log file: '{}' doesn't exist, break the loop", logFile);
            break;
        }

        LOG(Info, "Clean up old log file: '{}' exists, remove it", logFile);
        workspace.remove(logFile);
    }
}
} // namespace Logger
