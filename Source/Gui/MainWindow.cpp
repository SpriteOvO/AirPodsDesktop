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

#include "MainWindow.h"

#include <QScreen>
#include <QCursor>
#include <QFontMetrics>
#include <QPainter>
#include <QMessageBox>

#include <Config.h>
#include "../Helper.h"
#include "../Error.h"
#include "../Core/AppleCP.h"
#include "../Core/Settings.h"
#include "DownloadWindow.h"
#include "SelectWindow.h"
#include "Theme.h"

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

            const auto &colors = Theme::Manager::Instance().Colors();

            QColor color;
            if (_isHoldDown) {
                color = colors.mainClosePressed;
            }
            else if (_isHovering) {
                color = colors.mainCloseHover;
            }
            else {
                color = colors.mainCloseBg;
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
            painter.setPen(QPen{
                Theme::Manager::Instance().Colors().mainCloseGlyph, 2.5, Qt::SolidLine,
                Qt::RoundCap});
            painter.setBrush(Qt::NoBrush);

            QSize size = this->size();

            constexpr int margin = 8;

            painter.drawLine(margin, margin, size.width() - margin, size.height() - margin);

            painter.drawLine(size.width() - margin, margin, margin, size.height() - margin);
        }
        painter.restore();
    }
};

//////////////////////////////////////////////////

enum class NewVersionAction {
    Update,
    Skip,
    Later,
};

NewVersionAction NewVersionMessageBox(
    QWidget *parent, const QString &title, const QString &text,
    const Core::Update::ReleaseInfo &releaseInfo)
{
    QMessageBox msgBox{QMessageBox::Question, title, text, QMessageBox::NoButton, parent};

    const auto buttonUpdate = msgBox.addButton(QMessageBox::tr("Update now"), QMessageBox::YesRole);
    const auto buttonSkip =
        msgBox.addButton(QMessageBox::tr("Skip this version"), QMessageBox::AcceptRole);
    const auto buttonView = msgBox.addButton(QMessageBox::tr("View release"), QMessageBox::NoRole);
    const auto buttonLater =
        msgBox.addButton(QMessageBox::tr("Remind me later"), QMessageBox::NoRole);

    msgBox.setDefaultButton(buttonUpdate);

    buttonView->disconnect();
    msgBox.connect(buttonView, &QPushButton::clicked, &msgBox, [&] { releaseInfo.OpenUrl(); });

    if (msgBox.exec() == -1) {
        return NewVersionAction::Later;
    }

    const auto clickedButton = msgBox.clickedButton();

    if (clickedButton == buttonUpdate) {
        return NewVersionAction::Update;
    }
    else if (clickedButton == buttonSkip) {
        return NewVersionAction::Skip;
    }
    else {
        return NewVersionAction::Later;
    }
}

//////////////////////////////////////////////////

MainWindow::MainWindow(QWidget *parent) : QDialog{parent}
{
    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");
    qRegisterMetaType<Core::Update::ReleaseInfo>("Core::Update::ReleaseInfo");

    _animationView = new Widget::AnimationView{this};
    _closeButton = new CloseButton{this};

    _ui.setupUi(this);

    setFixedSize(_windowSize);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() | Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // Frameless and region-clipped to the iOS card shape; DWM must leave it alone.
    setProperty(Theme::Manager::kSkipDwmProperty, true);
    Utils::Qt::SetRoundedCorners(this, _windowCornerRadius);

    _ui.pushButton->setProperty("cssClass", "accent");

    auto titleFont = _ui.deviceLabel->font();
    titleFont.setFamilies(Theme::DisplayFontFamilies());
    titleFont.setWeight(QFont::DemiBold);
    _ui.deviceLabel->setFont(titleFont);

    ApplyTheme();
    connect(&Theme::Manager::Instance(), &Theme::Manager::Changed, this, &MainWindow::ApplyTheme);

    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MainWindow::OnAppStateChanged);
    connect(_ui.pushButton, &QPushButton::clicked, this, &MainWindow::OnButtonClicked);
    connect(&_posAnimation, &QPropertyAnimation::finished, this, &MainWindow::OnPosMoveFinished);
    connect(_animationView, &Widget::AnimationView::Clicked, this, &MainWindow::OnAnimationClicked);
    connect(_closeButton, &CloseButton::Clicked, this, &MainWindow::DoHide);
    connect(_mediaPlayer, &QMediaPlayer::stateChanged, this, &MainWindow::OnPlayerStateChanged);

    connect(this, &MainWindow::UpdateStateSafely, this, &MainWindow::UpdateState);
    connect(this, &MainWindow::AvailableSafely, this, &MainWindow::Available);
    connect(this, &MainWindow::UnavailableSafely, this, &MainWindow::Unavailable);
    connect(this, &MainWindow::DisconnectSafely, this, &MainWindow::Disconnect);
    connect(this, &MainWindow::BindSafely, this, &MainWindow::Bind);
    connect(this, &MainWindow::UnbindSafely, this, &MainWindow::Unbind);
    connect(this, &MainWindow::ShowSafely, this, &MainWindow::show);
    connect(this, &MainWindow::HideSafely, this, &MainWindow::DoHide);
    connect(
        this, &MainWindow::VersionUpdateAvailableSafely, this, &MainWindow::VersionUpdateAvailable);

    _posAnimation.setDuration(500);
    _autoHideTimer->callOnTimeout([this] { DoHide(); });
    _mediaPlayer->setMuted(true);
    _mediaPlayer->setVideoOutput(_animationView->Surface());

    _ui.layoutAnimation->addWidget(_animationView);
    _ui.layoutPods->addWidget(_leftBattery);
    _ui.layoutPods->addWidget(_rightBattery);
    _ui.layoutCase->addWidget(_caseBattery);
    _ui.layoutClose->addWidget(_closeButton);

    // For getting the correct initial height of `_animationView` later
    _ui.layoutAnimation->activate();
    _animationView->show();

    _updateChecker.Start();
}

MainWindow::~MainWindow()
{
    _deviceQueryThread.request_stop();
    if (_deviceQueryThread.joinable()) {
        _deviceQueryThread.join();
    }
}

void MainWindow::UpdateState(const Core::AirPods::State &state)
{
    LOG(Info, "MainWindow::UpdateState");

    _viewModel.UpdateState(state);
    Repaint();
}

void MainWindow::Available()
{
    LOG(Info, "MainWindow::Available");

    _viewModel.Available();
    Repaint();
}

void MainWindow::Unavailable()
{
    LOG(Info, "MainWindow::Unavailable");

    _viewModel.Unavailable();
    Repaint();
}

void MainWindow::Disconnect()
{
    LOG(Info, "MainWindow::Disconnect");

    _viewModel.Disconnect();
    Repaint();
}

void MainWindow::Bind()
{
    LOG(Info, "MainWindow::Bind");

    _viewModel.Bind();
    Repaint();
}

void MainWindow::Unbind()
{
    LOG(Info, "MainWindow::Unbind");

    _viewModel.Unbind();
    Repaint();
}

void MainWindow::AskUserUpdate(const Core::Update::ReleaseInfo &releaseInfo)
{
    auto releaseVersion = releaseInfo.version.toString();

    QString changeLogBlock;
    if (!releaseInfo.changeLog.isEmpty()) {
        changeLogBlock = QString{"\n\n%1\n%2"}.arg(tr("Change log:")).arg(releaseInfo.changeLog);
    }

    auto action = NewVersionMessageBox(
        nullptr, Config::ProgramName,
        tr("Hey! I found a new version available!\n"
           "\n"
           "Current version: %1\n"
           "Latest version: %2"
           "%3")
            .arg(Core::Update::GetLocalVersion().toString())
            .arg(releaseVersion)
            .arg(changeLogBlock),
        releaseInfo);

    switch (action) {
    case Gui::NewVersionAction::Update:
        LOG(Info, "VersionUpdate: User clicked Update.");

        if (!releaseInfo.CanAutoUpdate()) {
            LOG(Info, "VersionUpdate: Cannot auto update. Popup latest url and quit.");
            releaseInfo.OpenUrl();
        }
        else {
            Gui::DownloadWindow{releaseInfo}.exec();
        }

        Utils::Qt::QuitApplicationSafely();
        return;

    case Gui::NewVersionAction::Skip:
        LOG(Info, "VersionUpdate: User clicked Skip.");

        Core::Settings::ModifiableAccess()->skipped_version = releaseVersion;

        // Continue checking for new versions after the skipped version
        break;

    case Gui::NewVersionAction::Later:
        LOG(Info, "VersionUpdate: User clicked Later.");

        _updateChecker.Stop();
        break;

    default:
        LOG(Warn, "VersionUpdate: Unhandled user clicked button.");
        break;
    }
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
        StopAnimation();
        _mediaPlayer->setMedia(QMediaContent{});
    }
    else {
        const auto presentation = GetAnimationPresentation(model.value());

        auto aspectRatio =
            (float)presentation.sourceSize.width() / (float)presentation.sourceSize.height();
        auto widgetWidth = _animationView->height() * aspectRatio;
        _animationView->setFixedWidth(widgetWidth);

        _mediaPlayer->setMedia(QUrl{presentation.resource});

        if (_isVisible) {
            PlayAnimation();
        }
        else {
            StopAnimation();
        }
    }

    _cacheModel = model;
}

void MainWindow::PlayAnimation()
{
    _isAnimationPlaying = true;
    _mediaPlayer->play();
    _animationView->show();
}

void MainWindow::StopAnimation()
{
    _animationView->hide();
    _animationView->Clear();

    _isAnimationPlaying = false;
    _mediaPlayer->stop();
}

void MainWindow::BindDevice()
{
    LOG(Info, "BindDevice");

    if (_deviceQueryRunning.exchange(true)) {
        LOG(Info, "Ignore duplicate device query while one is already running.");
        return;
    }

    if (_deviceQueryThread.joinable()) {
        _deviceQueryThread.join();
    }

    _deviceQueryThread = std::jthread{[this](std::stop_token stopToken) {
        Core::OS::Windows::Winrt::Initialize();
        auto devices = Core::AirPods::GetDevices();
        if (stopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, devices = std::move(devices)]() mutable {
                _deviceQueryRunning = false;
                ShowDeviceSelector(std::move(devices));
            },
            Qt::QueuedConnection);
    }};
}

void MainWindow::ShowDeviceSelector(std::vector<Core::Bluetooth::Device> devices)
{
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
            auto deviceName = device.GetName();

            LOG(Trace, "Device name: '{}'", deviceName);
            LOG(Trace, "GetProductId: '{}' GetVendorId: '{}'", device.GetProductId(),
                device.GetVendorId());
            deviceNames.append(QString::fromStdString(deviceName));
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
        selectedIndex, selectedDevice.GetName());

    Core::Settings::ModifiableAccess()->device_address = selectedDevice.GetAddress();
}

void MainWindow::ControlAutoHideTimer(bool start)
{
    LOG(Trace, "ControlAutoHideTimer: start == '{}', _isVisible == '{}'", start, _isVisible);

    if (start && _isVisible) {
        _autoHideTimer->start(10s);
    }
    else {
        _autoHideTimer->stop();
    }
}

void MainWindow::VersionUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo, bool silent)
{
    LOG(Info, "MainWindow::VersionUpdateAvailable: silent: `{}`", silent);

    if (!silent) {
        AskUserUpdate(releaseInfo);
    }
    else {
        emit SilentUpdateAvailable(releaseInfo);
    }
}

void MainWindow::Repaint()
{
    const auto presentation = _viewModel.Present();
    _ui.deviceLabel->setText(presentation.title);
    FitDeviceLabelFont();
    ChangeButtonAction(presentation.buttonAction);
    SetAnimation(presentation.animationModel);

    const auto applyBattery = [](Widget::Battery *widget, const BatteryPresentation &battery) {
        if (!battery.visible) {
            widget->hide();
            return;
        }

        widget->setCharging(battery.charging);
        widget->setValue(battery.value);
        widget->show();
    };

    applyBattery(_leftBattery, presentation.leftBattery);
    applyBattery(_rightBattery, presentation.rightBattery);
    applyBattery(_caseBattery, presentation.caseBattery);
}

void MainWindow::ApplyTheme()
{
    const auto &colors = Theme::Manager::Instance().Colors();

    Utils::Qt::SetPaletteColor(this, QPalette::Window, colors.mainSurface);
    Utils::Qt::SetPaletteColor(_ui.deviceLabel, QPalette::WindowText, colors.mainText);

    for (auto *battery : {_leftBattery, _rightBattery, _caseBattery}) {
        battery->setNormalColor(colors.batteryNormal);
        battery->setAlarmColor(colors.batteryAlarm);
        battery->setBorderColor(colors.batteryBorder);
        battery->setChargingIconColor(colors.mainText);
        battery->setBorderRadius(4);
        battery->setBackgroundRadius(2.5);
        battery->setHeadRadius(1.5);
        Utils::Qt::SetPaletteColor(battery, QPalette::WindowText, colors.mainText);
    }

    _closeButton->update();
    update();
}

void MainWindow::FitDeviceLabelFont()
{
    auto font = _ui.deviceLabel->font();
    font.setPointSize(_deviceLabelMaximumPointSize);

    const auto availableWidth = _ui.deviceLabel->contentsRect().width();
    while (font.pointSize() > _deviceLabelMinimumPointSize &&
           QFontMetrics{font}.horizontalAdvance(_ui.deviceLabel->text()) > availableWidth)
    {
        font.setPointSize(font.pointSize() - 1);
    }

    _ui.deviceLabel->setFont(font);
}

void MainWindow::OnAppStateChanged(Qt::ApplicationState state)
{
    LOG(Trace, "OnAppStateChanged: '{}'", Helper::ToString(state));
    ControlAutoHideTimer(state != Qt::ApplicationActive);
}

void MainWindow::OnPosMoveFinished()
{
    if (!_isVisible) {
        hide();
        StopAnimation();
    }
}

void MainWindow::OnAnimationClicked()
{
#if defined APD_DEBUG
    using namespace Core::AirPods;

    static Model next = Model::AirPods_1;

    _ui.deviceLabel->setText(Helper::ToString(next));
    SetAnimation(next);

    next = static_cast<Model>(Helper::ToUnderlying(next) + 1);
    if (next >= Model::_Max) {
        next = Model::AirPods_1;
    }
#endif
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

    if (!_isVisible) {
        return;
    }
    _isVisible = false;

    ControlAutoHideTimer(false);

    const auto screenGeometry = screen()->geometry();

    _posAnimation.stop();
    _posAnimation.setEasingCurve(QEasingCurve::InExpo);
    _posAnimation.setStartValue(pos());
    _posAnimation.setEndValue(QPoint{x(), screenGeometry.bottom() + 1});
    _posAnimation.start();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    LOG(Trace, "MainWindow: Show");

    if (_isVisible) {
        return;
    }
    _isVisible = true;

    PlayAnimation();
    ControlAutoHideTimer(true);

    auto targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (targetScreen == nullptr) {
        targetScreen = screen();
    }

    const auto availableGeometry = targetScreen->availableGeometry();
    const auto screenGeometry = targetScreen->geometry();
    const auto targetX = availableGeometry.right() - width() + 1 - _screenMargin.width();
    const auto targetY = availableGeometry.bottom() - height() + 1 - _screenMargin.height();

    move(targetX, screenGeometry.bottom() + 1);
    Utils::Qt::SetRoundedCorners(this, _windowCornerRadius);

    _posAnimation.stop();
    _posAnimation.setEasingCurve(QEasingCurve::OutExpo);
    _posAnimation.setStartValue(pos());
    _posAnimation.setEndValue(QPoint{targetX, targetY});
    _posAnimation.start();
}
} // namespace Gui

#include "MainWindow.moc"
