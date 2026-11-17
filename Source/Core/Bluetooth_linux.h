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

#if !defined APD_OS_LINUX
    #error "This file shouldn't be compiled."
#endif

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QString>
#include <QVariant>

#include "Bluetooth_abstract.h"

namespace Core::Bluetooth {

class Device final : public Details::DeviceAbstract<uint64_t>
{
public:
    Device() = default;
    explicit Device(QString objectPath);
    Device(const Device &rhs);
    Device(Device &&rhs) noexcept;
    Device &operator=(const Device &rhs);
    Device &operator=(Device &&rhs) noexcept;
    ~Device() override;

    uint64_t GetAddress() const override;
    std::string GetName() const override;
    uint16_t GetVendorId() const override;
    uint16_t GetProductId() const override;
    DeviceState GetConnectionState() const override;

private:
    class Notifier;

    QString _objectPath;
    std::shared_ptr<Notifier> _notifier;
    uint64_t _sinkId{};
    uint64_t _nextSinkId{};

    void Attach();
    void Detach();
    void CopyFrom(const Device &rhs);
    void MoveFrom(Device &&rhs) noexcept;
};

namespace DeviceManager {

std::vector<Device> GetDevicesByState(DeviceState state);
std::optional<Device> FindDevice(uint64_t address);

} // namespace DeviceManager

class AdvertisementWatcher final
    : public Details::AdvertisementWatcherAbstract<AdvertisementWatcher>
{
public:
    using Timestamp = std::chrono::steady_clock::time_point;

    AdvertisementWatcher();
    ~AdvertisementWatcher() override;

    bool Start() override;
    bool Stop() override;

private:
    class Notifier;

    std::shared_ptr<Notifier> _notifier;
    QString _adapterPath;
    std::atomic<bool> _running{false};

    bool FindAdapter();
    bool StartDiscovery();
    bool StopDiscovery();
    void EmitAdvertisement(
        const QString &objectPath, uint64_t address, int16_t rssi,
        const std::map<uint16_t, std::vector<uint8_t>> &manufacturerData);
};

} // namespace Core::Bluetooth
