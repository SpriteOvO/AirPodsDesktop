#pragma once

#include <QMediaPlayer>
#include <QTimer>

#include "MainWindowPresentation.h"
#include "Widget/AnimationView.h"

namespace Gui {

// GUI-thread owner of one visible playback session. Hidden sessions pause the video and keep it
// loaded, so showing it again does not pay the cost of reopening the file.
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
    AnimationPresentation _presentation;
    bool _active{false};
    bool _attemptRunning{false};
    bool _loaded{false};
    int _retries{0};

    void StartAttempt();
    void Resume();
    void Pause();
    void Stop();
    void Fail(const QString &reason);
};

} // namespace Gui
