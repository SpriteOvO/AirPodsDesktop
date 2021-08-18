#include "ErrorHandle.h"

#include <format>
#include <cstdlib>

#include <QMessageBox>
#include <QDesktopServices>
#include <Windows.h>

#include <Config.h>
#include "Utils.h"

[[noreturn]] void FatalError(const std::string &content, bool report)
{
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
