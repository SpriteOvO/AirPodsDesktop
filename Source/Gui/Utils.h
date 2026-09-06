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

#include <functional>

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QTimer>
#include <QWidget>

#include "../Utils.h"

namespace Utils::Qt {

#define UTILS_QT_DISABLE_ESC_QUIT(base_name)                                                       \
    inline void keyPressEvent(QKeyEvent *event) override                                           \
    {                                                                                              \
        if (event->key() == Qt::Key_Escape) {                                                      \
            event->accept();                                                                       \
        }                                                                                          \
        else {                                                                                     \
            base_name::keyPressEvent(event);                                                       \
        }                                                                                          \
    }

#define UTILS_QT_REGISTER_LANGUAGECHANGE(base_name, callback)                                      \
    inline void changeEvent(QEvent *event) override                                                \
    {                                                                                              \
        if (event->type() == QEvent::LanguageChange) {                                             \
            callback();                                                                            \
        }                                                                                          \
        base_name::changeEvent(event);                                                             \
    }

inline void SetPaletteColor(QWidget *widget, QPalette::ColorRole colorRole, const QColor &color)
{
    auto palette = widget->palette();
    palette.setColor(colorRole, color);
    widget->setPalette(palette);
}

inline QColor InvertColor(const QColor &color)
{
    QColor result;
    result.setRgb(255 - color.red(), 255 - color.green(), 255 - color.blue());
    return result;
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

inline void QuitApplicationSafely()
{
    QMetaObject::invokeMethod(qApp, &QApplication::quit, ::Qt::QueuedConnection);
}

} // namespace Utils::Qt
