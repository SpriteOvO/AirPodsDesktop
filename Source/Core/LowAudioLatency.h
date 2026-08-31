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

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <QAudioFormat>
#include <QAudioOutput>
#include <QList>
#include <QString>
#include <QTimer>

using namespace std::chrono_literals;

namespace Core::LowAudioLatency {

namespace Details {

std::vector<QAudioFormat> BuildSilenceFormatCandidates(
    const QList<int> &sampleSizes, const QList<QAudioFormat::SampleType> &sampleTypes,
    const QList<QAudioFormat::Endian> &byteOrders);
std::vector<uint8_t> CreateSilentFrame(const QAudioFormat &format);

} // namespace Details

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
    bool StartWhenReady();
    void ResetAudio();
    void Control(bool enable);
    void SetDeviceConnected(bool connected);

    void Start();
    void Stop();
    void CheckOutputDevice();
    void OnStateChanged(QAudio::State state);
};

} // namespace Core::LowAudioLatency
