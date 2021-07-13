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

#include "../Helper.h"
#include "../Application.h"
#include "../Core/AppleCP.h"
#include "SelectWindow.h"


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

        setFixedSize(300, 300);
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlags(
            windowFlags() | Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
        );
        Utils::Qt::SetRoundedCorners(this, 30);

        auto palette = this->palette();
        palette.setColor(QPalette::Window, Qt::white);
        setPalette(palette);

        qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");

        _closeButton = new CloseButton{this};
        _leftBattery = new Widget::Battery{this};
        _rightBattery = new Widget::Battery{this};
        _caseBattery = new Widget::Battery{this};

        _leftBattery->hide();
        _rightBattery->hide();
        _caseBattery->hide();

        _screenSize = Application::primaryScreen()->size();

        auto deviceLabelPalette = _ui.deviceLabel->palette();
        deviceLabelPalette.setColor(QPalette::WindowText, QColor{94, 94, 94});
        _ui.deviceLabel->setPalette(deviceLabelPalette);

        InitCommonButton();

        _autoHideTimer->callOnTimeout([this] { DoHide(); });

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

        connect(this, &InfoWindow::ChangeButtonActionSafety, this, &InfoWindow::ChangeButtonAction);
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
        _showHideTimer->stop();
        _autoHideTimer->stop();
    }

    void InfoWindow::ShowSafety()
    {
        QMetaObject::invokeMethod(this, &InfoWindow::show);
    }

    void InfoWindow::InitCommonButton()
    {
        ChangeButtonAction(ButtonAction::NoButton);
        Utils::Qt::SetRoundedCorners(_ui.pushButton, 6);
        connect(_ui.pushButton, &QPushButton::clicked, this, &InfoWindow::OnButtonClicked);

        if (Core::Settings::GetCurrent().device_address == 0) {
            ChangeButtonAction(ButtonAction::Bind);
        }
    }

    void InfoWindow::ChangeButtonAction(ButtonAction action)
    {
        switch (action)
        {
        case ButtonAction::NoButton:
            _ui.pushButton->setText("");
            _ui.pushButton->hide();
            return;

        case ButtonAction::Bind:
            _ui.pushButton->setText(tr("Bind to AirPods"));
            break;

        default:
            spdlog::error("Unhandled button action. Value: '{}'", action);
            APD_ASSERT(false);
            return;
        }

        _buttonAction = action;
        _ui.pushButton->show();
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

    void InfoWindow::BindDevice()
    {
        spdlog::info("BindDevice");

        std::vector<Core::Bluetooth::Device> devices =
            Core::Bluetooth::DeviceManager::GetDevicesByState(Core::Bluetooth::DeviceState::Paired);

        spdlog::info("Devices count: {}", devices.size());

        devices.erase(std::remove_if(devices.begin(), devices.end(),
            [](const auto &device)
            {
                const auto vendorId = device.GetVendorId();
                const auto productId = device.GetProductId();

                const auto doErase = vendorId != Core::AppleCP::VendorId ||
                    Core::AppleCP::AirPods::GetModel(productId) == Core::AirPods::Model::Unknown;

                spdlog::trace(
                    "Device VendorId: '{}', ProductId: '{}', doErase: {}",
                    vendorId, productId, doErase
                );

                return doErase;
            }),
            devices.end()
        );

        spdlog::info("AirPods devices count: {} (filtered)", devices.size());

        if (devices.empty())
        {
            QMessageBox::warning(
                this,
                Config::ProgramName,
                QMessageBox::tr(
                    "No paired device found.\n"
                    "You need to pair your AirPods in Windows Bluetooth Settings first."
                )
            );
            return;
        }

        int selectedIndex = 0;

        if (devices.size() > 1)
        {
            QStringList deviceNames;
            for (const auto &device : devices)
            {
                auto displayName = device.GetDisplayName();

                spdlog::trace("Device name: '{}'", displayName);
                spdlog::trace("GetProductId: '{}' GetVendorId: '{}'", device.GetProductId(), device.GetVendorId());
                deviceNames.append(QString::fromStdString(displayName));
            }

            SelectWindow selector{tr("Please select your AirPods device below."), deviceNames, this};
            if (selector.exec() == -1) {
                spdlog::warn("selector.exec() == -1");
                return;
            }

            if (!selector.HasResult()) {
                spdlog::info("No result for selector.");
                return;
            }

            selectedIndex = selector.GetSeletedIndex();
            APD_ASSERT(selectedIndex >= 0 && selectedIndex < devices.size());
        }

        const auto &selectedDevice = devices.at(selectedIndex);

        spdlog::info(
            "Selected device index: '{}', device name: '{}'",
            selectedIndex, selectedDevice.GetDisplayName()
        );

        auto current = Core::Settings::GetCurrent();
        current.device_address = selectedDevice.GetAddress();
        Core::Settings::SaveToCurrentAndLocal(std::move(current));

        spdlog::info("Remembered this device.");

        ChangeButtonAction(ButtonAction::NoButton);
    }

    void InfoWindow::OnButtonClicked()
    {
        switch (_buttonAction)
        {
        case ButtonAction::Bind:
            spdlog::info("User clicked 'Bind'");
            BindDevice();
            break;

        default:
            spdlog::error("Unhandled button action. Value: '{}'", _buttonAction);
            APD_ASSERT(false);
            break;
        }
    }

    void InfoWindow::DoHide()
    {
        if (!_isShown) {
            return;
        }
        _isShown = false;

        _showHideTimer->stop();
        _autoHideTimer->stop();

        _showHideTimer->disconnect();
        _showHideTimer->callOnTimeout(
            [this]() {
                QPoint windowPos = pos();
                if (_screenSize.height() > windowPos.y()) {
                    move(windowPos.x(), windowPos.y() + _moveStep);
                }
                else {
                    _showHideTimer->stop();
                    hide();
                    StopAnimation();
                }
            }
        );

        _showHideTimer->start(1ms);
    }

    void InfoWindow::showEvent(QShowEvent *event)
    {
        if (_isShown) {
            return;
        }
        _isShown = true;

        _showHideTimer->stop();
        PlayAnimation();

        move(_screenSize.width() - size().width() - _screenMargin.width(), _screenSize.height());

        _showHideTimer->disconnect();
        _showHideTimer->callOnTimeout(
            [this]() {
                QPoint windowPos = pos();
                if (_screenSize.height() - size().height() - _screenMargin.height() < windowPos.y()) {
                    move(windowPos.x(), windowPos.y() - _moveStep);
                }
                else {
                    _showHideTimer->stop();
                }
            }
        );

        _showHideTimer->start(1ms);
        _autoHideTimer->start(10s);
    }

} // namespace Gui

#include "InfoWindow.moc"
