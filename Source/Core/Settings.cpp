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

#include "../Helper.h"
#include "../Logger.h"
#include "../Application.h"


namespace Core::Settings
{
    Status Data::LoadFromQSettings(const QSettings &settings)
    {
#define LOAD_VALUE_TO_VARIABLE(variable, key)                                           \
        if (!settings.contains((key))) {                                                \
            spdlog::warn(                                                               \
                "This QSettings doesn't contain key '{}'. Use the default value: {}",   \
                (key), (variable)                                                       \
            );                                                                          \
        }                                                                               \
        else {                                                                          \
            QVariant var = settings.value((key));                                       \
            if (!var.canConvert<decltype(variable)>() ||                                \
                !var.convert(qMetaTypeId<decltype(variable)>()))                        \
            {                                                                           \
                spdlog::warn(                                                           \
                    "The value of the key '{}' cannot be convert.",                     \
                    (key)                                                               \
                );                                                                      \
            }                                                                           \
            else {                                                                      \
                (variable) = var.value<decltype(variable)>();                           \
                spdlog::info(                                                           \
                    "Load key succeeded. Key: '{}', Value: {}",                         \
                    (key), (variable)                                                   \
                );                                                                      \
            }                                                                           \
        }

#define LOAD_VALUE(variable)    LOAD_VALUE_TO_VARIABLE(variable, TO_STRING(variable))

        std::remove_cv_t<decltype(abi_version)> settingsAbiVer = 0;

        LOAD_VALUE_TO_VARIABLE(settingsAbiVer, "abi_version");
        if (settingsAbiVer == 0) {
            return Status::SettingsLoadedDataNoAbiVer;
        }
        if (settingsAbiVer != abi_version) {
            return Status{Status::SettingsLoadedDataAbiIncompatible}
                .SetAdditionalData(settingsAbiVer);
        }

        LOAD_VALUE(auto_run);
        LOAD_VALUE(low_audio_latency);
        LOAD_VALUE(skipped_version);

        return Status::Success;

#undef LOAD_VALUE
#undef LOAD_VALUE_TO_VARIABLE
    }

    Status Data::SaveToQSettings(QSettings &settings) const
    {
#define SAVE_VALUE_FROM_VARIABLE(variable, key) {                                       \
            settings.setValue((key), (variable));                                       \
            spdlog::info(                                                               \
                "Save key succeeded. Key: '{}', Value: {}",                             \
                (key), (variable)                                                       \
            );                                                                          \
        }

#define SAVE_VALUE(variable)    SAVE_VALUE_FROM_VARIABLE(variable, TO_STRING(variable))

        SAVE_VALUE(abi_version);

        SAVE_VALUE(auto_run);
        SAVE_VALUE(low_audio_latency);
        SAVE_VALUE(skipped_version);

        return Status::Success;

#undef SAVE_VALUE
#undef SAVE_VALUE_FROM_VARIABLE
    }

    void Data::HandleDiff(const Data &other)
    {
#define HANDLE_IF_DIFF(variable, handler)   \
        if (variable != other.variable) { handler(other.variable); }

        HANDLE_IF_DIFF(auto_run, OnAutoRunChanged);
        // HANDLE_IF_DIFF(low_audio_latency, /* nothing */);

#undef HANDLE_IF_DIFF
    }

    void Data::OnAutoRunChanged(bool value)
    {
        spdlog::info("OnAutoRunChanged: {}", value);

#if !defined APD_OS_WIN
#   error "Need to port."
#endif

        QSettings regAutoRun{
            "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
            QSettings::Registry64Format
        };

        QString filePath = QDir::toNativeSeparators(Application::applicationFilePath());
        if (value) {
            regAutoRun.setValue(Config::ProgramName, filePath);
        }
        else {
            regAutoRun.remove(Config::ProgramName);
        }
    }


    class Manager
    {
    public:
        static Manager& GetInstance() {
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
                _currentData = data;
            }

            return status;
        }

        void Load(Data data)
        {
            std::lock_guard<std::mutex> lock{_mutex};
            _currentData = std::move(data);
        }

        Data GetCurrent() const
        {
            std::lock_guard<std::mutex> lock{_mutex};

            APD_ASSERT(_currentData.has_value());

            return _currentData.value();
        }

        Status SaveToCurrentAndLocal(Data data)
        {
            std::lock_guard<std::mutex> lock{_mutex};

            APD_ASSERT(_currentData.has_value());

            _currentData->HandleDiff(data);
            _currentData = std::move(data);
            return _currentData->SaveToQSettings(_settings);
        }

    private:
        QSettings _settings{QSettings::UserScope, Config::ProgramName, Config::ProgramName};
        mutable std::mutex _mutex;
        std::optional<Data> _currentData;
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
        return Manager::GetInstance().Load(GetDefault());
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
