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

#include <algorithm>
#include <cstring>

#include <QAudioDeviceInfo>
#include <QIODevice>

#include "../Logger.h"

namespace Core::LowAudioLatency {
namespace {

QAudioFormat CreateSilenceFormat(const QAudioDeviceInfo &device)
{
    QAudioFormat format;
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    if (!device.isFormatSupported(format)) {
        format = device.nearestFormat(format);
    }

    return format;
}

} // namespace

class SilenceDevice final : public QIODevice
{
public:
    explicit SilenceDevice(QObject *parent = nullptr) : QIODevice{parent} {}

    void Start()
    {
        open(QIODevice::ReadOnly);
    }

    void Stop()
    {
        close();
    }

    bool isSequential() const override
    {
        return true;
    }

    qint64 bytesAvailable() const override
    {
        return kBufferHint + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        std::memset(data, 0, static_cast<size_t>(std::max<qint64>(0, maxSize)));
        return maxSize;
    }

    qint64 writeData(const char *, qint64) override
    {
        return -1;
    }

private:
    constexpr static inline qint64 kBufferHint = 4096;
};

Controller::Controller(QObject *parent) : QObject{parent}
{
    connect(this, &Controller::ControlSafely, this, &Controller::Control);

    _initTimer.callOnTimeout([this] {
        if (Initialize()) {
            _initTimer.stop();
            if (_enabled) {
                Start();
            }
        }
    });

    if (!Initialize()) {
        _initTimer.start(kRetryInterval);
    }
}

Controller::~Controller() = default;

bool Controller::Initialize()
{
    // issue #20
    //
    // Constructing audio output when no audio output device is enabled will cause repeated
    // playback errors and is unrecoverable until the device list changes.
    const auto devices = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    if (devices.empty()) {
        LOG(Warn, "LowAudioLatency: Try to init, but no audio output device is enabled.");
        return false;
    }

    const auto device = QAudioDeviceInfo::defaultOutputDevice();
    _silenceDevice = std::make_unique<SilenceDevice>();
    _audioOutput = std::make_unique<QAudioOutput>(device, CreateSilenceFormat(device), this);

    connect(_audioOutput.get(), &QAudioOutput::stateChanged, this, &Controller::OnStateChanged);

    _inited = true;

    LOG(Info, "LowAudioLatency: Init successful. _enabled: {}", _enabled);

    return true;
}

void Controller::Control(bool enable)
{
    LOG(Info, "LowAudioLatency::Controller Control: {}, _inited: {}", enable, _inited);

    _enabled = enable;

    if (enable) {
        if (!_inited && !_initTimer.isActive()) {
            _initTimer.start(kRetryInterval);
        }
        Start();
    }
    else {
        Stop();
    }
}

void Controller::Start()
{
    if (!_inited || !_enabled || !_audioOutput || !_silenceDevice) {
        return;
    }

    if (_audioOutput->state() == QAudio::ActiveState || _audioOutput->state() == QAudio::IdleState) {
        return;
    }

    _silenceDevice->Start();
    _audioOutput->start(_silenceDevice.get());
}

void Controller::Stop()
{
    if (_audioOutput) {
        _audioOutput->stop();
    }
    if (_silenceDevice) {
        _silenceDevice->Stop();
    }
}

void Controller::OnStateChanged(QAudio::State state)
{
    if (!_enabled) {
        return;
    }

    if (state == QAudio::StoppedState && _audioOutput && _audioOutput->error() != QAudio::NoError) {
        LOG(Warn, "LowAudioLatency::Controller error: {}. Reinit later.", _audioOutput->error());
        Stop();
        _audioOutput.reset();
        _silenceDevice.reset();
        _inited = false;
        _initTimer.start(kRetryInterval);
    }
}

} // namespace Core::LowAudioLatency
