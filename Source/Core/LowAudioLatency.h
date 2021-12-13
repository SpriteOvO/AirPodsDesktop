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

#pragma once

#include <QMediaPlayer>
#include <QMediaPlaylist>

namespace Core::LowAudioLatency {

class Controller : public QObject
{
    Q_OBJECT

public:
    Controller(QObject *parent = nullptr);

Q_SIGNALS:
    void ControlSafely(bool enable);

private:
    QMediaPlayer _mediaPlayer;
    QMediaPlaylist _mediaPlaylist;

    void Control(bool enable);

    void OnError(QMediaPlayer::Error error);
};

} // namespace Core::LowAudioLatency
