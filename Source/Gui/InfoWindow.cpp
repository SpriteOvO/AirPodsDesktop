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

#include "InfoWindow.h"

#include <QScreen>
#include <QPainter>
#include <QPainterPath>

#include "../Helper.h"
#include "../Application.h"


using namespace std::chrono_literals;

namespace Gui
{
    class CloseButton : public QWidget
    {
        Q_OBJECT

    public:
        CloseButton(QWidget *parent = nullptr)
        {
            setFixedSize(25, 25);
        }

    Q_SIGNALS:
        void Clicked();

    protected:
        void paintEvent(QPaintEvent *event) override
        {
            QPainter painter{this};
            painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

            DrawBackground(painter);
            DrawX(painter);
        }

        void mouseReleaseEvent(QMouseEvent *event) override
        {
            Q_EMIT Clicked();
        }

        void DrawBackground(QPainter &painter)
        {
            painter.save();
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QBrush{QColor{238, 238, 239}});
                painter.drawEllipse(rect());
            }
            painter.restore();
        }

        void DrawX(QPainter &painter)
        {
            painter.save();
            {
                painter.setPen(QPen{QColor{131, 131, 135}, 3});
                painter.setBrush(Qt::NoBrush);

                QSize size = this->size();

                constexpr int margin = 8;

                painter.drawLine(
                    margin,
                    margin,
                    size.width() - margin,
                    size.height() - margin
                );

                painter.drawLine(
                    size.width() - margin,
                    margin,
                    margin,
                    size.height() - margin
                );
            }
            painter.restore();
        }
    };


    InfoWindow::InfoWindow(QWidget *parent) : QDialog{parent}
    {
        _ui.setupUi(this);

        _closeButton = new CloseButton{this};
        _leftBattery = new Widget::Battery{this};
        _rightBattery = new Widget::Battery{this};
        _caseBattery = new Widget::Battery{this};

        _leftBattery->hide();
        _rightBattery->hide();
        _caseBattery->hide();

        qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");

        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

        _screenSize = Application::primaryScreen()->size();

        QPalette palette = this->palette();
        palette.setColor(QPalette::Window, Qt::white);
        setPalette(palette);

        QPalette deviceLabelPalette = _ui.deviceLabel->palette();
        deviceLabelPalette.setColor(QPalette::WindowText, QColor{94, 94, 94});
        _ui.deviceLabel->setPalette(deviceLabelPalette);

        setFixedSize(300, 300);
        SetRoundedCorners();

        connect(_closeButton, &CloseButton::Clicked, this, &InfoWindow::DoHide);

        // for loop play
        //
        connect(
            _mediaPlayer,
            &QMediaPlayer::stateChanged,
            [this](QMediaPlayer::State newState) {
                if (newState == QMediaPlayer::StoppedState && _isAnimationPlaying) {
                    _mediaPlayer->play();
                }
            }
        );

        connect(this, &InfoWindow::UpdateStateSafety, this, &InfoWindow::UpdateState);
        connect(this, &InfoWindow::DisconnectSafety, this, &InfoWindow::Disconnect);
        connect(this, &InfoWindow::HideSafety, this, &InfoWindow::DoHide);

        _mediaPlayer->setMuted(true);
        _mediaPlayer->setVideoOutput(_videoWidget);
        _videoWidget->setAspectRatioMode(Qt::IgnoreAspectRatio);
        _videoWidget->show();

        _ui.layoutAnimation->addWidget(_videoWidget);

        _ui.layoutPods->addWidget(_leftBattery);
        _ui.layoutPods->addWidget(_rightBattery);
        _ui.layoutCase->addWidget(_caseBattery);
        _ui.layoutClose->addWidget(_closeButton);
    }

    InfoWindow::~InfoWindow()
    {
        _showTimer->stop();
        _hideTimer->stop();
    }

    void InfoWindow::ShowSafety()
    {
        QMetaObject::invokeMethod(this, &InfoWindow::show);
    }

    void InfoWindow::UpdateState(const Core::AirPods::State &state)
    {
        _ui.deviceLabel->setText(Helper::ToString(state.model));

        SetAnimation(state.model);

        if (!state.pods.left.battery.has_value()) {
            _leftBattery->hide();
        }
        else {
            _leftBattery->setCharging(state.pods.left.isCharging);
            _leftBattery->setValue(state.pods.left.battery.value());
            _leftBattery->show();
        }

        if (!state.pods.right.battery.has_value()) {
            _rightBattery->hide();
        }
        else {
            _rightBattery->setCharging(state.pods.right.isCharging);
            _rightBattery->setValue(state.pods.right.battery.value());
            _rightBattery->show();
        }

        if (!state.caseBox.battery.has_value()) {
            _caseBattery->hide();
        }
        else {
            _caseBattery->setCharging(state.caseBox.isCharging);
            _caseBattery->setValue(state.caseBox.battery.value());
            _caseBattery->show();
        }
    }

    void InfoWindow::Disconnect(const QString &title)
    {
        _ui.deviceLabel->setText(title);

        _cacheModel = Core::AirPods::Model::Unknown;
        _mediaPlayer->setMedia(QMediaContent{});

        _leftBattery->hide();
        _rightBattery->hide();
        _caseBattery->hide();
    }

    void InfoWindow::SetRoundedCorners()
    {
        QPainterPath path;
        path.addRoundedRect(rect(), 30, 30);
        setMask(QRegion{path.toFillPolygon().toPolygon()});
    }

    void InfoWindow::SetAnimation(Core::AirPods::Model model)
    {
        if (model == _cacheModel) {
            return;
        }
        _cacheModel = model;

        QString media;
        switch (model)
        {
        case Core::AirPods::Model::AirPods_1:
        case Core::AirPods::Model::AirPods_2:
            media = "qrc:/Resource/Video/AirPods_1_2.avi";
            break;
        case Core::AirPods::Model::AirPods_Pro:
            media = "qrc:/Resource/Video/AirPods_Pro.avi";
            break;
        case Core::AirPods::Model::Powerbeats_3:
        case Core::AirPods::Model::Beats_X:
        case Core::AirPods::Model::Beats_Solo3:
        default:
            media = "qrc:/Resource/Video/AirPods_1_2.avi";
            break;
        }

        _mediaPlayer->setMedia(QUrl{media});
    }

    void InfoWindow::PlayAnimation()
    {
        _isAnimationPlaying = true;
        _mediaPlayer->play();
    }

    void InfoWindow::StopAnimation()
    {
        _isAnimationPlaying = false;
        _mediaPlayer->stop();
    }

    void InfoWindow::DoHide()
    {
        if (!_isShown) {
            return;
        }
        _isShown = false;

        _showTimer->stop();

        _hideTimer->callOnTimeout(
            [this]() {
                QPoint windowPos = pos();
                if (_screenSize.height() > windowPos.y()) {
                    move(windowPos.x(), windowPos.y() + _moveStep);
                }
                else {
                    _hideTimer->stop();
                    hide();
                    StopAnimation();
                }
            }
        );

        _hideTimer->start(1ms);
    }

    void InfoWindow::showEvent(QShowEvent *event)
    {
        if (_isShown) {
            return;
        }
        _isShown = true;

        _hideTimer->stop();
        PlayAnimation();

        move(_screenSize.width() - size().width() - _screenMargin.width(), _screenSize.height());

        _showTimer->callOnTimeout(
            [this]() {
                QPoint windowPos = pos();
                if (_screenSize.height() - size().height() - _screenMargin.height() < windowPos.y()) {
                    move(windowPos.x(), windowPos.y() - _moveStep);
                }
                else {
                    _showTimer->stop();
                }
            }
        );

        _showTimer->start(1ms);
    }

} // namespace Gui

#include "InfoWindow.moc"
