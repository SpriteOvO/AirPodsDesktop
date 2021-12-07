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

#include <mutex>
#include <format>
#include <vector>
#include <cwctype>
#include <functional>

#include <QDir>
#include <QTimer>
#include <QWidget>
#include <QBitmap>
#include <QPainter>
#include <QKeyEvent>
#include <QApplication>
#include <QStandardPaths>

#include "Helper.h"
#include "Logger.h"
#include "Error.h"

#if defined APD_OS_WIN
    #include "Core/OS/Windows.h"
#endif

namespace Utils {
namespace Qt {

#define UTILS_QT_DISABLE_ESC_QUIT(base_name)                                                       \
    inline void keyPressEvent(QKeyEvent *event) override                                           \
    {                                                                                              \
        /* Prevent users from closing window by pressing ESC */                                    \
        if (event->key() == Qt::Key_Escape) {                                                      \
            event->accept();                                                                       \
        }                                                                                          \
        else {                                                                                     \
            base_name::keyPressEvent(event);                                                       \
        }                                                                                          \
    }

inline void SetRoundedCorners(QWidget *widget, qreal radius)
{
    QBitmap bmp{widget->size()};
    QPainter painter{&bmp};
    bmp.clear();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(::Qt::black);
    painter.setBrush(::Qt::black);
    painter.drawRoundedRect(widget->geometry(), radius, radius, ::Qt::AbsoluteSize);
    widget->setMask(bmp);
}

inline void SetPaletteColor(QWidget *widget, QPalette::ColorRole colorRole, const QColor &color)
{
    auto palette = widget->palette();
    palette.setColor(colorRole, color);
    widget->setPalette(palette);
}

inline void Dispatch(std::function<void()> callback)
{
    QTimer *timer = new QTimer;
    timer->moveToThread(qApp->thread());
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [timer, callback = std::move(callback)]() {
        callback();
        timer->deleteLater();
    });
    QMetaObject::invokeMethod(timer, "start", ::Qt::QueuedConnection, Q_ARG(int, 0));
}
} // namespace Qt

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
        result.mkdir(".");
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
} // namespace Process
} // namespace Utils
