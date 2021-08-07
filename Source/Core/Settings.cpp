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

#include "Settings.h"

#include <mutex>
#include <QDir>
#include <spdlog/spdlog.h>

#include <Config.h>
#include "../Helper.h"
#include "../Logger.h"
#include "../Application.h"
#include "GlobalMedia.h"
#include "LowAudioLatency.h"

namespace Core::Settings {

enum class ValueType : uint32_t { Normal, Sensitive };

template <ValueType type, class T>
std::conditional_t<type == ValueType::Normal, const T &, const char *> LogValue(const T &value)
{
    if constexpr (type == ValueType::Normal) {
        return value;
    }
    else if constexpr (type == ValueType::Sensitive) {
        if ((value) != T{}) {
            return "** MAYBE HAVE VALUE **";
        }
        else {
            return "** MAYBE NO VALUE **";
        }
    }
    else {
        static_assert(false);
    }
}

Status Data::LoadFromQSettings(const QSettings &settings)
{
#define LOAD_VALUE_TO_VARIABLE(variable, type, key)                                                \
    if (!settings.contains((key))) {                                                               \
        SPDLOG_WARN(                                                                               \
            "This QSettings doesn't contain key '{}'. Use the default value: {}", (key),           \
            LogValue<ValueType::type>(variable));                                                  \
    }                                                                                              \
    else {                                                                                         \
        QVariant var = settings.value((key));                                                      \
        if (!var.canConvert<decltype(variable)>() ||                                               \
            !var.convert(qMetaTypeId<decltype(variable)>())) {                                     \
            SPDLOG_WARN("The value of the key '{}' cannot be convert.", (key));                    \
        }                                                                                          \
        else {                                                                                     \
            (variable) = var.value<decltype(variable)>();                                          \
            SPDLOG_INFO(                                                                           \
                "Load key succeeded. Key: '{}', Value: {}", (key),                                 \
                LogValue<ValueType::type>(variable));                                              \
        }                                                                                          \
    }

#define LOAD_VALUE(variable, type) LOAD_VALUE_TO_VARIABLE(variable, type, TO_STRING(variable))

    std::remove_cv_t<decltype(abi_version)> settingsAbiVer = 0;

    LOAD_VALUE_TO_VARIABLE(settingsAbiVer, Normal, "abi_version");
    if (settingsAbiVer == 0) {
        return Status::SettingsLoadedDataNoAbiVer;
    }
    if (settingsAbiVer != abi_version) {
        return Status{Status::SettingsLoadedDataAbiIncompatible}.SetAdditionalData(settingsAbiVer);
    }

    LOAD_VALUE(auto_run, Normal);
    LOAD_VALUE(low_audio_latency, Normal);
    LOAD_VALUE(automatic_ear_detection, Normal);
    LOAD_VALUE(skipped_version, Normal);
    LOAD_VALUE(rssi_min, Normal);
    LOAD_VALUE(reduce_loud_sounds, Normal);
    LOAD_VALUE(loud_volume_level, Normal);
    LOAD_VALUE(device_address, Sensitive);

    return Status::Success;

#undef LOAD_VALUE
#undef LOAD_VALUE_TO_VARIABLE
}

Status Data::SaveToQSettings(QSettings &settings) const
{
#define SAVE_VALUE_FROM_VARIABLE(variable, type, key)                                              \
    {                                                                                              \
        settings.setValue((key), (variable));                                                      \
        SPDLOG_INFO(                                                                               \
            "Save key succeeded. Key: '{}', Value: {}", (key),                                     \
            LogValue<ValueType::type>(variable));                                                  \
    }

#define SAVE_VALUE(variable, type) SAVE_VALUE_FROM_VARIABLE(variable, type, TO_STRING(variable))

    SAVE_VALUE(abi_version, Normal);

    SAVE_VALUE(auto_run, Normal);
    SAVE_VALUE(low_audio_latency, Normal);
    SAVE_VALUE(automatic_ear_detection, Normal);
    SAVE_VALUE(skipped_version, Normal);
    SAVE_VALUE(rssi_min, Normal);
    SAVE_VALUE(reduce_loud_sounds, Normal);
    SAVE_VALUE(loud_volume_level, Normal);
    SAVE_VALUE(device_address, Sensitive);

    return Status::Success;

#undef SAVE_VALUE
#undef SAVE_VALUE_FROM_VARIABLE
}

void Data::HandleFields(const Data &other)
{
#define HANDLE_FIELD(variable, handler)                                                            \
    if (/*variable != other.variable*/ true) {                                                     \
        handler(other, other.variable);                                                            \
    }

    HANDLE_FIELD(auto_run, OnAutoRunChanged);
    HANDLE_FIELD(low_audio_latency, OnLowAudioLatencyChanged);
    HANDLE_FIELD(reduce_loud_sounds, OnReduceLoudSoundsChanged);
    HANDLE_FIELD(loud_volume_level, OnLoudVolumeLevelChanged);
    HANDLE_FIELD(device_address, OnDeviceAddressChanged);

#undef HANDLE_FIELD
}

void Data::OnAutoRunChanged(const Data &current, bool value)
{
    SPDLOG_INFO("OnAutoRunChanged: {}", value);

#if !defined APD_OS_WIN
    #error "Need to port."
#endif

    QSettings regAutoRun{
        "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::Registry64Format};

    QString filePath = QDir::toNativeSeparators(ApdApplication::applicationFilePath());
    if (value) {
        regAutoRun.setValue(Config::ProgramName, filePath);
    }
    else {
        regAutoRun.remove(Config::ProgramName);
    }
}

void Data::OnLowAudioLatencyChanged(const Data &current, bool value)
{
    SPDLOG_INFO("OnLowAudioLatencyChanged: {}", value);

    LowAudioLatency::Control(value);
}

void Data::OnReduceLoudSoundsChanged(const Data &current, bool value)
{
    SPDLOG_INFO("OnReduceLoudSoundsChanged: {}", value);

    GlobalMedia::LimitVolume(
        value ? std::optional<uint32_t>{current.loud_volume_level} : std::nullopt);
}

void Data::OnLoudVolumeLevelChanged(const Data &current, uint32_t value)
{
    SPDLOG_INFO("OnLoudVolumeLevelChanged: {}", value);

    GlobalMedia::LimitVolume(
        current.reduce_loud_sounds ? std::optional<uint32_t>{value} : std::nullopt);
}

void Data::OnDeviceAddressChanged(const Data &current, uint64_t value)
{
    SPDLOG_INFO("OnDeviceAddressChanged: {}", LogValue<ValueType::Sensitive>(value));

    AirPods::OnBindDeviceChanged(value);
}

class Manager
{
public:
    static Manager &GetInstance()
    {
        static Manager i;
        return i;
    }

    static Data GetDefault()
    {
        return Data{};
    }

    Status LoadFromLocal()
    {
        Data data;
        Status status = data.LoadFromQSettings(_settings);

        if (status.IsSucceeded()) {
            std::lock_guard<std::mutex> lock{_mutex};
            Load(std::move(data));
        }
        return status;
    }

    void LoadDefault()
    {
        std::lock_guard<std::mutex> lock{_mutex};

        Load(Data{});
    }

    Data GetCurrent() const
    {
        std::lock_guard<std::mutex> lock{_mutex};

        return _currentData;
    }

    Status SaveToCurrentAndLocal(Data data)
    {
        std::lock_guard<std::mutex> lock{_mutex};

        Load(std::move(data));
        return _currentData.SaveToQSettings(_settings);
    }

private:
    QSettings _settings{QSettings::UserScope, Config::ProgramName, Config::ProgramName};
    mutable std::mutex _mutex;
    Data _currentData;

    void Load(Data data)
    {
        _currentData.HandleFields(data);
        _currentData = std::move(data);
    }
};

Data GetDefault()
{
    return Manager::GetDefault();
}

Data GetCurrent()
{
    return Manager::GetInstance().GetCurrent();
}

void LoadDefault()
{
    return Manager::GetInstance().LoadDefault();
}

Status LoadFromLocal()
{
    return Manager::GetInstance().LoadFromLocal();
}

Status SaveToCurrentAndLocal(Data data)
{
    return Manager::GetInstance().SaveToCurrentAndLocal(std::move(data));
}
} // namespace Core::Settings
