#include "ErrorHandle.h"

#include <cstdlib>
#include <QMessageBox>
#include <QDesktopServices>
#include <Windows.h>

#include <Config.h>
#include "Utils.h"

[[noreturn]] void FatalError(const QString &content, bool report)
{
#if !defined APD_OS_WIN
    #error "Need to port."
#endif

    // Because QMessageBox does not allow calls from non-GUI threads.
    // So far, there seems to be no better way than using native APIs.
    //

    QString title = QMessageBox::tr("%1 fatal error").arg(Config::ProgramName);

    if (report) {
        // clang-format off

        QString message = QMessageBox::tr(
            "An error has occurred!\n"
            "Please help us fix this problem.\n"
            "--------------------------------------------------\n"
            "\n"
            "%1\n"
            "\n"
            "--------------------------------------------------\n"
            "Click \"Yes\" will pop up GitHub issue tracker page.\n"
            "You can submit this information to us there.\n"
            "Thank you very much."
        ).arg(content);

        // clang-format on

        int button = MessageBoxW(
            nullptr, message.toStdWString().c_str(), title.toStdWString().c_str(),
            MB_ICONERROR | MB_YESNO);

#if !defined APD_DEBUG
        if (button == IDYES) {
            QDesktopServices::openUrl(QUrl{Config::UrlIssues});
        }
#endif
    }
    else {
        MessageBoxW(
            nullptr, content.toStdWString().c_str(), title.toStdWString().c_str(),
            MB_ICONERROR | MB_OK);
    }

#if defined APD_DEBUG
    Utils::Debug::BreakPoint();
#endif

    std::abort();
}
