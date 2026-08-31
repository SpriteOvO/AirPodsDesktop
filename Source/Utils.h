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

#pragma once

#include <mutex>
#include <format>
#include <vector>
#include <cwctype>

#include <QDir>
#include <QLocale>
#include <QStandardPaths>

#include "Helper.h"
#include "Logger.h"
#include "Error.h"

#include <Config.h>

#if defined APD_OS_WIN
    #include "Core/OS/Windows.h"
#endif

namespace Utils {

inline const QVector<QLocale> &AvailableLocales()
{
    static const QVector<QLocale> locales = []() {
        const auto localeNames =
            QString{Config::TranslationLocales}.split(QStringLiteral(";"), ::Qt::SkipEmptyParts);

        QVector<QLocale> result = {QLocale{"en"}};

        for (const auto &localName : localeNames) {
            QLocale locale{localName};

            if (locale.language() == QLocale::C) {
                LOG(Warn, "Possibly invalid locale name '{}', ignore", localName);
                continue;
            }

            result.push_back(locale);
        }

        return result;
    }();

    return locales;
}

namespace Debug {

inline void BreakPoint()
{
#if defined APD_DEBUG
    #if !defined APD_MSVC
        #error "Need to port."
    #endif

    #if defined APD_MSVC
    __debugbreak();
    #endif
#else
    LOG(Warn, "Triggered a break point.");
#endif
}
} // namespace Debug

namespace Text {

[[nodiscard]] constexpr std::string ToLower(std::string source)
{
    std::transform(source.begin(), source.end(), source.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return source;
}

[[nodiscard]] constexpr std::wstring ToLower(std::wstring source)
{
    std::transform(source.begin(), source.end(), source.begin(), &std::towlower);
    return source;
}

[[nodiscard]] constexpr std::string ToUpper(std::string source)
{
    std::transform(source.begin(), source.end(), source.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return source;
}

[[nodiscard]] constexpr std::wstring ToUpper(std::wstring source)
{
    std::transform(source.begin(), source.end(), source.begin(), &std::towupper);
    return source;
}

} // namespace Text

namespace File {

inline QDir GetWorkspace()
{
    auto location = QStandardPaths::writableLocation(QStandardPaths::DataLocation);

    QDir result{std::move(location)};
    if (!result.exists()) {
        result.mkpath(".");
    }
    return result;
}

inline bool OpenFileLocation(const QDir &directory)
{
#if defined APD_OS_WIN
    return Core::OS::Windows::File::OpenFileLocation(directory);
#else
    #error "Need to port."
#endif
}
} // namespace File

namespace Process {
//
// Retained for backward compatibility with v0.2.0 and before.
// TODO: Remove this function in [v1.0.0]
//
inline bool SingleInstance(const QString &instanceName)
{
#if !defined APD_OS_WIN
    #error "Need to port."
#endif
    HANDLE mutex = CreateMutexW(
        nullptr, false, ("Global\\" + instanceName + "_InstanceMutex").toStdWString().c_str());
    uint32_t lastError = GetLastError();

    if (mutex == nullptr) {
        FatalError(std::format("Create instance mutex failed.\nErrorCode: {}", lastError), false);
    }

    // No need to close the handle
    //
    return lastError != ERROR_ALREADY_EXISTS;
}

inline void AttachConsole()
{
#if defined APD_OS_WIN
    Core::OS::Windows::Process::AttachConsole();
#else
    #error "Need to port."
#endif
}

} // namespace Process
} // namespace Utils
