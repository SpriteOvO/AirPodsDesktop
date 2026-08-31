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
#include <array>
#include <cstring>
#include <vector>

#include <QAudioDeviceInfo>
#include <QIODevice>

#include "../Logger.h"

namespace Core::LowAudioLatency {

namespace Details {

std::vector<QAudioFormat> BuildSilenceFormatCandidates(
    const QList<int> &sampleSizes, const QList<QAudioFormat::SampleType> &sampleTypes,
    const QList<QAudioFormat::Endian> &byteOrders)
{
    auto orderedSampleSizes = sampleSizes;
    std::sort(orderedSampleSizes.begin(), orderedSampleSizes.end());
    orderedSampleSizes.erase(
        std::unique(orderedSampleSizes.begin(), orderedSampleSizes.end()),
        orderedSampleSizes.end());

    constexpr std::array kSampleTypePreference{
        QAudioFormat::SignedInt,
        QAudioFormat::UnSignedInt,
        QAudioFormat::Float,
    };
    constexpr std::array kByteOrderPreference{
        QAudioFormat::LittleEndian,
        QAudioFormat::BigEndian,
    };

    std::vector<QAudioFormat> candidates;
    for (const auto sampleSize : orderedSampleSizes) {
        for (const auto sampleType : kSampleTypePreference) {
            if (!sampleTypes.contains(sampleType)) {
                continue;
            }
            for (const auto byteOrder : kByteOrderPreference) {
                if (!byteOrders.contains(byteOrder)) {
                    continue;
                }

                QAudioFormat format;
                format.setSampleRate(8000);
                format.setChannelCount(1);
                format.setSampleSize(sampleSize);
                format.setCodec("audio/pcm");
                format.setByteOrder(byteOrder);
                format.setSampleType(sampleType);
                candidates.push_back(format);
            }
        }
    }
    return candidates;
}

std::vector<uint8_t> CreateSilentFrame(const QAudioFormat &format)
{
    const auto bitsPerSample = std::max(1, format.sampleSize());
    const auto bytesPerSample = std::max(1, (bitsPerSample + 7) / 8);
    const auto channelCount = std::max(1, format.channelCount());
    std::vector<uint8_t> frame(static_cast<size_t>(bytesPerSample * channelCount), 0);

    if (format.sampleType() != QAudioFormat::UnSignedInt) {
        return frame;
    }

    const auto midpointBit = bitsPerSample - 1;
    const auto byteFromLeastSignificant = midpointBit / 8;
    const auto midpointMask = static_cast<uint8_t>(1u << (midpointBit % 8));
    const auto midpointByte = format.byteOrder() == QAudioFormat::LittleEndian
                                  ? byteFromLeastSignificant
                                  : bytesPerSample - byteFromLeastSignificant - 1;
    for (auto channel = 0; channel < channelCount; ++channel) {
        frame[static_cast<size_t>(channel * bytesPerSample + midpointByte)] = midpointMask;
    }
    return frame;
}

} // namespace Details

namespace {

QAudioFormat CreateSilenceFormat(const QAudioDeviceInfo &device)
{
    const auto candidates = Details::BuildSilenceFormatCandidates(
        device.supportedSampleSizes(), device.supportedSampleTypes(), device.supportedByteOrders());
    for (const auto &candidate : candidates) {
        if (device.isFormatSupported(candidate)) {
            return candidate;
        }
    }

    QAudioFormat fallback;
    fallback.setSampleRate(8000);
    fallback.setChannelCount(1);
    fallback.setSampleSize(16);
    fallback.setCodec("audio/pcm");
    fallback.setByteOrder(QAudioFormat::LittleEndian);
    fallback.setSampleType(QAudioFormat::SignedInt);
    return device.nearestFormat(fallback);
}

} // namespace

class SilenceDevice final : public QIODevice
{
public:
    explicit SilenceDevice(const QAudioFormat &format, QObject *parent = nullptr)
        : QIODevice{parent}, _silentFrame{Details::CreateSilentFrame(format)}
    {
        _zeroFilled = std::all_of(
            _silentFrame.cbegin(), _silentFrame.cend(), [](const auto byte) { return byte == 0; });
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

    _initTimer.setInterval(kRetryInterval);
    _initTimer.callOnTimeout([this] {
        if (!_enabled || !_deviceConnected) {
            _initTimer.stop();
            return;
        }
        StartWhenReady();
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

bool Controller::StartWhenReady()
{
    if (!_inited && !Initialize()) {
        _deviceCheckTimer.stop();
        if (!_initTimer.isActive()) {
            _initTimer.start();
        }
        return false;
    }
    _initTimer.stop();
    Start();
    _deviceCheckTimer.start();
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
        StartWhenReady();
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
    StartWhenReady();
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
    StartWhenReady();
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
            _deviceCheckTimer.stop();
            ResetAudio();
            _initTimer.start();
        });
    }
}

} // namespace Core::LowAudioLatency
