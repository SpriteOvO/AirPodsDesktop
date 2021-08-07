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

#include "Logger.h"

#include <QUrl>
#include <QDir>
#include <QMessageBox>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/pattern_formatter.h>

#include <Config.h>
#include "Helper.h"
#include "Utils.h"
#include "Application.h"
#include "ErrorHandle.h"

#include "Core/OS/Windows.h"

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
        const auto logFilePath = GetLogFilePath().absolutePath().toStdString();

        // clang-format off
        auto logger = std::make_shared<spdlog::logger>(
            "Main", std::initializer_list<spdlog::sink_ptr>{
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath, 0x100000, 1)
#if defined APD_ENABLE_CONSOLE
                ,std::make_shared<spdlog::sinks::stdout_color_sink_mt>()
#endif
            }
        );
        // clang-format on

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
        FatalError(QString{"spdlog initialize failed.\n\n%1"}.arg(exception.what()), true);
        return false;
    }
}
} // namespace Logger
