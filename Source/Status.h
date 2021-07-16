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

#include <variant>
#include <unordered_map>
#include <QString>
#include <QMessageBox>
#include <spdlog/fmt/ostr.h>

#include "Config.h"
#include "Helper.h"

#if defined APD_OS_WIN
    #include "Core/OS/Windows.h"
#endif

class Status
{
private:
    using ValueType = uint64_t;

    enum class State : uint16_t {
        Unknown = 0,
        Success = 1,
        Warning = 2,
        Failure = 3,
    };

    enum class Module : uint16_t {
        Unknown = 0,
        Common = 1,

        Bluetooth = 2,
        GlobalMedia = 3,
        Settings = 4,
        Update = 5,
    };

#define MAKE_STATUS(state, module, specific)                                                       \
    ((ValueType)(                                                                                  \
        ((((ValueType)(state)) & 0xFF) << 48) | ((((ValueType)(module)) & 0xFF) << 32) |           \
        (((ValueType)(specific)) & 0xFFFF)))

    // clang-format off

#define STATUS_VALUES(invoke) \
    invoke(Unknown,                             MAKE_STATUS(State::Unknown, Module::Unknown, 0), "Unknown status.") \
    invoke(Success,                             MAKE_STATUS(State::Success, Module::Common, 0), "Success.") \
\
    invoke(BluetoothAdvWatcherStartFailed,      MAKE_STATUS(State::Failure, Module::Bluetooth, 1), "Bluetooth advertisement watcher start failed.") \
    invoke(BluetoothAdvWatcherStopFailed,       MAKE_STATUS(State::Failure, Module::Bluetooth, 2), "Bluetooth advertisement watcher stop failed.") \
\
    invoke(GlobalMediaPlayAlreadyPlaying,       MAKE_STATUS(State::Success, Module::GlobalMedia, 1), "Play: No need to do anything, because it's already playing.") \
    invoke(GlobalMediaPauseNothingIsPlaying,    MAKE_STATUS(State::Success, Module::GlobalMedia, 2), "Pause: No need to do anything, because nothing is playing.") \
    invoke(GlobalMediaInitializeFailed,         MAKE_STATUS(State::Failure, Module::GlobalMedia, 1), "Global media initialize failed.") \
    invoke(GlobalMediaPlayFailed,               MAKE_STATUS(State::Failure, Module::GlobalMedia, 2), "") \
    invoke(GlobalMediaVKSwitchFailed,           MAKE_STATUS(State::Failure, Module::GlobalMedia, 3), "") \
    invoke(GlobalMediaUSSPlayFailed,            MAKE_STATUS(State::Failure, Module::GlobalMedia, 4), "") \
    invoke(GlobalMediaUSSPauseFailed,           MAKE_STATUS(State::Failure, Module::GlobalMedia, 5), "") \
\
    invoke(SettingsLoadedDataNoAbiVer,          MAKE_STATUS(State::Failure, Module::Settings, 1), "") \
    invoke(SettingsLoadedDataAbiIncompatible,   MAKE_STATUS(State::Failure, Module::Settings, 2), "") \
\
    invoke(UpdateResponseStatusCodeIsNot200,    MAKE_STATUS(State::Failure, Module::Update, 1), "") \
    invoke(UpdateResponseJsonFieldsTypeMismatch,MAKE_STATUS(State::Failure, Module::Update, 2), "") \
    invoke(UpdateResponseJsonFieldUrlInvalid,   MAKE_STATUS(State::Failure, Module::Update, 3), "") \
    invoke(UpdateResponseJsonParseFailed,       MAKE_STATUS(State::Failure, Module::Update, 4), "") \
    invoke(UpdateDownloadCreateDirectoryFailed, MAKE_STATUS(State::Failure, Module::Update, 5), "") \
    invoke(UpdateDownloadCannotAutoUpdate,      MAKE_STATUS(State::Failure, Module::Update, 6), "") \
    invoke(UpdateDownloadStatusCodeIsNot200,    MAKE_STATUS(State::Failure, Module::Update, 7), "") \
    invoke(UpdateDownloadFileSizeMismatch,      MAKE_STATUS(State::Failure, Module::Update, 8), "") \
    invoke(UpdateDownloadStartInstallerFailed,  MAKE_STATUS(State::Failure, Module::Update, 9), "")

    // clang-format on

    class AdditionalData
    {
    private:
        using VarData = std::variant<
            std::monostate, long, uint32_t, uint64_t, bool, std::string, QString
#if defined APD_OS_WIN
            ,
            winrt::hresult_error
#endif
            >;

    public:
        AdditionalData() = default;

        template <class T>
        inline AdditionalData(const T &var)
        {
            _data = VarData{var};
        }

        template <class T>
        inline AdditionalData(T &&var)
        {
            _data = VarData{std::move(var)};
        }

        inline bool HasValue() const
        {
            return _data.index() != 0;
        }

        inline QString ToString() const
        {
            QString format{"(%1) %2"};

            // clang-format off

            return std::visit(
                Helper::Overloaded{
                    [&](std::monostate value) {
                        return format.arg("std::monostate").arg("null");
                    },
                    [&](long value) {
                        return format.arg("long").arg(value);
                    },
                    [&](uint32_t value) {
                        return format.arg("uint32_t").arg(value);
                    },
                    [&](uint64_t value) {
                        return format.arg("uint64_t").arg(value);
                    },
                    [&](bool value) {
                        return format.arg("bool").arg(value);
                    },
                    [&](const std::string &value) {
                        return format.arg("std::string").arg(QString::fromStdString(value));
                    },
                    [&](const QString &value) {
                        return format.arg("QString").arg(value);
                    }
#if defined APD_OS_WIN
                    ,[&](const winrt::hresult_error &value) {
                        return format.arg("winrt::hresult_error").arg(Helper::ToString(value));
                    }
#endif
                },
                _data
            );

            // clang-format on
        }

    private:
        VarData _data;
    };

public:
    enum UnscopedStatus : ValueType {
#define DEFINE_ENUMERATOR(name, value, description) name = (value),
        STATUS_VALUES(DEFINE_ENUMERATOR)
#undef DEFINE_ENUMERATOR
    };

    inline Status(UnscopedStatus status) : _value{status} {}
    Status(const Status &) = default;
    Status(Status &&) = default;

    Status &operator=(const Status &rhs) = default;
    Status &operator=(Status &&rhs) = default;

    inline Status &operator=(UnscopedStatus status)
    {
        _additionalData = decltype(_additionalData){};
        _value = status;
    }

    inline ValueType GetValue() const
    {
        return _value;
    }

    inline bool IsSucceeded() const
    {
        State state = (State)((_value >> 48) & 0xFF);
        return state == State::Success || state == State::Warning;
    }

    inline bool IsFailed() const
    {
        return !IsSucceeded();
    }

    inline QString GetDescription() const
    {
        static std::unordered_map<ValueType, QString> statusDescriptionMap = {
#define DEFINE_ENUMERATOR(name, value, description)                                                \
    {UnscopedStatus::name, QMessageBox::tr(description)},

            STATUS_VALUES(DEFINE_ENUMERATOR)
#undef DEFINE_ENUMERATOR
        };

        auto iter = statusDescriptionMap.find(_value);
        if (iter == statusDescriptionMap.end()) {
            return QString{"Status description not found. Value: %1"}.arg(_value);
        }
        else {
            return (*iter).second;
        }
    }

    inline QString ToMessage() const
    {
        QString result =
            QString{"0x%1 (%2)"}.arg(QString::number(_value, 16)).arg(GetDescription());

        for (size_t i = 0; i < _additionalData.size(); ++i) {
            const auto &data = _additionalData.at(i);

            if (data.HasValue()) {
                result += QString{" AdditionalData %1: %2"}.arg(i).arg(data.ToString());
            }
        }

        return result;
    }

    inline Status &SetAdditionalData(
        const AdditionalData &data1, const AdditionalData &data2 = AdditionalData{},
        const AdditionalData &data3 = AdditionalData{},
        const AdditionalData &data4 = AdditionalData{})
    {
        _additionalData = {data1, data2, data3, data4};
        return *this;
    }

    inline const auto &GetAdditionalData() const
    {
        return _additionalData;
    }

    template <class OutStream>
    inline friend OutStream &operator<<(OutStream &outStream, const Status &status)
    {
        return outStream << status.ToMessage().toStdString().c_str();
    }

private:
    ValueType _value{UnscopedStatus::Unknown};
    std::array<AdditionalData, 4> _additionalData{};
};
