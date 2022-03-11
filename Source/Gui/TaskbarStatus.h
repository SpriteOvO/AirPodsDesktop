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

#include <optional>

#include <QDialog>

#include "ui_TaskbarStatus.h"

#include "../Core/AirPods.h"
#include "../Core/Settings.h"
#include "../Utils.h"
#include "Base.h"
#include "Widget/Battery.h"

struct TaskBarInfo {
    bool isHorizontal;
    HWND hShellTrayWnd, hReBarWindow32, hMSTaskSwWClass;
    QRect rectReBarWindow32, rectMSTaskSwWClass, rectMSTaskSwWClassForParent;
};

namespace Gui {

using namespace std::chrono_literals;

using Core::Settings::TaskbarStatusBehavior;

class MiniIcon : public QWidget
{
    Q_OBJECT

public:
    inline MiniIcon(QWidget *parent = nullptr)
    {
        setFixedSize(kSize, kSize);

        _textFont = font();
        _textFont.setPointSize(_textFont.pointSize() - 2);
    }

    inline void SetText(QString text)
    {
        _text = std::move(text);
    }

    inline void SetBgColor(QColor color)
    {
        _bgColor = std::move(color);
    }

    inline void SetFgColor(QColor color)
    {
        _fgColor = std::move(color);
    }

private:
    constexpr static inline auto kSize{12};

    QString _text;
    QFont _textFont;
    QColor _bgColor{Qt::white}, _fgColor{Qt::black};

protected:
    inline void paintEvent(QPaintEvent *event) override
    {
        QPainter painter{this};
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

        DrawBackground(painter);
        DrawText(painter);
    }

    inline void DrawBackground(QPainter &painter)
    {
        painter.save();
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush{_bgColor});
            painter.drawEllipse(rect());
        }
        painter.restore();
    }

    inline void DrawText(QPainter &painter)
    {
        painter.save();
        {
            painter.setPen(_fgColor);
            painter.setBrush(Qt::NoBrush);
            painter.setFont(_textFont);

            painter.drawText(rect(), Qt::AlignCenter, _text);
        }
        painter.restore();
    }
};

class TaskbarStatus : public QDialog
{
    Q_OBJECT

public:
    TaskbarStatus(QWidget *parent = nullptr);
    ~TaskbarStatus();

    void UpdateState(const Core::AirPods::State &state);
    void Unavailable();
    void Disconnect();
    void Unbind();

Q_SIGNALS:
    void OnSettingsChangedSafely(TaskbarStatusBehavior value);

private:
    constexpr static inline auto kFixedWidth{60};  // for horizontal taskbar
    constexpr static inline auto kFixedHeight{40}; // for vertical taskbar

    constexpr static inline auto kUpdateInterval{100ms};

    Ui::TaskbarStatus _ui;
    Helper::Sides<MiniIcon *> _icon = {new MiniIcon{this}, new MiniIcon{this}};
    Helper::Sides<Widget::Battery *> _battery = {
        new Widget::Battery{this}, new Widget::Battery{this}};
    TaskbarStatusBehavior _behavior{TaskbarStatusBehavior::Disable};
    bool _isWin11OrGreater{false}, _isActuallyEnabled{false}, _isStateReady{false},
        _isFirstTimeout{false};
    int _cachedLength{0};
    QTimer _updateTimer;
    std::optional<Core::AirPods::State> _airPodsState;
    Status _status{Status::Unavailable};
#if defined APD_DEBUG
    bool _drawDebugBorder{false};
#endif

    void UpdateVisible();
    bool Enable();
    bool Disable();
    void UpdatePos(const TaskBarInfo &info, bool enable);
    void Repaint();

    void OnUpdateTimer();
    void OnSettingsChanged(TaskbarStatusBehavior value);

    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
    UTILS_QT_REGISTER_LANGUAGECHANGE(QDialog, [this] {
        _ui.retranslateUi(this);
        Repaint();
    });
};

} // namespace Gui
