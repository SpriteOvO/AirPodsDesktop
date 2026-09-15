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
#include "Widget/DeviceImage.h"
#include "Widget/FadeOverlay.h"

namespace Gui {

class CloseButton;
class BatteryInfo;

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void StartUpdateChecks();
    void Show();

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
    void ShowSafely();
    void HideSafely();
    bool VersionUpdateAvailableSafely(const Core::Update::ReleaseInfo &releaseInfo, bool silent);
    void SilentUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo);

private:
    constexpr static QSize _windowSize{320, 300};
    constexpr static QSize _screenMargin{24, 24};
    constexpr static qreal _windowCornerRadius = 32.0;
    constexpr static int _deviceLabelMaximumPointSize = 18;
    constexpr static int _deviceLabelMinimumPointSize = 12;

    Ui::MainWindow _ui;

    QPropertyAnimation _posAnimation{this, "pos"};
    // One fade for the whole scene: picture and battery row leave together, then the new ones
    // arrive together. The overlay carries the old battery row while the widgets underneath are
    // already rearranged.
    Widget::FadeOverlay *_batteryFade = new Widget::FadeOverlay{this};
    std::optional<MainWindowPresentation> _lastPresentation;
    // Turntable video while both pods rest in the open case; a still render (cross-faded from
    // the last frame) as soon as one is taken out.
    Widget::AnimationView *_animationView;
    AnimationPlayback *_playback;
    Widget::DeviceImage *_deviceImage;
    bool _rotating{false};  // what the scene asks for
    bool _shownView{false}; // which view is on screen right now (may lag during a fade)
    QVariantAnimation _sceneFade{this};
    enum class FadePhase { Idle, Out, In } _fadePhase{FadePhase::Idle};
    bool _fadeMedia{false};     // the picture takes part in the running fade
    bool _fadeBatteries{false}; // the battery row takes part in the running fade
    qreal _fadeFrom{1.0};       // opacity the running phase started from (a resumed fade)
    Widget::DeviceImage::Arrangement _pendingArrangement{Widget::DeviceImage::Arrangement::Spread};
    // Both pods out: the render keeps turning this long, then the case leaves the picture.
    QTimer *_podsOnlyTimer = new QTimer{this};
    QRect _podsRowGeometry;
    QTimer *_autoHideTimer = new QTimer{this};
    // Caps how long an opened lid may keep the popup up, in case the state stops updating.
    QTimer *_lidSafetyTimer = new QTimer{this};
    CloseButton *_closeButton;
    Widget::Battery *_leftBattery = new Widget::Battery{this};
    Widget::Battery *_rightBattery = new Widget::Battery{this};
    Widget::Battery *_caseBattery = new Widget::Battery{this};

    Core::Update::AsyncChecker _updateChecker{[this](auto &&...args) {
        VersionUpdateAvailableSafely(std::forward<decltype(args)>(args)...);
    }};
    std::optional<Core::AirPods::Model> _cacheModel;
    ButtonAction _buttonAction{ButtonAction::NoButton};
    MainWindowViewModel _viewModel;
    bool _isVisible{false};
    // Set when the lid opens with both pods inside; the popup then stays (also while the pods are
    // out) until the lid is seen closed, the device disconnects or the safety cap fires.
    bool _holdForOpenLid{false};
    std::atomic<bool> _deviceQueryRunning{false};
    std::jthread _deviceQueryThread;

    void ChangeButtonAction(ButtonAction action);
    void SetAnimation(std::optional<Core::AirPods::Model> model);
    void PlayAnimation();
    void StopAnimation();
    void ShowMedia(bool rotating);
    void StartSceneFade(FadePhase phase);
    void SetMediaOpacity(bool rotatingView, qreal opacity);
    qreal MediaOpacity(bool rotatingView) const;
    void ApplyScene(Scene scene);
    void BindDevice();
    void ShowDeviceSelector(std::vector<Core::Bluetooth::Device> devices);
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
