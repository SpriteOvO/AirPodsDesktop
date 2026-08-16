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
#include <QAudioOutput>
#include <QString>

using namespace std::chrono_literals;

namespace Core::LowAudioLatency {

class SilenceDevice;

class Controller : public QObject
{
    Q_OBJECT

public:
    Controller(QObject *parent = nullptr);
    ~Controller();

Q_SIGNALS:
    void ControlSafely(bool enable);
    void SetDeviceConnectedSafely(bool connected);

private:
    constexpr static inline auto kRetryInterval = 30s;
    constexpr static inline auto kDeviceCheckInterval = 5s;
    std::unique_ptr<QAudioOutput> _audioOutput;
    std::unique_ptr<SilenceDevice> _silenceDevice;
    QTimer _initTimer;
    QTimer _deviceCheckTimer;
    QString _deviceName;
    bool _inited{false}, _enabled{false}, _deviceConnected{false};

    bool Initialize();
    void ResetAudio();
    void Control(bool enable);
    void SetDeviceConnected(bool connected);

    void Start();
    void Stop();
    void CheckOutputDevice();
    void OnStateChanged(QAudio::State state);
};

} // namespace Core::LowAudioLatency
