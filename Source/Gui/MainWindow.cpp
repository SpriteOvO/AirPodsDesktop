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

MainWindow::MainWindow(QWidget *parent) : QDialog{parent}
{
    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");
    qRegisterMetaType<Core::Update::ReleaseInfo>("Core::Update::ReleaseInfo");

    _animationView = new Widget::AnimationView{this};
    _playback = new AnimationPlayback{*_animationView, this};
    _closeButton = new Widget::CloseButton{this};

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
    titleFont.setWeight(QFont::Bold);
    _ui.deviceLabel->setFont(titleFont);
    _ui.deviceLabel->setTextFormat(Qt::PlainText);
    // A long name wraps onto a second line like the iOS sheet; see `FitDeviceLabelFont`.
    _ui.deviceLabel->setWordWrap(true);
    _ui.deviceLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    _ui.deviceLabel->setProperty("fontRole", "display");

    ApplyTheme();
    connect(&Theme::Manager::Instance(), &Theme::Manager::Changed, this, &MainWindow::ApplyTheme);

    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MainWindow::OnAppStateChanged);
    connect(_ui.pushButton, &QPushButton::clicked, this, &MainWindow::OnButtonClicked);
    connect(&_posAnimation, &QPropertyAnimation::finished, this, &MainWindow::OnPosMoveFinished);
    connect(_animationView, &Widget::AnimationView::Clicked, this, &MainWindow::OnAnimationClicked);
    connect(_closeButton, &Widget::CloseButton::clicked, this, &MainWindow::DoHide);

    connect(this, &MainWindow::UpdateStateSafely, this, &MainWindow::UpdateState);
    connect(this, &MainWindow::AvailableSafely, this, &MainWindow::Available);
    connect(this, &MainWindow::UnavailableSafely, this, &MainWindow::Unavailable);
    connect(this, &MainWindow::DisconnectSafely, this, &MainWindow::Disconnect);
    connect(this, &MainWindow::BindSafely, this, &MainWindow::Bind);
    connect(this, &MainWindow::UnbindSafely, this, &MainWindow::Unbind);
    connect(
        this, &MainWindow::ShowWithoutActivatingSafely, this, &MainWindow::ShowWithoutActivating);
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
    _podRingSpacing = _ui.layoutPods->spacing();
    _caseRowGeometry = _ui.caseBatteryContainer->geometry();
    _sceneFade.setDuration(500);
    _sceneFade.setEasingCurve(QEasingCurve::InOutSine);
    connect(&_sceneFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        const qreal opacity = value.toReal();
        if (_fadePhase == FadePhase::Out) {
            if (_fadeMedia) {
                _animationView->SetOpacity(opacity); // the outgoing picture, still playing
            }
            if (_fadeBatteries) {
                // The snapshot is what was on screen when this phase began; it leaves in step
                // with the picture, however far along the picture already was.
                _batteryFade->SetSnapshotOpacity(_fadeFrom > 0.0 ? opacity / _fadeFrom : 0.0);
            }
        }
        else if (_fadePhase == FadePhase::In) {
            if (_fadeMedia) {
                _animationView->SetOpacity(opacity); // the incoming picture
            }
            if (_fadeBatteries) {
                const qreal remaining = _fadeFrom < 1.0 ? (1.0 - opacity) / (1.0 - _fadeFrom) : 0.0;
                _batteryFade->SetCoverOpacity(remaining);
                _batteryFade->SetSnapshotOpacity(remaining); // only set when a fade-in was resumed
            }
        }
    });
    connect(&_sceneFade, &QVariantAnimation::finished, this, [this] {
        if (_fadePhase == FadePhase::Out) {
            // Everything old is gone: swap the picture, then bring the new scene in together.
            if (_fadeMedia && _cacheModel.has_value()) {
                ShowMedia(_rotating);
                _animationView->SetOpacity(0.0);
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
            _animationView->SetOpacity(1.0);
            _fadeMedia = _fadeBatteries = false;
        }
    });

    // Centred horizontally so the battery rows can be placed under it without a layout pass.
    _ui.layoutAnimation->addWidget(_animationView, 0, 0, Qt::AlignHCenter);
    for (auto *battery : {_leftBattery, _rightBattery, _caseBattery}) {
        battery->setShape(Widget::Battery::Shape::Ring);
        battery->setTextPadding(4);
        battery->setBatterySize(28, 28);
    }
    _ui.layoutPods->addWidget(_leftBattery);
    _ui.layoutPods->addWidget(_rightBattery);
    _ui.layoutCase->addWidget(_caseBattery);
    _ui.layoutClose->addWidget(_closeButton);

    // Lay the view out once so `SetAnimation` can size it from its height; it stays hidden until
    // there is a device to show.
    _ui.layoutAnimation->activate();
    _animationView->hide();
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
    if (lidOpened.has_value()) {
        SetHoldForOpenLid(*lidOpened);
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
    SetHoldForOpenLid(false);
}

void MainWindow::Disconnect()
{
    LOG(Info, "MainWindow::Disconnect");

    _viewModel.Disconnect();
    Repaint();
    SetHoldForOpenLid(false);
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

void MainWindow::SetAnimation(std::optional<Core::AirPods::Model> model, Core::AppleCP::Color color)
{
    // Compared by the render that will play, not by the advertised color: several color codes
    // can share one render, and reloading it would restart the video for nothing.
    const auto presentation = model.has_value() ? GetAnimationPresentation(model.value(), color)
                                                : AnimationPresentation{};
    if (model == _cacheModel && presentation.resource == _cacheResource) {
        return;
    }

    if (!model.has_value()) {
        StopAnimation();
        _playback->SetAnimation({});
        _podsCenter = 0.5;
        _caseCenter.reset();
        _still.reset();
        _animationView->SetStillImage({});
    }
    else {
        _videoAspect =
            static_cast<qreal>(presentation.sourceSize.width()) / presentation.sourceSize.height();
        _still = Widget::LoadDeviceStill(presentation.StillDirectory());
        _animationView->SetStillImage(_still.has_value() ? _still->image : QImage{});
        _animationView->setFixedWidth(MediaWidth(_rotating));
        _playback->SetAnimation(presentation);
        _podsCenter = presentation.podsCenter;
        _caseCenter = presentation.caseCenter;

        if (_isVisible) {
            PlayAnimation();
        }
        else {
            StopAnimation();
        }
    }

    _cacheModel = model;
    _cacheResource = presentation.resource;
}

void MainWindow::PlayAnimation()
{
    _sceneFade.stop();
    _fadePhase = FadePhase::Idle;
    _fadeMedia = _fadeBatteries = false;
    _batteryFade->Finish();
    _animationView->SetOpacity(1.0);
    ShowMedia(_rotating);
}

void MainWindow::ShowMedia(bool rotating)
{
    _shownRotating = rotating;
    _animationView->SetShowStill(!rotating);
    _animationView->setFixedWidth(MediaWidth(rotating));
    _animationView->show();
    // Place it now rather than on the next layout pass, so its first frame is not drawn at the
    // previous model's position.
    _ui.layoutAnimation->activate();
    _playback->SetActive(rotating);
}

int MainWindow::MediaWidth(bool rotating) const
{
    // The still and the video are framed differently, so the view takes the width of whichever
    // it shows; its height is the same for both.
    const qreal aspect = !rotating && _still.has_value()
                             ? static_cast<qreal>(_still->image.width()) / _still->image.height()
                             : _videoAspect;
    return qRound(_animationView->height() * aspect);
}

void MainWindow::StartSceneFade(FadePhase phase)
{
    _fadePhase = phase;
    // Pick up from wherever the picture currently is, so a reversed hand-over never jumps.
    const bool outgoing = phase == FadePhase::Out;
    const bool resumed = _sceneFade.state() == QAbstractAnimation::Running;
    _sceneFade.stop();
    const qreal from = _fadeMedia ? _animationView->Opacity()
                       : resumed  ? _sceneFade.currentValue().toReal()
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
    _animationView->SetOpacity(1.0);
    _fadeMedia = false;
}

QRect MainWindow::BatteryRowRect() const
{
    const int top = (std::min)(_podsRowGeometry.y(), _caseRowGeometry.y());
    const int bottom = (std::max)(_podsRowGeometry.bottom(), _caseRowGeometry.bottom());
    return {0, top, width(), bottom - top + 1};
}

void MainWindow::LayoutBatteryRows()
{
    // Like iOS, each ring sits under the object it describes: the pods' ring(s) under the pods,
    // the case ring under the case, a headset's ring under the headset. The view is centred in
    // its cell, so its position follows from its width without waiting for a layout pass.
    const QRect cell =
        _ui.animationContainer->geometry().marginsRemoved(_ui.layoutAnimation->contentsMargins());
    const int viewWidth = (std::min)(cell.width(), MediaWidth(_rotating));
    const qreal viewLeft = cell.x() + (cell.width() - viewWidth) / 2.0;

    const auto centredUnder = [&](QRect row, qreal fraction) {
        const int left = qRound(viewLeft + fraction * viewWidth - row.width() / 2.0);
        row.moveLeft(std::clamp(left, 0, width() - row.width()));
        return row;
    };

    // In the still scene each pod has its own ring, as far apart as the pods are drawn: the two
    // rings lean towards each other and the spacing between them sets the distance. A lone ring
    // stays in the middle, like the rings of every other scene.
    const bool still = !_rotating && _still.has_value();
    const bool apart = still && !_leftBattery->isHidden() && !_rightBattery->isHidden();
    int ringSpacing = _podRingSpacing;
    if (apart) {
        const qreal ringDistance = (_still->rightCenter - _still->leftCenter) * viewWidth;
        const qreal ringWidth = (_leftBattery->width() + _rightBattery->width()) / 2.0;
        ringSpacing = (std::max)(0, qRound(ringDistance - ringWidth));
    }
    _ui.layoutPods->setAlignment(_leftBattery, apart ? Qt::AlignRight : Qt::Alignment{});
    _ui.layoutPods->setAlignment(_rightBattery, apart ? Qt::AlignLeft : Qt::Alignment{});
    if (_ui.layoutPods->spacing() != ringSpacing) {
        _ui.layoutPods->setSpacing(ringSpacing);
    }

    const qreal podsCenter = still ? (_still->leftCenter + _still->rightCenter) / 2.0 : _podsCenter;
    const std::optional<qreal> caseCenter = still ? std::optional{_still->caseCenter} : _caseCenter;

    const QRect podsRow = centredUnder(_podsRowGeometry, podsCenter);
    if (_ui.podsBatteryContainer->geometry() != podsRow) {
        _ui.podsBatteryContainer->setGeometry(podsRow);
    }
    const QRect caseRow =
        caseCenter.has_value() ? centredUnder(_caseRowGeometry, *caseCenter) : _caseRowGeometry;
    if (_ui.caseBatteryContainer->geometry() != caseRow) {
        _ui.caseBatteryContainer->setGeometry(caseRow);
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
#if defined APD_OS_WIN
        Core::OS::Windows::Winrt::Initialize();
#endif
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
            tr("No paired device found.\n"
               "You need to pair your AirPods in the system Bluetooth settings first."));
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

void MainWindow::SetHoldForOpenLid(bool hold)
{
    if (hold == _holdForOpenLid) {
        return;
    }
    _holdForOpenLid = hold;
    // Re-apply the last request rather than forcing a start, so a popup the user is hovering or
    // has activated keeps no timer running.
    ControlAutoHideTimer(_autoHideRequested);
}

void MainWindow::ControlAutoHideTimer(bool start)
{
    LOG(Trace, "ControlAutoHideTimer: start == '{}', _isVisible == '{}'", start, _isVisible);

    _autoHideRequested = start;

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

    const bool batteriesChanged =
        _lastPresentation.has_value() &&
        (_lastPresentation->leftBattery != presentation.leftBattery ||
         _lastPresentation->rightBattery != presentation.rightBattery ||
         _lastPresentation->caseBattery != presentation.caseBattery ||
         _lastPresentation->scene != presentation.scene ||
         _lastPresentation->animationModel != presentation.animationModel);

    // Snapshot the battery row before anything moves; the old look then leaves together with
    // the old picture, and the new row arrives together with the new one.
    const QRect batteryRow = BatteryRowRect();
    QPixmap before;
    if (_isVisible && _lastPresentation.has_value() &&
        (batteriesChanged || _batteryFade->IsRunning()))
    {
        // Taken with any running overlay still in place: the fade resumes from exactly what
        // is on screen right now.
        before = grab(batteryRow);
    }

    FitDeviceLabelFont(presentation.title);
    ChangeButtonAction(presentation.buttonAction);
    // One pod out of the case is the one scene iOS shows as a still.
    _rotating = presentation.scene != Scene::OnePodOut;
    SetAnimation(presentation.animationModel, presentation.animationColor);

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
    LayoutBatteryRows();
    _lastPresentation = presentation;

    if (!_isVisible) {
        return; // `PlayAnimation` puts up the right picture when the popup shows
    }
    const bool mediaChanged = _cacheModel.has_value() && _shownRotating != _rotating;
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
    if (_fadePhase != FadePhase::Idle && _fadeMedia && _shownRotating == _rotating) {
        // The picture on screen is the one wanted: either the target flipped back to what was
        // fading out, or another update arrived while the new picture was fading in. It fades
        // in from where it is, and the battery row dissolves to its new look alongside.
        StartSceneFade(FadePhase::In);
    }
    else {
        StartSceneFade(FadePhase::Out);
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

    // Restyling can put back the title's font from before it was fitted; fit it again.
    FitDeviceLabelFont(_ui.deviceLabel->accessibleName());

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
    // Laid out like the iOS sheet: the title is centred on the window with its first line at
    // the measured height, and a name too long for one line wraps onto a second before the
    // font shrinks.
    const int availableWidth = _titleMaximumWidth;
    const int left = (width() - availableWidth) / 2;

    // Line heights come from the laid-out text, not the primary font alone: CJK names are drawn
    // with a taller fallback font.
    const auto lineHeight = [](const QFontMetrics &metrics, const QString &line) {
        return metrics
            .boundingRect(QRect{0, 0, QWIDGETSIZE_MAX, QWIDGETSIZE_MAX}, Qt::TextSingleLine, line)
            .height();
    };

    auto font = _ui.deviceLabel->font();
    QString displayText = text;
    int firstLineHeight = 0;
    int textHeight = 0;
    for (int size = _deviceLabelMaximumPointSize;; --size) {
        font.setPointSize(size);
        const QFontMetrics metrics{font};
        firstLineHeight = lineHeight(metrics, text);
        if (metrics.horizontalAdvance(text) <= availableWidth) {
            textHeight = firstLineHeight;
            break;
        }
        const auto wrapped = metrics.boundingRect(
            QRect{0, 0, availableWidth, QWIDGETSIZE_MAX}, Qt::AlignHCenter | Qt::TextWordWrap,
            text);
        // At most two lines; a third would run into the picture.
        if (wrapped.width() <= availableWidth && wrapped.height() < firstLineHeight * 5 / 2) {
            textHeight = wrapped.height();
            break;
        }
        if (size <= _deviceLabelMinimumPointSize) {
            displayText = metrics.elidedText(text, Qt::ElideMiddle, availableWidth);
            firstLineHeight = textHeight = lineHeight(metrics, displayText);
            break;
        }
    }

    const int top = qRound(_titleFirstLineCentreY - firstLineHeight / 2.0);
    _ui.deviceLabel->setFont(font);
    _ui.deviceLabel->setGeometry(left, top, availableWidth, textHeight);
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
    SetAnimation(next, Core::AppleCP::Color::White);

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
    // The popup is created at startup but polished only when first shown, and that polish puts
    // back the title colour it had before any theme switch in between (e.g. Windows turning
    // light in the morning). Re-apply the current colours now that polishing is done.
    ApplyTheme();
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

// Used when the popup opens by itself (the case lid opened), so it doesn't pull focus away from
// the app in use, e.g. a full-screen game.
//
void MainWindow::ShowWithoutActivating()
{
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    Show();
    setAttribute(Qt::WA_ShowWithoutActivating, false);
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
