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

#include "LowAudioLatency.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QUrl>

#include "../Logger.h"

namespace Core::LowAudioLatency {

Controller::Controller(QObject *parent) : QObject{parent}
{
    connect(this, &Controller::ControlSafely, this, &Controller::Control);
    connect(this, &Controller::SetDeviceConnectedSafely, this, &Controller::SetDeviceConnected);

    _initTimer.setInterval(kRetryInterval);
    _initTimer.callOnTimeout([this] {
        if (!_enabled || !_deviceConnected) {
            _initTimer.stop();
            return;
        }
        StartWhenReady();
    });
}

Controller::~Controller()
{
    _initTimer.stop();
    ResetAudio();
}

bool Controller::Initialize()
{
    // issue #20
    //
    // Constructing `QMediaPlayer` when no audio output device is enabled will cause `play` to
    // continually raise errors and is unrecoverable until the device list changes.
    if (QMediaDevices::audioOutputs().empty()) {
        LOG(Warn, "LowAudioLatency: Try to init, but no audio output device is enabled.");
        return false;
    }

    _mediaPlayer = std::make_unique<QMediaPlayer>();
    _audioOutput = std::make_unique<QAudioOutput>();

    connect(
        _mediaPlayer.get(), &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error error, const QString &) { OnError(error); });

    _mediaPlayer->setAudioOutput(_audioOutput.get());
    _mediaPlayer->setSource(QUrl{"qrc:/Resource/Audio/Silence.mp3"});
    _mediaPlayer->setLoops(QMediaPlayer::Infinite);

    _inited = true;
    LOG(Info, "LowAudioLatency: Compatible media backend initialized.");
    return true;
}

bool Controller::StartWhenReady()
{
    if (!_inited && !Initialize()) {
        if (!_initTimer.isActive()) {
            _initTimer.start();
        }
        return false;
    }

    _initTimer.stop();
    Start();
    return true;
}

void Controller::ResetAudio()
{
    Stop();
    _mediaPlayer.reset();
    _audioOutput.reset();
    _inited = false;
}

void Controller::Control(bool enable)
{
    LOG(Info, "LowAudioLatency::Controller Control: {}, _inited: {}", enable, _inited);

    _enabled = enable;

    if (enable) {
        if (!_deviceConnected) {
            LOG(Info, "LowAudioLatency: Waiting for the bound device to connect.");
            return;
        }
        StartWhenReady();
    }
    else {
        _initTimer.stop();
        ResetAudio();
    }
}

void Controller::SetDeviceConnected(bool connected)
{
    if (_deviceConnected == connected) {
        return;
    }

    _deviceConnected = connected;
    if (!_enabled) {
        return;
    }

    if (!connected) {
        LOG(Info, "LowAudioLatency: Bound device disconnected; releasing the audio stream.");
        _initTimer.stop();
        ResetAudio();
        return;
    }

    LOG(Info, "LowAudioLatency: Bound device connected; starting the compatible media stream.");
    StartWhenReady();
}

void Controller::Start()
{
    if (!_inited || !_enabled || !_deviceConnected || !_mediaPlayer) {
        return;
    }

    if (_mediaPlayer->playbackState() != QMediaPlayer::PlayingState) {
        _mediaPlayer->play();
    }
}

void Controller::Stop()
{
    if (_mediaPlayer) {
        _mediaPlayer->stop();
    }
}

void Controller::OnError(QMediaPlayer::Error error)
{
    if (!_enabled) {
        return;
    }

    LOG(Warn, "LowAudioLatency::Controller error: {}. Reinit later.", error);

    // Do not destroy QMediaPlayer while it is emitting its error signal.
    const auto *failedPlayer = _mediaPlayer.get();
    QTimer::singleShot(0, this, [this, failedPlayer] {
        if (!_enabled || !_deviceConnected || _mediaPlayer.get() != failedPlayer) {
            return;
        }
        ResetAudio();
        _initTimer.start();
    });
}

} // namespace Core::LowAudioLatency
