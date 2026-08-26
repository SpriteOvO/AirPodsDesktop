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
#include <vector>

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
    explicit SilenceDevice(const QAudioFormat &format, QObject *parent = nullptr)
        : QIODevice{parent}
    {
        const auto bytesPerSample = std::max(1, format.sampleSize() / 8);
        const auto bytesPerFrame = std::max(1, bytesPerSample * format.channelCount());
        _silentFrame.assign(static_cast<size_t>(bytesPerFrame), 0);

        // Integer PCM is biased around its midpoint when unsigned. A zero-filled unsigned
        // buffer is full-scale negative DC and can be heard as noise on negotiated formats.
        if (format.sampleType() == QAudioFormat::UnSignedInt) {
            _zeroFilled = false;
            const auto signByte =
                format.byteOrder() == QAudioFormat::LittleEndian ? bytesPerSample - 1 : 0;
            for (auto channel = 0; channel < format.channelCount(); ++channel) {
                _silentFrame[static_cast<size_t>(channel * bytesPerSample + signByte)] = 0x80;
            }
        }
    }

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
        if (maxSize <= 0) {
            return 0;
        }

        if (_zeroFilled) {
            std::memset(data, 0, static_cast<size_t>(maxSize));
            return maxSize;
        }

        auto remaining = static_cast<size_t>(maxSize);
        auto *output = reinterpret_cast<uint8_t *>(data);
        while (remaining > 0) {
            const auto chunk = std::min(remaining, _silentFrame.size() - _frameOffset);
            std::memcpy(output, _silentFrame.data() + _frameOffset, chunk);
            output += chunk;
            remaining -= chunk;
            _frameOffset = (_frameOffset + chunk) % _silentFrame.size();
        }
        return maxSize;
    }

    qint64 writeData(const char *, qint64) override
    {
        return -1;
    }

private:
    constexpr static inline qint64 kBufferHint = 4096;
    std::vector<uint8_t> _silentFrame;
    size_t _frameOffset{0};
    bool _zeroFilled{true};
};

Controller::Controller(QObject *parent) : QObject{parent}
{
    connect(this, &Controller::ControlSafely, this, &Controller::Control);
    connect(this, &Controller::SetDeviceConnectedSafely, this, &Controller::SetDeviceConnected);

    _initTimer.callOnTimeout([this] {
        if (Initialize()) {
            _initTimer.stop();
            Start();
            _deviceCheckTimer.start();
        }
    });

    _deviceCheckTimer.setInterval(kDeviceCheckInterval);
    _deviceCheckTimer.callOnTimeout([this] { CheckOutputDevice(); });
}

Controller::~Controller()
{
    Stop();
}

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
    const auto format = CreateSilenceFormat(device);
    if (!format.isValid()) {
        LOG(Warn, "LowAudioLatency: Default output device has no valid PCM format.");
        return false;
    }

    _silenceDevice = std::make_unique<SilenceDevice>(format);
    _audioOutput = std::make_unique<QAudioOutput>(device, format, this);
    _deviceName = device.deviceName();

    LOG(Info, "LowAudioLatency: Format: {} Hz, {} channel(s), {} bit, sample type {}.",
        format.sampleRate(), format.channelCount(), format.sampleSize(), format.sampleType());

    connect(_audioOutput.get(), &QAudioOutput::stateChanged, this, &Controller::OnStateChanged);

    _inited = true;

    LOG(Info, "LowAudioLatency: Init successful. _enabled: {}", _enabled);

    return true;
}

void Controller::ResetAudio()
{
    Stop();
    _audioOutput.reset();
    _silenceDevice.reset();
    _deviceName.clear();
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
        if (!_inited && !Initialize()) {
            _initTimer.start(kRetryInterval);
            return;
        }
        Start();
        _deviceCheckTimer.start();
    }
    else {
        _initTimer.stop();
        _deviceCheckTimer.stop();
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
        _deviceCheckTimer.stop();
        ResetAudio();
        return;
    }

    LOG(Info, "LowAudioLatency: Bound device connected; starting the audio stream.");
    if (!_inited && !Initialize()) {
        _initTimer.start(kRetryInterval);
        return;
    }
    Start();
    _deviceCheckTimer.start();
}

void Controller::Start()
{
    if (!_inited || !_enabled || !_deviceConnected || !_audioOutput || !_silenceDevice) {
        return;
    }

    if (_audioOutput->state() == QAudio::ActiveState || _audioOutput->state() == QAudio::IdleState)
    {
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

void Controller::CheckOutputDevice()
{
    if (!_enabled || !_deviceConnected) {
        return;
    }

    const auto device = QAudioDeviceInfo::defaultOutputDevice();
    if (!device.isNull() && device.deviceName() == _deviceName) {
        return;
    }

    LOG(Info, "LowAudioLatency: Default output device changed; recreating the silence stream.");
    ResetAudio();
    if (!Initialize()) {
        _deviceCheckTimer.stop();
        _initTimer.start(kRetryInterval);
        return;
    }
    Start();
}

void Controller::OnStateChanged(QAudio::State state)
{
    if (!_enabled) {
        return;
    }

    if (state == QAudio::StoppedState && _audioOutput && _audioOutput->error() != QAudio::NoError) {
        LOG(Warn, "LowAudioLatency::Controller error: {}. Reinit later.", _audioOutput->error());
        // Do not destroy QAudioOutput while it is emitting stateChanged.
        const auto *failedOutput = _audioOutput.get();
        QTimer::singleShot(0, this, [this, failedOutput] {
            if (!_enabled || !_deviceConnected || _audioOutput.get() != failedOutput) {
                return;
            }
            ResetAudio();
            _initTimer.start(kRetryInterval);
        });
    }
}

} // namespace Core::LowAudioLatency
