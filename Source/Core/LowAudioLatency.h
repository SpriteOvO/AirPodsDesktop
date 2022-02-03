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

#include <memory>
#include <chrono>

#include <QTimer>
#include <QMediaPlayer>
#include <QMediaPlaylist>

using namespace std::chrono_literals;

namespace Core::LowAudioLatency {

class Controller : public QObject
{
    Q_OBJECT

public:
    Controller(QObject *parent = nullptr);

Q_SIGNALS:
    void ControlSafely(bool enable);

private:
    constexpr static inline auto kRetryInterval = 30s;

    std::unique_ptr<QMediaPlayer> _mediaPlayer;
    std::unique_ptr<QMediaPlaylist> _mediaPlaylist;
    QTimer _initTimer;
    bool _inited{false}, _enabled{false};

    bool Initialize();
    void Control(bool enable);

    void OnError(QMediaPlayer::Error error);
};

} // namespace Core::LowAudioLatency
