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
#include <vector>
#include <functional>
#include <QDir>
#include <QTimer>
#include <QKeyEvent>
#include <QApplication>
#include <QPainterPath>
#include <QStandardPaths>

#include <spdlog/spdlog.h>

#include "Config.h"
#include "Helper.h"
#include "Logger.h"

#if defined APD_OS_WIN
    #include "Core/OS/Windows.h"
#endif

namespace Utils {
namespace Qt {

#define UTILS_QT_DISABLE_ESC_QUIT(class_name, base_name)                                           \
    inline void class_name::keyPressEvent(QKeyEvent *event) override                               \
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
    QPainterPath path;
    path.addRoundedRect(widget->rect(), radius, radius);
    widget->setMask(QRegion{path.toFillPolygon().toPolygon()});
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
    SPDLOG_WARN("Triggered a break point.");
#endif
}
} // namespace Debug

namespace Text {

template <class String, std::enable_if_t<Helper::is_string_v<String>, int> = 0>
inline String ToLower(const String &str)
{
    String result = str;
    std::transform(result.begin(), result.end(), result.begin(), std::tolower);
    return result;
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
} // namespace File

namespace Process {

inline bool SingleInstance(const QString &instanceName)
{
#if !defined APD_OS_WIN
    #error "Need to port."
#endif
    HANDLE mutex = CreateMutexW(
        nullptr, false, ("Global\\" + instanceName + "_InstanceMutex").toStdWString().c_str());
    uint32_t lastError = GetLastError();

    if (mutex == nullptr) {
        Logger::DoError(
            QString{"Create instance mutex failed.\nErrorCode: %1"}.arg(lastError), false);
    }

    // No need to close the handle
    //
    return lastError != ERROR_ALREADY_EXISTS;
}
} // namespace Process
} // namespace Utils
