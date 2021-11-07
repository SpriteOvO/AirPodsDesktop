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

#include "MainWindow.h"

#include <future>

#include <QScreen>
#include <QPainter>
#include <QMessageBox>
#include <QDesktopServices>

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

MainWindow::MainWindow(QWidget *parent) : QDialog{parent}
{
    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");

    _closeButton = new CloseButton{this};

    _ui.setupUi(this);

    setFixedSize(300, 300);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() | Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    Utils::Qt::SetRoundedCorners(this, 30);
    Utils::Qt::SetRoundedCorners(_ui.pushButton, 6);
    Utils::Qt::SetPaletteColor(this, QPalette::Window, Qt::white);
    Utils::Qt::SetPaletteColor(_ui.deviceLabel, QPalette::WindowText, QColor{94, 94, 94});

    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MainWindow::OnAppStateChanged);
    connect(_ui.pushButton, &QPushButton::clicked, this, &MainWindow::OnButtonClicked);
    connect(&_posAnimation, &QPropertyAnimation::finished, this, &MainWindow::OnPosMoveFinished);
    connect(_closeButton, &CloseButton::Clicked, this, &MainWindow::DoHide);
    connect(_mediaPlayer, &QMediaPlayer::stateChanged, this, &MainWindow::OnPlayerStateChanged);

    connect(this, &MainWindow::UpdateStateSafety, this, &MainWindow::UpdateState);
    connect(this, &MainWindow::AvailableSafety, this, &MainWindow::Available);
    connect(this, &MainWindow::UnavailableSafety, this, &MainWindow::Unavailable);
    connect(this, &MainWindow::DisconnectSafety, this, &MainWindow::Disconnect);
    connect(this, &MainWindow::BindSafety, this, &MainWindow::Bind);
    connect(this, &MainWindow::UnbindSafety, this, &MainWindow::Unbind);
    connect(this, &MainWindow::ShowSafety, this, &MainWindow::show);
    connect(this, &MainWindow::HideSafety, this, &MainWindow::DoHide);

    _posAnimation.setDuration(500);
    _autoHideTimer->callOnTimeout([this] { DoHide(); });
    _mediaPlayer->setMuted(true);
    _mediaPlayer->setVideoOutput(_videoWidget);
    _videoWidget->setAspectRatioMode(Qt::IgnoreAspectRatio);
    _videoWidget->show();

    _ui.layoutAnimation->addWidget(_videoWidget);
    _ui.layoutPods->addWidget(_leftBattery);
    _ui.layoutPods->addWidget(_rightBattery);
    _ui.layoutCase->addWidget(_caseBattery);
    _ui.layoutClose->addWidget(_closeButton);

    Unavailable();
    CheckUpdate();
}

void MainWindow::UpdateState(const Core::AirPods::State &state)
{
    LOG(Info, "MainWindow::UpdateState");
    _lastState = State::Updating;

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

    ApdApp->GetTrayIcon()->UpdateState(state);
}

void MainWindow::Available()
{
    LOG(Info, "MainWindow::Available");
    if (_lastState != State::Unavailable) {
        return;
    }
    _lastState = State::Available;

    Disconnect();
}

void MainWindow::Unavailable()
{
    LOG(Info, "MainWindow::Unavailable");
    _lastState = State::Unavailable;

    _ui.deviceLabel->setText(tr("Unavailable"));

    SetAnimation(std::nullopt);

    _leftBattery->hide();
    _rightBattery->hide();
    _caseBattery->hide();

    ChangeButtonAction(ButtonAction::NoButton);

    ApdApp->GetTrayIcon()->Unavailable();
}

void MainWindow::Disconnect()
{
    LOG(Info, "MainWindow::Disconnect");
    if (_lastState == State::Unbind) {
        return;
    }
    _lastState = State::Disconnected;

    _ui.deviceLabel->setText(tr("Disconnected"));

    SetAnimation(std::nullopt);

    _leftBattery->hide();
    _rightBattery->hide();
    _caseBattery->hide();

    ChangeButtonAction(ButtonAction::NoButton);

    ApdApp->GetTrayIcon()->Disconnect();
}

void MainWindow::Bind()
{
    LOG(Info, "MainWindow::Bind");
    _lastState = State::Bind;

    Disconnect();
}

void MainWindow::Unbind()
{
    LOG(Info, "MainWindow::Unbind");
    _lastState = State::Unbind;

    _ui.deviceLabel->setText(tr("Waiting for Binding"));

    SetAnimation(std::nullopt);

    _leftBattery->hide();
    _rightBattery->hide();
    _caseBattery->hide();

    ChangeButtonAction(ButtonAction::Bind);

    ApdApp->GetTrayIcon()->Unbind();
}

void MainWindow::CheckUpdate()
{
    // LOG(Trace, "CheckUpdate: Poll.");

    const auto &DispatchNext = []() { Utils::Qt::Dispatch(&CheckUpdate); };

    using OptReleaseInfo = std::optional<Core::Update::ReleaseInfo>;
    static std::promise<OptReleaseInfo> pmsRelease;
    static std::optional<std::future<OptReleaseInfo>> optFuture;

    if (!optFuture.has_value()) {
        LOG(Info, "CheckUpdate: Prepare promise and future. Fetch release info async.");
        pmsRelease = decltype(pmsRelease){};
        optFuture = pmsRelease.get_future();
        std::thread{[]() { pmsRelease.set_value(Core::Update::FetchUpdateRelease()); }}.detach();
        DispatchNext();
        return;
    }

    if (!Helper::IsFutureReady(optFuture.value())) {
        // LOG(Trace, "CheckUpdate: The future is not ready.");
        DispatchNext();
        return;
    }

    auto optRelease = optFuture->get();
    optFuture.reset();
    LOG(Info, "CheckUpdate: Fetch release info successfully.");

    do {
        if (!optRelease.has_value()) {
            break;
        }

        auto releaseInfo = std::move(optRelease.value());
        auto releaseVersion = releaseInfo.version.toString();

        QString changeLogBlock;
        if (!releaseInfo.changeLog.isEmpty()) {
            changeLogBlock =
                QString{"%1\n%2\n\n"}.arg(tr("Change log:")).arg(releaseInfo.changeLog);
        }

        auto button = QMessageBox::question(
            nullptr, Config::ProgramName,
            tr("Hey! I found a new version available!\n"
               "\n"
               "Current version: %1\n"
               "Latest version: %2\n"
               "\n"
               "%3"
               "Click \"Ignore\" to skip this new version.\n"
               "\n"
               "Do you want to update it now?")
                .arg(Core::Update::GetLocalVersion().toString())
                .arg(releaseVersion)
                .arg(changeLogBlock),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Ignore, QMessageBox::Yes);

        if (button == QMessageBox::Yes) {
            LOG(Info, "CheckUpdate: User clicked Yes.");

            if (!releaseInfo.CanAutoUpdate()) {
                LOG(Info, "CheckUpdate: Popup latest url and quit.");
                QDesktopServices::openUrl(QUrl{releaseInfo.url});
                ApdApplication::QuitSafety();
                break;
            }

            Gui::DownloadWindow{std::move(releaseInfo)}.exec();
        }
        else if (button == QMessageBox::Ignore) {
            LOG(Info, "CheckUpdate: User clicked Ignore.");

            Core::Settings::ModifiableAccess()->skipped_version = releaseVersion;
        }
        else {
            LOG(Info, "CheckUpdate: User clicked No.");
        }

    } while (false);

    // TODO: Enable this timer after the icon notification is implemented
    //
    // QTimer::singleShot(1h, &CheckUpdate);
}

void MainWindow::ChangeButtonAction(ButtonAction action)
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

void MainWindow::SetAnimation(std::optional<Core::AirPods::Model> model)
{
    if (model == _cacheModel) {
        return;
    }

    if (!model.has_value()) {
        _mediaPlayer->setMedia(QMediaContent{});
    }
    else {
        QString media;
        switch (model.value()) {
        case Core::AirPods::Model::AirPods_1:
            media = "qrc:/Resource/Video/AirPods_1.avi";
            break;
        case Core::AirPods::Model::AirPods_2:
            media = "qrc:/Resource/Video/AirPods_2.avi";
            break;
        case Core::AirPods::Model::AirPods_3:
            media = "qrc:/Resource/Video/AirPods_3.avi";
            break;
        case Core::AirPods::Model::AirPods_Pro:
            media = "qrc:/Resource/Video/AirPods_Pro.avi";
            break;
        case Core::AirPods::Model::Powerbeats_3:
        case Core::AirPods::Model::Beats_X:
        case Core::AirPods::Model::Beats_Solo3:
        default:
            media = "qrc:/Resource/Video/AirPods_1.avi";
            break;
        }

        _mediaPlayer->setMedia(QUrl{media});
    }

    _cacheModel = model;
}

void MainWindow::PlayAnimation()
{
    _isAnimationPlaying = true;
    _mediaPlayer->play();
}

void MainWindow::StopAnimation()
{
    _isAnimationPlaying = false;
    _mediaPlayer->stop();
}

void MainWindow::BindDevice()
{
    //
    // TODO: Move non-UI code to `Core::AirPods::Manager`
    //

    LOG(Info, "BindDevice");

    std::vector<Core::Bluetooth::Device> devices =
        Core::Bluetooth::DeviceManager::GetDevicesByState(Core::Bluetooth::DeviceState::Paired);

    LOG(Info, "Devices count: {}", devices.size());

    devices.erase(
        std::remove_if(
            devices.begin(), devices.end(),
            [](const auto &device) {
                const auto vendorId = device.GetVendorId();
                const auto productId = device.GetProductId();

                const auto doErase =
                    vendorId != Core::AppleCP::VendorId ||
                    Core::AppleCP::AirPods::GetModel(productId) == Core::AirPods::Model::Unknown;

                LOG(Trace, "Device VendorId: '{}', ProductId: '{}', doErase: {}", vendorId,
                    productId, doErase);

                return doErase;
            }),
        devices.end());

    LOG(Info, "AirPods devices count: {} (filtered)", devices.size());

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

            LOG(Trace, "Device name: '{}'", displayName);
            LOG(Trace, "GetProductId: '{}' GetVendorId: '{}'", device.GetProductId(),
                device.GetVendorId());
            deviceNames.append(QString::fromStdString(displayName));
        }

        SelectWindow selector{tr("Please select your AirPods device below."), deviceNames, this};
        if (selector.exec() == -1) {
            LOG(Warn, "selector.exec() == -1");
            return;
        }

        if (!selector.HasResult()) {
            LOG(Info, "No result for selector.");
            return;
        }

        selectedIndex = selector.GetSeletedIndex();
        APD_ASSERT(selectedIndex >= 0 && selectedIndex < devices.size());
    }

    const auto &selectedDevice = devices.at(selectedIndex);

    LOG(Info, "Selected device index: '{}', device name: '{}'. Bound to this device.",
        selectedIndex, selectedDevice.GetDisplayName());

    Core::Settings::ModifiableAccess()->device_address = selectedDevice.GetAddress();
}

void MainWindow::ControlAutoHideTimer(bool start)
{
    LOG(Trace, "ControlAutoHideTimer: start == '{}', _isShown == '{}'", start, _isShown);

    if (start && _isShown) {
        _autoHideTimer->start(10s);
    }
    else {
        _autoHideTimer->stop();
    }
}

void MainWindow::OnAppStateChanged(Qt::ApplicationState state)
{
    LOG(Trace, "OnAppStateChanged: '{}'", Helper::ToString(state));
    ControlAutoHideTimer(state != Qt::ApplicationActive);
}

void MainWindow::OnPosMoveFinished()
{
    if (!_isShown) {
        hide();
        StopAnimation();
    }
}

void MainWindow::OnButtonClicked()
{
    switch (_buttonAction) {
    case ButtonAction::Bind:
        LOG(Info, "User clicked 'Bind'");
        BindDevice();
        break;

    default:
        FatalError(
            std::format("Unhandled ButtonAction: '{}'", Helper::ToUnderlying(_buttonAction)), true);
    }
}

// for loop play
void MainWindow::OnPlayerStateChanged(QMediaPlayer::State newState)
{
    if (newState == QMediaPlayer::StoppedState && _isAnimationPlaying) {
        _mediaPlayer->play();
    }
}

void MainWindow::DoHide()
{
    LOG(Trace, "MainWindow: Hide");

    if (!_isShown) {
        return;
    }
    _isShown = false;

    ControlAutoHideTimer(false);

    auto screenSize = ApdApplication::primaryScreen()->size();

    _posAnimation.stop();
    _posAnimation.setEasingCurve(QEasingCurve::InExpo);
    _posAnimation.setStartValue(pos());
    _posAnimation.setEndValue(QPoint{x(), screenSize.height()});
    _posAnimation.start();
}

void MainWindow::showEvent(QShowEvent *event)
{
    LOG(Trace, "MainWindow: Show");

    if (_isShown) {
        return;
    }
    _isShown = true;

    PlayAnimation();
    ControlAutoHideTimer(true);

    auto screenSize = ApdApplication::primaryScreen()->size();

    move(screenSize.width() - size().width() - _screenMargin.width(), screenSize.height());

    _posAnimation.stop();
    _posAnimation.setEasingCurve(QEasingCurve::OutExpo);
    _posAnimation.setStartValue(pos());
    _posAnimation.setEndValue(
        QPoint{x(), screenSize.height() - size().height() - _screenMargin.height()});
    _posAnimation.start();
}
} // namespace Gui

#include "MainWindow.moc"