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
#include <QCloseEvent>
#include <QEnterEvent>

#include <Config.h>
#include "../Helper.h"
#include "../Error.h"
#include "../Core/AppleCP.h"
#include "../Core/Settings.h"
#include "UpdateWindow.h"
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

    void enterEvent(QEnterEvent *event) override
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

MainWindow::MainWindow(QWidget *parent) : QDialog{parent}
{
    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");
    qRegisterMetaType<Core::Update::ReleaseInfo>("Core::Update::ReleaseInfo");

    _animationView = new Widget::AnimationView{this};
    _playback = new AnimationPlayback{*_animationView, this};
    _deviceImage = new Widget::DeviceImage{this};
    _closeButton = new CloseButton{this};

    _ui.setupUi(this);

    setFixedSize(_windowSize);
    setWindowFlags(windowFlags() | Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // A translucent backing store retains fractional edge alpha. Native window regions and
    // QBitmap masks are binary and produce visibly stepped corners, especially above 100% DPI.
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setProperty(Theme::Manager::kSkipDwmProperty, true);

    _ui.pushButton->setProperty("cssClass", "accent");

    auto titleFont = _ui.deviceLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    _ui.deviceLabel->setFont(titleFont);
    _ui.deviceLabel->setTextFormat(Qt::PlainText);
    _ui.deviceLabel->setWordWrap(false);
    _ui.deviceLabel->setProperty("fontRole", "display");

    ApplyTheme();
    connect(&Theme::Manager::Instance(), &Theme::Manager::Changed, this, &MainWindow::ApplyTheme);

    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MainWindow::OnAppStateChanged);
    connect(_ui.pushButton, &QPushButton::clicked, this, &MainWindow::OnButtonClicked);
    connect(&_posAnimation, &QPropertyAnimation::finished, this, &MainWindow::OnPosMoveFinished);
    connect(_deviceImage, &Widget::DeviceImage::Clicked, this, &MainWindow::OnAnimationClicked);
    connect(_animationView, &Widget::AnimationView::Clicked, this, &MainWindow::OnAnimationClicked);
    connect(_closeButton, &CloseButton::Clicked, this, &MainWindow::DoHide);

    connect(this, &MainWindow::UpdateStateSafely, this, &MainWindow::UpdateState);
    connect(this, &MainWindow::AvailableSafely, this, &MainWindow::Available);
    connect(this, &MainWindow::UnavailableSafely, this, &MainWindow::Unavailable);
    connect(this, &MainWindow::DisconnectSafely, this, &MainWindow::Disconnect);
    connect(this, &MainWindow::BindSafely, this, &MainWindow::Bind);
    connect(this, &MainWindow::UnbindSafely, this, &MainWindow::Unbind);
    connect(this, &MainWindow::ShowSafely, this, &MainWindow::Show);
    connect(this, &MainWindow::HideSafely, this, &MainWindow::DoHide);
    connect(
        this, &MainWindow::VersionUpdateAvailableSafely, this, &MainWindow::VersionUpdateAvailable);

    _posAnimation.setDuration(500);
    _autoHideTimer->setObjectName("autoHideTimer");
    _autoHideTimer->callOnTimeout([this] { DoHide(); });
    _lidSafetyTimer->setObjectName("lidSafetyTimer");
    _lidSafetyTimer->setSingleShot(true);
    _lidSafetyTimer->callOnTimeout([this] { DoHide(); });
    _podsRowGeometry = _ui.podsBatteryContainer->geometry();
    _sceneFade.setDuration(500);
    _sceneFade.setEasingCurve(QEasingCurve::InOutSine);
    connect(&_sceneFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        const qreal opacity = value.toReal();
        if (_fadePhase == FadePhase::Out) {
            if (_fadeMedia) {
                SetMediaOpacity(_shownView, opacity); // the outgoing view, still playing
            }
            if (_fadeBatteries) {
                // The snapshot is what was on screen when this phase began; it leaves in step
                // with the picture, however far along the picture already was.
                _batteryFade->SetSnapshotOpacity(_fadeFrom > 0.0 ? opacity / _fadeFrom : 0.0);
            }
        }
        else if (_fadePhase == FadePhase::In) {
            if (_fadeMedia) {
                SetMediaOpacity(_shownView, opacity); // the incoming view
            }
            if (_fadeBatteries) {
                const qreal remaining =
                    _fadeFrom < 1.0 ? (1.0 - opacity) / (1.0 - _fadeFrom) : 0.0;
                _batteryFade->SetCoverOpacity(remaining);
                _batteryFade->SetSnapshotOpacity(remaining); // only set when a fade-in was resumed
            }
        }
    });
    connect(&_sceneFade, &QVariantAnimation::finished, this, [this] {
        if (_fadePhase == FadePhase::Out) {
            // Everything old is gone: swap the picture, then bring the new scene in together.
            if (_fadeMedia) {
                _deviceImage->SetArrangement(_pendingArrangement, false);
                ShowMedia(_rotating);
                SetMediaOpacity(_shownView, 0.0);
            }
            if (_fadeBatteries) {
                _batteryFade->DropSnapshot();
            }
            StartSceneFade(FadePhase::In);
        }
        else {
            _fadePhase = FadePhase::Idle;
            if (_fadeBatteries) {
                _batteryFade->Finish();
            }
            _fadeMedia = _fadeBatteries = false;
        }
    });
    _podsOnlyTimer->setObjectName("podsOnlyTimer");
    _podsOnlyTimer->setSingleShot(true);
    _podsOnlyTimer->callOnTimeout([this] {
        _viewModel.SetPodsOnly(true);
        Repaint();
    });

    _ui.layoutAnimation->addWidget(_animationView);
    _ui.layoutAnimation->addWidget(_deviceImage);
    _animationView->hide();
    for (auto *battery : {_leftBattery, _rightBattery, _caseBattery}) {
        battery->setShape(Widget::Battery::Shape::Ring);
        battery->setTextPadding(4);
        battery->setBatterySize(28, 28);
    }
    _ui.layoutPods->addWidget(_leftBattery);
    _ui.layoutPods->addWidget(_rightBattery);
    _ui.layoutCase->addWidget(_caseBattery);
    _ui.layoutClose->addWidget(_closeButton);

    // For getting the correct initial height of `_deviceImage` later
    _ui.layoutAnimation->activate();
    _deviceImage->show();

}

void MainWindow::StartUpdateChecks()
{
    _updateChecker.Start();
}

MainWindow::~MainWindow()
{
    // Stop the decoder before QObject destroys the video surface child.
    delete _playback;
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

    // Lid state is only reported while the pods sit in the case, so with the pods out the last
    // decision stands: an opened case keeps the popup, a closed one released it.
    std::optional<bool> lidOpened;
    if (state.caseBox.isBothPodsInCase) {
        lidOpened = state.caseBox.isLidOpened;
    }
    if (lidOpened.has_value() && *lidOpened != _holdForOpenLid) {
        _holdForOpenLid = *lidOpened;
        ControlAutoHideTimer(_isVisible);
    }
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
    _holdForOpenLid = false;
    ControlAutoHideTimer(_isVisible);
}

void MainWindow::Disconnect()
{
    LOG(Info, "MainWindow::Disconnect");

    _viewModel.SetPodsOnly(false);
    _podsOnlyTimer->stop();
    _viewModel.Disconnect();
    Repaint();
    _holdForOpenLid = false;
    ControlAutoHideTimer(_isVisible);
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

    UpdateWindow updateWindow{releaseInfo};
    updateWindow.exec();
    const auto action = updateWindow.SelectedAction();
    switch (action) {
    case UpdateWindow::Action::Update:
        LOG(Info, "VersionUpdate: User clicked Update.");

        if (updateWindow.Result() == UpdateWindow::Outcome::KeepRunning) {
            return;
        }

        Utils::Qt::QuitApplicationSafely();
        return;

    case UpdateWindow::Action::Skip:
        LOG(Info, "VersionUpdate: User clicked Skip.");

        Core::Settings::ModifiableAccess()->skipped_version = releaseVersion;

        // Continue checking for new versions after the skipped version
        break;

    case UpdateWindow::Action::Later:
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
        _playback->SetAnimation({});
        _deviceImage->SetSource({});
    }
    else {
        const auto presentation = GetAnimationPresentation(model.value());

        auto aspectRatio =
            (float)presentation.sourceSize.width() / (float)presentation.sourceSize.height();
        auto widgetWidth = _deviceImage->height() * aspectRatio;
        _deviceImage->setFixedWidth(widgetWidth);
        _animationView->setFixedWidth(widgetWidth);
        _playback->SetAnimation(presentation);
        _deviceImage->SetSource(QImage{presentation.FallbackResource()});

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
    _sceneFade.stop();
    _fadePhase = FadePhase::Idle;
    _fadeMedia = _fadeBatteries = false;
    _batteryFade->Finish();
    _animationView->SetOpacity(1.0);
    _deviceImage->SetOpacity(1.0);
    _deviceImage->SetArrangement(_pendingArrangement, false);
    ShowMedia(_rotating);
}

void MainWindow::ShowMedia(bool rotating)
{
    _shownView = rotating;
    if (rotating) {
        _deviceImage->hide();
        _animationView->show();
        _playback->SetActive(true);
    }
    else {
        _playback->SetActive(false);
        _animationView->hide();
        _deviceImage->show();
    }
}

void MainWindow::SetMediaOpacity(bool rotatingView, qreal opacity)
{
    if (rotatingView) {
        _animationView->SetOpacity(opacity);
    }
    else {
        _deviceImage->SetOpacity(opacity);
    }
}

qreal MainWindow::MediaOpacity(bool rotatingView) const
{
    return rotatingView ? _animationView->Opacity() : _deviceImage->Opacity();
}

void MainWindow::StartSceneFade(FadePhase phase)
{
    _fadePhase = phase;
    // Pick up from wherever the picture currently is, so a reversed hand-over never jumps.
    const bool outgoing = phase == FadePhase::Out;
    const bool resumed = _sceneFade.state() == QAbstractAnimation::Running;
    _sceneFade.stop();
    const qreal from = _fadeMedia ? MediaOpacity(_shownView)
                       : resumed ? _sceneFade.currentValue().toReal()
                                 : (outgoing ? 1.0 : 0.0);
    _fadeFrom = from;
    _sceneFade.setStartValue(from);
    _sceneFade.setEndValue(outgoing ? 0.0 : 1.0);
    _sceneFade.setDuration(qRound(500 * (outgoing ? from : 1.0 - from)) + 1);
    _sceneFade.start();
}

void MainWindow::StopAnimation()
{
    _playback->SetActive(false);
    _animationView->hide();
    _deviceImage->hide();
}

void MainWindow::ApplyScene(Scene scene)
{
    using Arrangement = Widget::DeviceImage::Arrangement;

    switch (scene) {
    case Scene::BothPodsOut:
        if (!_podsOnlyTimer->isActive()) {
            _podsOnlyTimer->start(15s);
        }
        break;
    case Scene::PodsOnly:
        _podsOnlyTimer->stop();
        break;
    default:
        _podsOnlyTimer->stop();
        if (_viewModel.IsPodsOnly()) {
            _viewModel.SetPodsOnly(false);
        }
        break;
    }

    _pendingArrangement = scene == Scene::PodsOnly ? Arrangement::PodsOnly : Arrangement::Spread;
    _rotating = scene == Scene::PodsInCase || scene == Scene::BothPodsOut;

    // Pods only: the single ring takes the whole row so it sits under the centred pair.
    const QRect podsRow = _ui.podsBatteryContainer->geometry();
    const QRect centredRow{
        _podsRowGeometry.x(), _podsRowGeometry.y(), width() - 2 * _podsRowGeometry.x(),
        _podsRowGeometry.height()};
    const QRect fullRow = scene == Scene::PodsOnly ? centredRow : _podsRowGeometry;
    if (podsRow != fullRow) {
        _ui.podsBatteryContainer->setGeometry(fullRow);
    }
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

    if (start && _isVisible && _holdForOpenLid) {
        // Like iOS: the sheet stays while the case sits open, even as pods are taken out; the
        // safety timer only guards against a lid close we never get to see.
        _autoHideTimer->stop();
        if (!_lidSafetyTimer->isActive()) {
            _lidSafetyTimer->start(90s);
        }
    }
    else if (start && _isVisible) {
        _lidSafetyTimer->stop();
        _autoHideTimer->start(10s);
    }
    else {
        _lidSafetyTimer->stop();
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

    const bool wasRotating = _rotating;
    const auto wasArrangement = _pendingArrangement;
    const bool batteriesChanged =
        _lastPresentation.has_value() &&
        (_lastPresentation->leftBattery != presentation.leftBattery ||
         _lastPresentation->rightBattery != presentation.rightBattery ||
         _lastPresentation->caseBattery != presentation.caseBattery ||
         _lastPresentation->scene != presentation.scene);

    // Snapshot the battery row before anything moves; the old look then leaves together with
    // the old picture, and the new row arrives together with the new one.
    const QRect batteryRow{
        _podsRowGeometry.x(), _podsRowGeometry.y(), width() - 2 * _podsRowGeometry.x(),
        _podsRowGeometry.height()};
    QPixmap before;
    if (_isVisible && _lastPresentation.has_value() &&
        (batteriesChanged || _batteryFade->IsRunning())) {
        // Taken with any running overlay still in place: the fade resumes from exactly what
        // is on screen right now.
        before = grab(batteryRow);
    }

    FitDeviceLabelFont(presentation.title);
    ChangeButtonAction(presentation.buttonAction);
    SetAnimation(presentation.animationModel);
    ApplyScene(presentation.scene);

    const auto applyBattery = [](Widget::Battery *widget, const BatteryPresentation &battery) {
        if (!battery.visible) {
            widget->hide();
            return;
        }

        widget->setCharging(battery.charging);
        widget->setValue(battery.value);
        static_assert(
            static_cast<int>(Widget::Battery::Badge::Case) == static_cast<int>(BatteryBadge::Case),
            "BatteryBadge and Widget::Battery::Badge must line up");
        widget->setBadge(static_cast<Widget::Battery::Badge>(battery.badge));
        widget->show();
    };

    applyBattery(_leftBattery, presentation.leftBattery);
    applyBattery(_rightBattery, presentation.rightBattery);
    applyBattery(_caseBattery, presentation.caseBattery);
    _lastPresentation = presentation;

    const bool mediaChanged = wasRotating != _rotating || wasArrangement != _pendingArrangement;
    if (!_isVisible) {
        if (mediaChanged) {
            _deviceImage->SetArrangement(_pendingArrangement, false);
        }
        return;
    }
    if (!mediaChanged && before.isNull()) {
        return;
    }

    if (!before.isNull()) {
        _ui.podsBatteryContainer->layout()->activate();
        _ui.caseBatteryContainer->layout()->activate();
        _batteryFade->Begin(before, batteryRow);
        _fadeBatteries = true;
    }
    _fadeMedia = _fadeMedia || mediaChanged;

    // Whatever is on screen keeps doing its thing (the video keeps turning) while it fades out;
    // only then does the new scene fade in. A change caught mid-fade continues from where the
    // previous one got to.
    switch (_fadePhase) {
    case FadePhase::Idle:
        StartSceneFade(FadePhase::Out);
        break;
    case FadePhase::In:
        // The half-arrived scene goes back out, from wherever it is now.
        StartSceneFade(FadePhase::Out);
        break;
    case FadePhase::Out:
        if (_fadeMedia && _shownView == _rotating &&
            (_rotating || _deviceImage->GetArrangement() == _pendingArrangement)) {
            // Target flipped back to what is currently fading out: fade it in again.
            StartSceneFade(FadePhase::In);
            break;
        }
        // Restart the fade-out from the current opacity so a fresh snapshot leaves in step.
        StartSceneFade(FadePhase::Out);
        break;
    }
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

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::Manager::Instance().Colors().mainSurface);
    painter.drawRoundedRect(QRectF{rect()}, _windowCornerRadius, _windowCornerRadius);
}

void MainWindow::FitDeviceLabelFont(const QString &text)
{
    auto font = _ui.deviceLabel->font();
    font.setPointSize(_deviceLabelMaximumPointSize);

    const auto availableWidth = _ui.deviceLabel->contentsRect().width();
    while (font.pointSize() > _deviceLabelMinimumPointSize &&
           QFontMetrics{font}.horizontalAdvance(text) > availableWidth)
    {
        font.setPointSize(font.pointSize() - 1);
    }

    _ui.deviceLabel->setFont(font);
    const QFontMetrics metrics{font};
    const auto displayText = metrics.horizontalAdvance(text) > availableWidth
                                 ? metrics.elidedText(text, Qt::ElideMiddle, availableWidth)
                                 : text;
    _ui.deviceLabel->setText(displayText);
    _ui.deviceLabel->setAccessibleName(text);
    _ui.deviceLabel->setToolTip(displayText == text ? QString{} : text);
}

void MainWindow::OnAppStateChanged(Qt::ApplicationState state)
{
    LOG(Trace, "OnAppStateChanged: '{}'", Helper::ToString(state));
    ControlAutoHideTimer(_isVisible && state != Qt::ApplicationActive);
}

void MainWindow::OnPosMoveFinished()
{
    if (!_isVisible) {
        hide();
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

void MainWindow::DoHide()
{
    LOG(Trace, "MainWindow: Hide");

    if (!_isVisible) {
        return;
    }
    _isVisible = false;

    ControlAutoHideTimer(false);

    const auto screenGeometry = screen()->geometry();

    // Leaving is quicker than arriving: the popup is out of the way in a quarter second.
    _posAnimation.stop();
    _posAnimation.setDuration(250);
    _posAnimation.setEasingCurve(QEasingCurve::InCubic);
    _posAnimation.setStartValue(pos());
    _posAnimation.setEndValue(QPoint{x(), screenGeometry.bottom() + 1});
    _posAnimation.start();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    BeginShow(true);
}

void MainWindow::Show()
{
    if (isVisible()) {
        BeginShow(false);
    }
    else {
        QDialog::show();
    }
}

void MainWindow::BeginShow(bool fromHidden)
{
    LOG(Trace, "MainWindow: Show");

    auto targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (targetScreen == nullptr) {
        targetScreen = screen();
    }

    const auto availableGeometry = targetScreen->availableGeometry();
    const auto screenGeometry = targetScreen->geometry();
    const auto target = PopupPosition(availableGeometry, size(), _screenMargin);
    const bool changingScreen = screen() != targetScreen;

    if (changingScreen && !fromHidden) {
        // Hide before the native DPI transition so an intermediate resize is never painted.
        hide();
        QDialog::show();
        return;
    }

    if (_isVisible && !changingScreen &&
        (pos() == target || (_posAnimation.state() == QAbstractAnimation::Running &&
                             _posAnimation.endValue().toPoint() == target)))
    {
        ControlAutoHideTimer(true);
        return;
    }
    _isVisible = true;
    PlayAnimation();
    ControlAutoHideTimer(true);

    _posAnimation.stop();
    if (fromHidden || changingScreen) {
        // Resolve the native monitor and its DPI inside the destination screen before
        // positioning the slide origin outside it.
        move(target);
        move(target.x(), screenGeometry.bottom() + 1);
    }
    _posAnimation.setDuration(500);
    _posAnimation.setEasingCurve(QEasingCurve::OutExpo);
    _posAnimation.setStartValue(pos());
    _posAnimation.setEndValue(target);
    _posAnimation.start();
}

void MainWindow::hideEvent(QHideEvent *event)
{
    _isVisible = false;
    _batteryFade->hide();
    _posAnimation.stop();
    ControlAutoHideTimer(false);
    StopAnimation();
    QDialog::hideEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    DoHide();
}
} // namespace Gui

#include "MainWindow.moc"
