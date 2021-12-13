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

#include "../Logger.h"
#include "../Application.h"

namespace Core::LowAudioLatency {

Controller::Controller(QObject *parent) : QObject{parent}
{
    connect(
        &_mediaPlayer, qOverload<QMediaPlayer::Error>(&QMediaPlayer::error), this,
        &Controller::OnError);

    connect(this, &Controller::ControlSafely, this, &Controller::Control);

    _mediaPlaylist.addMedia(QUrl{"qrc:/Resource/Audio/Silence.mp3"});
    _mediaPlaylist.setPlaybackMode(QMediaPlaylist::Loop);
    _mediaPlayer.setPlaylist(&_mediaPlaylist);
}

void Controller::Control(bool enable)
{
    LOG(Info, "LowAudioLatency::Controller Control: {}", enable);

    if (enable) {
        _mediaPlayer.play();
    }
    else {
        _mediaPlayer.stop();
    }
}

void Controller::OnError(QMediaPlayer::Error error)
{
    LOG(Warn, "LowAudioLatency::Controller error: {}", error);
}

} // namespace Core::LowAudioLatency
