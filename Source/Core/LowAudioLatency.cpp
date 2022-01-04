//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
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

#include <thread>

#include <QAudioDeviceInfo>

#include "../Logger.h"
#include "../Application.h"

using namespace std::chrono_literals;

namespace Core::LowAudioLatency {

Controller::Controller(QObject *parent) : QObject{parent}
{
    connect(this, &Controller::ControlSafely, this, &Controller::Control);

    if (!Initialize()) {
        // retry later
        _initTimer.callOnTimeout([this] {
            if (Initialize()) {
                _initTimer.stop();
            }
        });
        _initTimer.start(30s);
    }
}

bool Controller::Initialize()
{
    // issue #20
    //
    // Constructing `QMediaPlayer` when no audio output device is enabled will cause `play` to
    // continually raise errors and is unrecoverable.
    if (QAudioDeviceInfo::availableDevices(QAudio::AudioOutput).size() == 0) {
        LOG(Warn, "LowAudioLatency: Try to init, but no audio output device is enabled.");
        return false;
    }

    _mediaPlayer = std::make_unique<QMediaPlayer>();
    _mediaPlaylist = std::make_unique<QMediaPlaylist>();

    connect(
        _mediaPlayer.get(), qOverload<QMediaPlayer::Error>(&QMediaPlayer::error), this,
        &Controller::OnError);

    _mediaPlaylist->addMedia(QUrl{"qrc:/Resource/Audio/Silence.mp3"});
    _mediaPlaylist->setPlaybackMode(QMediaPlaylist::Loop);
    _mediaPlayer->setPlaylist(_mediaPlaylist.get());

    _inited = true;

    LOG(Info, "LowAudioLatency: Init successful. _enabled: {}", _enabled);

    if (_enabled) {
        Control(true);
    }

    return true;
}

void Controller::Control(bool enable)
{
    LOG(Info, "LowAudioLatency::Controller Control: {}, _inited: {}", enable, _inited);

    if (_inited) {
        if (enable) {
            _mediaPlayer->play();
        }
        else {
            _mediaPlayer->stop();
        }
    }

    _enabled = enable;
}

void Controller::OnError(QMediaPlayer::Error error)
{
    LOG(Warn, "LowAudioLatency::Controller error: {}", error);
}

} // namespace Core::LowAudioLatency
