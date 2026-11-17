#pragma once

#include <QMediaPlayer>
#include <QString>
#include <QTimer>

#include "MainWindowPresentation.h"
#include "Widget/AnimationView.h"

namespace Gui {

// GUI-thread owner of the device animation. While a model is known but the popup is hidden the
// decoder is kept loaded and paused on its first frame, because loading the file costs roughly
// half a second. Opening the lid then starts the animation within a frame or two instead of
// leaving the poster on screen for the whole slide-in.
class AnimationPlayback : public QObject
{
    Q_OBJECT

public:
    explicit AnimationPlayback(Widget::AnimationView &view, QObject *parent = nullptr);
    ~AnimationPlayback() override;

    void SetAnimation(const AnimationPresentation &presentation);
    void SetActive(bool active);

Q_SIGNALS:
    void AttemptStarted();
    void FallbackActivated();

private:
    Widget::AnimationView &_view;
    QMediaPlayer _player{this};
    QTimer _watchdog{this};
    QTimer _retryTimer{this};
    QTimer _warmTimer{this};
    AnimationPresentation _presentation;
    QString _loadedResource;
    bool _active{false};
    bool _attemptRunning{false};
    bool _warming{false};
    int _retries{0};

    void StartAttempt();
    void BeginWarmUp();
    void EndWarmUp(bool keepWarm);
    void StopPlayback();
    void ReleasePlayer();
    void Fail(const QString &reason);
};

} // namespace Gui
