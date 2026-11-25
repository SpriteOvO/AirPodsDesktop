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

#include <atomic>
#include <chrono>
#include <map>
#include <thread>

#include <QDialog>

#include "ui_MainWindow.h"

#include <QMediaPlayer>
#include <QPropertyAnimation>

#include "Utils.h"
#include "MainWindowPresentation.h"
#include "../Core/AirPods.h"
#include "../Core/Update.h"
#include "Base.h"
#include "Widget/Battery.h"
#include "AnimationPlayback.h"
#include "Widget/AnimationView.h"
#include "Widget/CloseButton.h"
#include "Widget/DeviceStill.h"
#include "Widget/FadeOverlay.h"

namespace Gui {

class BatteryInfo;

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void StartUpdateChecks();
    void Show();
    void ShowWithoutActivating();

    void UpdateState(const Core::AirPods::State &state);
    void Available();
    void Unavailable();
    void Disconnect();
    void Bind();
    void Unbind();
    void AskUserUpdate(const Core::Update::ReleaseInfo &releaseInfo);

Q_SIGNALS:
    void UpdateStateSafely(const Core::AirPods::State &state);
    void AvailableSafely();
    void UnavailableSafely();
    void DisconnectSafely();
    void BindSafely();
    void UnbindSafely();
    void ShowWithoutActivatingSafely();
    void HideSafely();
    bool VersionUpdateAvailableSafely(const Core::Update::ReleaseInfo &releaseInfo, bool silent);
    void SilentUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo);

private:
    // Proportioned like the iOS sheet: the device sits well below the title, the battery rings
    // below it, with room underneath.
    constexpr static QSize _windowSize{320, 330};
    constexpr static QSize _screenMargin{24, 24};
    constexpr static qreal _windowCornerRadius = 32.0;
    // Header proportions measured from the iOS sheet, scaled to the popup's width: a bold
    // ~17 pt title whose first line is centred 42 px from the top (a little below the close
    // button's centre), wrapping onto a second line beyond ~56 % of the width.
    constexpr static int _deviceLabelMaximumPointSize = 17;
    constexpr static int _deviceLabelMinimumPointSize = 12;
    constexpr static int _titleFirstLineCentreY = 42;
    constexpr static int _titleMaximumWidth = 180;

    Ui::MainWindow _ui;

    QPropertyAnimation _posAnimation{this, "pos"};
    // One fade for the whole scene, as on iOS: the picture and the battery row leave together,
    // then the new ones arrive together. The overlay carries the old row while the widgets
    // underneath are already rearranged.
    Widget::FadeOverlay *_batteryFade = new Widget::FadeOverlay{this};
    std::optional<MainWindowPresentation> _lastPresentation;
    // Like iOS: the turntable video plays while the pods are together (both in the case or both
    // out) and for headsets; with one pod taken out the view shows the pods and the case side by
    // side, still, each over its own battery ring.
    Widget::AnimationView *_animationView;
    AnimationPlayback *_playback;
    QVariantAnimation _sceneFade{this};
    enum class FadePhase { Idle, Out, In } _fadePhase{FadePhase::Idle};
    bool _rotating{true};       // what the scene asks for: the video, or else the still
    bool _shownRotating{true};  // what is on screen right now (lags behind during a fade-out)
    bool _fadeMedia{false};     // the picture takes part in the running fade
    bool _fadeBatteries{false}; // the battery row takes part in the running fade
    qreal _fadeFrom{1.0};       // opacity the running phase started from (a resumed fade)
    // Where the pods and the case sit in the current render, as fractions of its width.
    qreal _podsCenter{0.5};
    std::optional<qreal> _caseCenter;
    qreal _videoAspect{2.0};
    // The still scene's picture; models without a charging case have none.
    std::optional<Widget::DeviceStill> _still;
    int _podRingSpacing{0}; // the layout's own spacing between the left and the right ring
    QRect _podsRowGeometry;
    QRect _caseRowGeometry;
    QTimer *_autoHideTimer = new QTimer{this};
    // Caps how long an opened lid may keep the popup up, in case the state stops updating.
    QTimer *_lidSafetyTimer = new QTimer{this};
    Widget::CloseButton *_closeButton;
    Widget::Battery *_leftBattery = new Widget::Battery{this};
    Widget::Battery *_rightBattery = new Widget::Battery{this};
    Widget::Battery *_caseBattery = new Widget::Battery{this};

    Core::Update::AsyncChecker _updateChecker{[this](auto &&...args) {
        VersionUpdateAvailableSafely(std::forward<decltype(args)>(args)...);
    }};
    std::optional<Core::AirPods::Model> _cacheModel;
    QString _cacheResource;
    ButtonAction _buttonAction{ButtonAction::NoButton};
    MainWindowViewModel _viewModel;
    bool _isVisible{false};
    // Set when the lid opens with both pods inside; the popup then stays (also while the pods are
    // out) until the lid is seen closed, the device disconnects or the safety cap fires.
    bool _holdForOpenLid{false};
    // The last `start` passed to ControlAutoHideTimer: false while hidden, hovered or activated.
    bool _autoHideRequested{false};
    std::atomic<bool> _deviceQueryRunning{false};
    std::jthread _deviceQueryThread;

    void ChangeButtonAction(ButtonAction action);
    void SetAnimation(std::optional<Core::AirPods::Model> model, Core::AppleCP::Color color);
    void PlayAnimation();
    void StopAnimation();
    void ShowMedia(bool rotating);
    int MediaWidth(bool rotating) const;
    void StartSceneFade(FadePhase phase);
    void LayoutBatteryRows();
    QRect BatteryRowRect() const;
    void BindDevice();
    void ShowDeviceSelector(std::vector<Core::Bluetooth::Device> devices);
    void SetHoldForOpenLid(bool hold);
    void ControlAutoHideTimer(bool start);
    void VersionUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo, bool silent);
    void Repaint();
    void ApplyTheme();
    void FitDeviceLabelFont(const QString &text);

    void OnAppStateChanged(Qt::ApplicationState state);
    void OnPosMoveFinished();
    void OnAnimationClicked();
    void OnButtonClicked();

    void DoHide();
    void BeginShow(bool fromHidden);
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
    UTILS_QT_REGISTER_LANGUAGECHANGE(QDialog, [this] {
        _ui.retranslateUi(this);
        Repaint();
    });
};
} // namespace Gui
