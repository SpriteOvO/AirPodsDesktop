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
    _warmTimer.setSingleShot(true);
    _warmTimer.setInterval(5000);

    connect(&_watchdog, &QTimer::timeout, this, [this] {
        Fail(QStringLiteral("No video frame received for 3 seconds"));
    });
    connect(&_retryTimer, &QTimer::timeout, this, &AnimationPlayback::StartAttempt);
    connect(&_warmTimer, &QTimer::timeout, this, [this] {
        if (_warming) {
            LOG(Warn, "Animation preload received no frame for 5 seconds: '{}'",
                _presentation.resource.toStdString());
            EndWarmUp(false);
        }
    });
    connect(&_view, &Widget::AnimationView::FramePresented, this, [this] {
        if (_warming && !_active) {
            EndWarmUp(true);
            return;
        }
        if (_active && _attemptRunning) {
            _watchdog.start();
        }
    });
    connect(
        &_player, &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error error, const QString &errorString) {
            if (error == QMediaPlayer::NoError) {
                return;
            }
            if (_warming && !_active) {
                LOG(Warn, "Animation preload failed: '{}', reason: '{}'",
                    _presentation.resource.toStdString(), errorString.toStdString());
                EndWarmUp(false);
                return;
            }
            Fail(QStringLiteral("error %1: %2").arg(static_cast<int>(error)).arg(errorString));
        });
    connect(&_player, &QMediaPlayer::mediaStatusChanged, this, [this](auto status) {
        if (status != QMediaPlayer::InvalidMedia) {
            return;
        }
        if (_warming && !_active) {
            LOG(Warn, "Animation preload rejected its media: '{}'",
                _presentation.resource.toStdString());
            EndWarmUp(false);
            return;
        }
        if (!_active || !_attemptRunning) {
            return;
        }
        Fail(_player.errorString());
    });
}

AnimationPlayback::~AnimationPlayback()
{
    ReleasePlayer();
    _player.setVideoSink(nullptr);
}

void AnimationPlayback::SetAnimation(const AnimationPresentation &presentation)
{
    ReleasePlayer();
    _presentation = presentation;
    _retries = 0;
    _view.SetRemoveEnclosedBackground(presentation.removeEnclosedBackground);
    // An empty presentation (no device bound or connected) has no fallback artwork;
    // loading an empty path would only make QImage log "No file name specified".
    const auto fallbackResource = presentation.FallbackResource();
    _view.SetFallbackImage(fallbackResource.isEmpty() ? QImage{} : QImage{fallbackResource});

    if (_presentation.resource.isEmpty()) {
        return;
    }
    if (_active) {
        StartAttempt();
    }
    else {
        BeginWarmUp();
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
        StopPlayback();
    }
}

void AnimationPlayback::StartAttempt()
{
    if (!_active || _presentation.resource.isEmpty()) {
        return;
    }

    // A popup that opens during warm-up takes over the decoder that is already running.
    _warming = false;
    _warmTimer.stop();

    _attemptRunning = true;
    _view.SetPlaybackEnabled(true);
    _watchdog.start();
    Q_EMIT AttemptStarted();

    if (_loadedResource != _presentation.resource) {
        _player.stop();
        _loadedResource = _presentation.resource;
        _player.setSource(QUrl{_presentation.resource});
        // setSource can synchronously report a missing backend or invalid resource.
        if (!_attemptRunning) {
            return;
        }
    }
    else {
        _player.setPosition(0);
    }
    _player.play();
}

void AnimationPlayback::BeginWarmUp()
{
    if (_presentation.resource.isEmpty()) {
        return;
    }

    // The popup is hidden, but frame delivery still drives the decoder to its first frame. Without
    // a consumer the backend defers that work, which is why `play()` alone is not enough.
    _warming = true;
    _warmTimer.start();
    _view.SetPlaybackEnabled(true);

    _player.stop();
    _loadedResource = _presentation.resource;
    _player.setSource(QUrl{_presentation.resource});
    if (_warming) {
        _player.play();
    }
}

void AnimationPlayback::EndWarmUp(bool keepWarm)
{
    if (!_warming) {
        return;
    }
    _warming = false;
    _warmTimer.stop();
    _view.SetPlaybackEnabled(false);

    if (!keepWarm) {
        ReleasePlayer();
        return;
    }

    // Park the decoder on the first frame so the next activation is a plain `play()`.
    _player.pause();
    _player.setPosition(0);
    LOG(Trace, "Animation preloaded: '{}'", _presentation.resource.toStdString());
}

void AnimationPlayback::StopPlayback()
{
    _attemptRunning = false;
    _watchdog.stop();
    _retryTimer.stop();
    _view.SetPlaybackEnabled(false);

    if (_loadedResource.isEmpty()) {
        return;
    }
    // Keep the loaded source and its decoder alive so the next open resumes immediately.
    _player.pause();
    _player.setPosition(0);
}

void AnimationPlayback::ReleasePlayer()
{
    _warming = false;
    _warmTimer.stop();
    _attemptRunning = false;
    _watchdog.stop();
    _retryTimer.stop();
    _view.SetPlaybackEnabled(false);
    _player.stop();
    _player.setSource({});
    _loadedResource.clear();
}

void AnimationPlayback::Fail(const QString &reason)
{
    if (!_active || !_attemptRunning) {
        return;
    }
    LOG(Warn, "Animation playback failed: '{}', retry: {}, reason: '{}'",
        _presentation.resource.toStdString(), _retries, reason.toStdString());
    ReleasePlayer();
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
