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
#include <QDesktopServices>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/pattern_formatter.h>

#include <Config.h>
#include "Helper.h"
#include "Utils.h"
#include "Application.h"

#include "Core/OS/Windows.h"


namespace Logger
{
    void DoError(const QString &content, bool report)
    {
#if !defined APD_OS_WIN
#   error "Need to port."
#endif

        // Because QMessageBox does not allow calls from non-GUI threads.
        // So far, there seems to be no better way than using native APIs.
        //

        QString title = QMessageBox::tr("%1 fatal error").arg(Config::ProgramName);

        if (report)
        {
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

            int button = MessageBoxW(
                nullptr,
                message.toStdWString().c_str(),
                title.toStdWString().c_str(),
                MB_ICONERROR | MB_YESNO
            );

#if !defined APD_DEBUG
            if (button == IDYES) {
                QDesktopServices::openUrl(QUrl{Config::UrlIssues});
            }
#endif
        }
        else {
            MessageBoxW(
                nullptr,
                content.toStdWString().c_str(),
                title.toStdWString().c_str(),
                MB_ICONERROR | MB_OK
            );
        }

#if defined APD_DEBUG
        Utils::Debug::BreakPoint();
#endif

        QMetaObject::invokeMethod(qApp, &Application::quit, Qt::QueuedConnection);
    }

    void DoWarn(const QString &content)
    {
#if defined APD_DEBUG
        // MessageBoxW(
        //     nullptr,
        //     content.toStdWString().c_str(),
        //     QString{"%1 warning"}.arg(Config::ProgramName).toStdWString().c_str(),
        //     MB_ICONWARNING | MB_OK
        // );
#endif
    }

    namespace Details
    {
        template <class Mutex = std::mutex>
        class CustomFileSink : public spdlog::sinks::sink, Helper::NonCopyable
        {
        public:
            CustomFileSink(const spdlog::filename_t &filename) :
                _formatter{spdlog::details::make_unique<spdlog::pattern_formatter>()}
            {
                _fileHelper.open(filename, true);
            }

            ~CustomFileSink() override = default;

            void log(const spdlog::details::log_msg &message) final
            {
                std::lock_guard<Mutex> lock{_mutex};

                // SinkIt
                //
                spdlog::memory_buf_t formatted;
                _formatter->format(message, formatted);
                _fileHelper.write(formatted);

                PostHandler(message, formatted);
            }

            void flush() final
            {
                std::lock_guard<Mutex> lock{_mutex};
                Flush();
            }

            void set_pattern(const std::string &pattern) final
            {
                std::lock_guard<Mutex> lock{_mutex};
                SetPattern(pattern);
            }

            void set_formatter(std::unique_ptr<spdlog::formatter> sinkFormatter) final
            {
                std::lock_guard<Mutex> lock{_mutex};
                SetFormatter(std::move(sinkFormatter));
            }

        protected:
            std::unique_ptr<spdlog::formatter> _formatter;
            Mutex _mutex;
            spdlog::details::file_helper _fileHelper;

            void Flush()
            {
                _fileHelper.flush();
            }

            void SetPattern(const std::string &pattern)
            {
                SetFormatter(spdlog::details::make_unique<spdlog::pattern_formatter>(pattern));
            }

            void SetFormatter(std::unique_ptr<spdlog::formatter> sinkFormatter)
            {
                _formatter = std::move(sinkFormatter);
            }

            void PostHandler(
                const spdlog::details::log_msg &message,
                const spdlog::memory_buf_t &formatted
            )
            {
                std::string stdPayload{message.payload.begin(), message.payload.end()};

#if defined APD_DEBUG
                std::cout << std::string{formatted.begin(), formatted.end()};
#endif

                QString payload = QString::fromStdString(stdPayload);

                switch (message.level)
                {
                case spdlog::level::warn:
                    DoWarn(payload);
                    break;
                case spdlog::level::err:
                    DoError(payload, false);
                    break;
                case spdlog::level::critical:
                    DoError(payload, true);
                    break;
                }
            }
        };

    } // namespace Details

    bool Initialize(bool enableTrace)
    {
#if defined APD_DEBUG
        enableTrace = true;
#endif
        auto workspace = Utils::File::GetWorkspace();
        auto logFilePath = workspace.filePath(CONFIG_PROGRAM_NAME ".log");

        try {
            auto logger = std::make_shared<spdlog::logger>(
                "Main",
                std::initializer_list<spdlog::sink_ptr>{
                    std::make_shared<Details::CustomFileSink<>>(logFilePath.toStdString())
                }
            );
            spdlog::register_logger(logger);
            spdlog::set_default_logger(logger);

            spdlog::set_level(enableTrace ? spdlog::level::trace : spdlog::level::info);
            spdlog::flush_on(spdlog::level::trace);

#if defined APD_DEBUG
            spdlog::set_error_handler(
                [](const std::string &msg) {
                    Utils::Debug::BreakPoint();
                }
            );
#endif
            return true;
        }
        catch (spdlog::spdlog_ex &exception) {
            DoError(QString{"spdlog initialize failed.\n\n%1"}.arg(exception.what()), true);
            return false;
        }
    }

} // namespace Logger
