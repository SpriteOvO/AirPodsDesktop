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

#include "ui_InfoWindow.h"

#include <QTimer>
#include <QVideoWidget>
#include <QMediaPlayer>

#include "../Core/AirPods.h"
#include "../Utils.h"
#include "Widget/Battery.h"


namespace Gui
{
    class CloseButton;
    class BatteryInfo;

    enum class ButtonAction : uint32_t
    {
        NoButton,
        Bind,
    };

    class InfoWindow : public QDialog
    {
        Q_OBJECT

    public:
        InfoWindow(QWidget *parent = nullptr);
        ~InfoWindow();

        void ShowSafety();

    Q_SIGNALS:
        void ChangeButtonActionSafety(ButtonAction action);
        void HideSafety();
        void UpdateStateSafety(const Core::AirPods::State &state);
        void DisconnectSafety(const QString &title);

    private:
        Ui::InfoWindow _ui;

        QVideoWidget *_videoWidget = new QVideoWidget{this};
        QMediaPlayer *_mediaPlayer = new QMediaPlayer{this};

        QSize _screenSize;
        QTimer *_showHideTimer = new QTimer{this},
            *_autoHideTimer = new QTimer{this};
        CloseButton *_closeButton;
        Widget::Battery *_leftBattery, *_rightBattery, *_caseBattery;
        Core::AirPods::Model _cacheModel{Core::AirPods::Model::Unknown};
        ButtonAction _buttonAction{ButtonAction::NoButton};

        bool _isShown{false};
        bool _isAnimationPlaying{false};

        constexpr static QSize _screenMargin{50, 100};
        constexpr static int _moveStep = 2;

        void InitCommonButton();

        void ChangeButtonAction(ButtonAction action);
        void UpdateState(const Core::AirPods::State &state);
        void Disconnect(const QString &title);

        void SetAnimation(Core::AirPods::Model model);
        void PlayAnimation();
        void StopAnimation();

        void BindDevice();

        void DoHide();
        void OnButtonClicked();
        void showEvent(QShowEvent *event) override;

        UTILS_QT_DISABLE_ESC_QUIT(InfoWindow, QDialog);
    };

} // namespace Gui
