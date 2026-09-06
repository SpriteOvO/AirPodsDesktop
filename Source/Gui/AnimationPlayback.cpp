#include "AnimationPlayback.h"

#include "../Utils.h"

namespace Gui {

AnimationPlayback::AnimationPlayback(Widget::AnimationView &view, QObject *parent)
    : QObject{parent}, _view{view}
{
    _player.setVideoSink(_view.VideoSink());
    _player.setLoops(QMediaPlayer::Infinite);
    _view.SetPlaybackEnabled(false);
    _watchdog.setSingleShot(true);
    _watchdog.setInterval(3000);
    _retryTimer.setSingleShot(true);
    _retryTimer.setInterval(250);

    connect(&_watchdog, &QTimer::timeout, this, [this] {
        Fail(QStringLiteral("No video frame received for 3 seconds"));
    });
    connect(&_retryTimer, &QTimer::timeout, this, &AnimationPlayback::StartAttempt);
    connect(&_view, &Widget::AnimationView::FramePresented, this, [this] {
        if (_active && _attemptRunning) {
            _watchdog.start();
        }
    });
    connect(
        &_player, &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error error, const QString &errorString) {
            if (error != QMediaPlayer::NoError) {
                Fail(QStringLiteral("error %1: %2").arg(static_cast<int>(error)).arg(errorString));
            }
        });
    connect(&_player, &QMediaPlayer::mediaStatusChanged, this, [this](auto status) {
        if (!_active || !_attemptRunning) {
            return;
        }
        if (status == QMediaPlayer::InvalidMedia) {
            Fail(_player.errorString());
        }
    });
}

AnimationPlayback::~AnimationPlayback()
{
    Stop();
    _player.setVideoSink(nullptr);
}

void AnimationPlayback::SetAnimation(const AnimationPresentation &presentation)
{
    Stop();
    _presentation = presentation;
    _retries = 0;
    _view.SetRemoveEnclosedBackground(presentation.removeEnclosedBackground);
    _view.SetFallbackImage(QImage{presentation.FallbackResource()});
    if (_active) {
        StartAttempt();
    }
}

void AnimationPlayback::SetActive(bool active)
{
    if (_active == active) {
        return;
    }
    _active = active;
    if (active) {
        _retries = 0;
        StartAttempt();
    }
    else {
        Stop();
    }
}

void AnimationPlayback::StartAttempt()
{
    if (!_active || _presentation.resource.isEmpty()) {
        return;
    }
    _attemptRunning = true;
    _view.SetPlaybackEnabled(true);
    _watchdog.start();
    Q_EMIT AttemptStarted();
    _player.setSource(QUrl{_presentation.resource});
    // setSource can synchronously report a missing backend or invalid resource.
    if (_attemptRunning) {
        _player.play();
    }
}

void AnimationPlayback::Stop()
{
    _attemptRunning = false;
    _watchdog.stop();
    _retryTimer.stop();
    _view.SetPlaybackEnabled(false);
    _player.stop();
    _player.setSource({});
}

void AnimationPlayback::Fail(const QString &reason)
{
    if (!_active || !_attemptRunning) {
        return;
    }
    LOG(Warn, "Animation playback failed: '{}', retry: {}, reason: '{}'",
        _presentation.resource.toStdString(), _retries, reason.toStdString());
    Stop();
    // The poster remains visible even if no frame could be decoded. One retry per activation
    // prevents invalid media from spinning forever; a later reopen/model change gets a fresh try.
    if (_retries++ == 0) {
        _retryTimer.start();
    }
    else {
        Q_EMIT FallbackActivated();
    }
}

} // namespace Gui
