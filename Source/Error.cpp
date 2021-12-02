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

#include "Error.h"

#include <format>
#include <cstdlib>
#include <fstream>

#include <QMessageBox>
#include <QDesktopServices>

#include <boost/stacktrace.hpp>

#include <Config.h>
#include "Utils.h"

constexpr auto kStackTraceFileName = "StackTrace.log";

namespace Error {
namespace Impl {

void WriteStackTraceFile()
{
    using namespace boost;

    auto workspace = Utils::File::GetWorkspace();
    std::ofstream file{workspace.absoluteFilePath(kStackTraceFileName).toStdString()};

    file << stacktrace::stacktrace();
}
} // namespace Impl

void Initialize()
{
    auto workspace = Utils::File::GetWorkspace();

    // Delete the last StackTrace log file, if any
    //
    workspace.remove(kStackTraceFileName);
}
} // namespace Error

[[noreturn]] void FatalError(const std::string &content, bool report)
{
    Error::Impl::WriteStackTraceFile();

#if !defined APD_OS_WIN
    #error "Need to port."
#endif

    // Because QMessageBox does not allow calls from non-GUI threads.
    // So far, there seems to be no better way than using native APIs.
    //

    auto title = std::format("{} fatal error", Config::ProgramName);

    if (report) {
        auto message = std::format(
            "An error has occurred!\n"
            "Please help us fix this problem.\n"
            "--------------------------------------------------\n"
            "\n"
            "{}\n"
            "\n"
            "--------------------------------------------------\n"
            "Click \"Yes\" will pop up GitHub issue tracker page.\n"
            "You can submit this information to us there.\n"
            "Thank you very much.",
            content);

        int button = MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_ICONERROR | MB_YESNO);

#if !defined APD_DEBUG
        if (button == IDYES) {
            QDesktopServices::openUrl(QUrl{Config::UrlIssues});
        }
#endif
    }
    else {
        MessageBoxA(nullptr, content.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
    }

#if defined APD_DEBUG
    Utils::Debug::BreakPoint();
#endif

    std::abort();
}
