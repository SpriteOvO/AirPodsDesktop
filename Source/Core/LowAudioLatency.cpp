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

#include <QMediaPlayer>
#include <QMediaPlaylist>

#include "../Logger.h"
#include "../Application.h"

namespace Core::LowAudioLatency {

class Controller : public QObject
{
    Q_OBJECT

public:
    static auto &GetInstance()
    {
        static Controller i;
        return i;
    }

    Controller(QObject *parent = nullptr) : QObject{parent}
    {
        connect(
            &_mediaPlayer, qOverload<QMediaPlayer::Error>(&QMediaPlayer::error), this,
            &Controller::OnError);

        connect(this, &Controller::ControlSafety, this, &Controller::Control);

        _mediaPlaylist.addMedia(QUrl{"qrc:/Resource/Audio/Silence.mp3"});
        _mediaPlaylist.setPlaybackMode(QMediaPlaylist::Loop);
        _mediaPlayer.setPlaylist(&_mediaPlaylist);
    }

Q_SIGNALS:
    void ControlSafety(bool enable);

private:
    QMediaPlayer _mediaPlayer;
    QMediaPlaylist _mediaPlaylist;

    void Control(bool enable)
    {
        SPDLOG_INFO("LowAudioLatency::Controller Control: {}", enable);

        if (enable) {
            _mediaPlayer.play();
        }
        else {
            _mediaPlayer.stop();
        }
    }

    void OnError(QMediaPlayer::Error error)
    {
        SPDLOG_WARN("LowAudioLatency::Controller error: {}", error);
    }
};

void Control(bool enable)
{
    Controller::GetInstance().ControlSafety(enable);
}
} // namespace Core::LowAudioLatency

#include "LowAudioLatency.moc"
