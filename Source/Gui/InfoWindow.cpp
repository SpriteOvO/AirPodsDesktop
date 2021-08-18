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

#include <Config.h>
#include "../Helper.h"
#include "../ErrorHandle.h"
#include "../Application.h"
#include "../Core/AppleCP.h"
#include "SelectWindow.h"

using namespace std::chrono_literals;

namespace Gui {

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

private:
    bool _isHovering{false}, _isHoldDown{false};

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter{this};
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

        DrawBackground(painter);
        DrawX(painter);
    }

    void enterEvent(QEvent *event) override
    {
        _isHovering = true;
        repaint();
    }

    void leaveEvent(QEvent *event) override
    {
        _isHovering = false;
        repaint();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        _isHoldDown = true;
        repaint();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        _isHoldDown = false;
        Q_EMIT Clicked();
        repaint();
    }

    void DrawBackground(QPainter &painter)
    {
        painter.save();
        {
            painter.setPen(Qt::NoPen);

            QColor color;
            if (_isHoldDown) {
                color = QColor{218, 218, 219};
            }
            else if (_isHovering) {
                color = QColor{228, 228, 229};
            }
            else {
                color = QColor{238, 238, 239};
            }

            painter.setBrush(QBrush{color});
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

            painter.drawLine(margin, margin, size.width() - margin, size.height() - margin);

            painter.drawLine(size.width() - margin, margin, margin, size.height() - margin);
        }
        painter.restore();
    }
};

InfoWindow::InfoWindow(QWidget *parent) : QDialog{parent}
{
    _ui.setupUi(this);

    setFixedSize(300, 300);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() | Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
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

    _screenSize = ApdApplication::primaryScreen()->size();

    auto deviceLabelPalette = _ui.deviceLabel->palette();
    deviceLabelPalette.setColor(QPalette::WindowText, QColor{94, 94, 94});
    _ui.deviceLabel->setPalette(deviceLabelPalette);

    InitCommonButton();

    _autoHideTimer->callOnTimeout([this] { DoHide(); });

    connect(_closeButton, &CloseButton::Clicked, this, &InfoWindow::DoHide);

    // for loop play
    //
    connect(_mediaPlayer, &QMediaPlayer::stateChanged, [this](QMediaPlayer::State newState) {
        if (newState == QMediaPlayer::StoppedState && _isAnimationPlaying) {
            _mediaPlayer->play();
        }
    });

    QObject::connect(
        qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                _autoHideTimer->stop();
            }
            else {
                _autoHideTimer->start(10s);
            }
        });

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
    switch (action) {
    case ButtonAction::NoButton:
        _ui.pushButton->setText("");
        _ui.pushButton->hide();
        return;

    case ButtonAction::Bind:
        _ui.pushButton->setText(tr("Bind to AirPods"));
        break;

    default:
        FatalError(std::format("Unhandled ButtonAction: '{}'", Helper::ToUnderlying(action)), true);
    }

    _buttonAction = action;
    _ui.pushButton->show();
}

void InfoWindow::UpdateState(const Core::AirPods::State &state)
{
    // _ui.deviceLabel->setText(Helper::ToString(state.model));
    _ui.deviceLabel->setText(Core::AirPods::GetDisplayName());

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
    switch (model) {
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
    SPDLOG_INFO("BindDevice");

    std::vector<Core::Bluetooth::Device> devices =
        Core::Bluetooth::DeviceManager::GetDevicesByState(Core::Bluetooth::DeviceState::Paired);

    SPDLOG_INFO("Devices count: {}", devices.size());

    devices.erase(
        std::remove_if(
            devices.begin(), devices.end(),
            [](const auto &device) {
                const auto vendorId = device.GetVendorId();
                const auto productId = device.GetProductId();

                const auto doErase =
                    vendorId != Core::AppleCP::VendorId ||
                    Core::AppleCP::AirPods::GetModel(productId) == Core::AirPods::Model::Unknown;

                SPDLOG_TRACE(
                    "Device VendorId: '{}', ProductId: '{}', doErase: {}", vendorId, productId,
                    doErase);

                return doErase;
            }),
        devices.end());

    SPDLOG_INFO("AirPods devices count: {} (filtered)", devices.size());

    if (devices.empty()) {
        QMessageBox::warning(
            this, Config::ProgramName,
            QMessageBox::tr("No paired device found.\n"
                            "You need to pair your AirPods in Windows Bluetooth Settings first."));
        return;
    }

    int selectedIndex = 0;

    if (devices.size() > 1) {
        QStringList deviceNames;
        for (const auto &device : devices) {
            auto displayName = device.GetDisplayName();

            SPDLOG_TRACE("Device name: '{}'", displayName);
            SPDLOG_TRACE(
                "GetProductId: '{}' GetVendorId: '{}'", device.GetProductId(),
                device.GetVendorId());
            deviceNames.append(QString::fromStdString(displayName));
        }

        SelectWindow selector{tr("Please select your AirPods device below."), deviceNames, this};
        if (selector.exec() == -1) {
            SPDLOG_WARN("selector.exec() == -1");
            return;
        }

        if (!selector.HasResult()) {
            SPDLOG_INFO("No result for selector.");
            return;
        }

        selectedIndex = selector.GetSeletedIndex();
        APD_ASSERT(selectedIndex >= 0 && selectedIndex < devices.size());
    }

    const auto &selectedDevice = devices.at(selectedIndex);

    SPDLOG_INFO(
        "Selected device index: '{}', device name: '{}'. Bound to this device.", selectedIndex,
        selectedDevice.GetDisplayName());

    Core::Settings::ModifiableAccess()->device_address = selectedDevice.GetAddress();

    ChangeButtonAction(ButtonAction::NoButton);
}

void InfoWindow::OnButtonClicked()
{
    switch (_buttonAction) {
    case ButtonAction::Bind:
        SPDLOG_INFO("User clicked 'Bind'");
        BindDevice();
        break;

    default:
        FatalError(
            std::format("Unhandled ButtonAction: '{}'", Helper::ToUnderlying(_buttonAction)), true);
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
    _showHideTimer->callOnTimeout([this]() {
        QPoint windowPos = pos();
        if (_screenSize.height() > windowPos.y()) {
            move(windowPos.x(), windowPos.y() + _moveStep);
        }
        else {
            _showHideTimer->stop();
            hide();
            StopAnimation();
        }
    });

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
    _showHideTimer->callOnTimeout([this]() {
        QPoint windowPos = pos();
        if (_screenSize.height() - size().height() - _screenMargin.height() < windowPos.y()) {
            move(windowPos.x(), windowPos.y() - _moveStep);
        }
        else {
            _showHideTimer->stop();
        }
    });

    _showHideTimer->start(1ms);
}
} // namespace Gui

#include "InfoWindow.moc"
