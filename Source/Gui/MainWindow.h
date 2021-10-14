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

#include <QDialog>

#include "ui_MainWindow.h"

#include <QVideoWidget>
#include <QMediaPlayer>
#include <QPropertyAnimation>

#include "../Core/AirPods.h"
#include "../Utils.h"
#include "Widget/Battery.h"

namespace Gui {

class CloseButton;
class BatteryInfo;

enum class ButtonAction : uint32_t {
    NoButton,
    Bind,
};

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

    void UpdateState(const Core::AirPods::State &state);
    void Available();
    void Unavailable();
    void Disconnect();
    void Bind();
    void Unbind();

Q_SIGNALS:
    void UpdateStateSafety(const Core::AirPods::State &state);
    void AvailableSafety();
    void UnavailableSafety();
    void DisconnectSafety();
    void BindSafety();
    void UnbindSafety();
    void ShowSafety();
    void HideSafety();

private:
    enum class State { Updating, Available, Unavailable, Disconnected, Bind, Unbind };

    constexpr static QSize _screenMargin{50, 100};

    Ui::MainWindow _ui;

    QPropertyAnimation _posAnimation{this, "pos"};
    QVideoWidget *_videoWidget = new QVideoWidget{this};
    QMediaPlayer *_mediaPlayer = new QMediaPlayer{this};
    QTimer *_autoHideTimer = new QTimer{this};
    CloseButton *_closeButton;
    Widget::Battery *_leftBattery = new Widget::Battery{this};
    Widget::Battery *_rightBattery = new Widget::Battery{this};
    Widget::Battery *_caseBattery = new Widget::Battery{this};

    std::optional<Core::AirPods::Model> _cacheModel;
    ButtonAction _buttonAction{ButtonAction::NoButton};
    State _lastState{State::Unavailable};
    bool _isShown{false};
    bool _isAnimationPlaying{false};

    static void CheckUpdate();

    void ChangeButtonAction(ButtonAction action);
    void SetAnimation(std::optional<Core::AirPods::Model> model);
    void PlayAnimation();
    void StopAnimation();
    void BindDevice();
    void ControlAutoHideTimer(bool start);

    void OnAppStateChanged(Qt::ApplicationState state);
    void OnPosMoveFinished();
    void OnButtonClicked();
    void OnPlayerStateChanged(QMediaPlayer::State newState);

    void DoHide();
    void showEvent(QShowEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};
} // namespace Gui
