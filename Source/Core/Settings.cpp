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
#include <boost/pfr.hpp>

#include <Config.h>
#include "../Helper.h"
#include "../Logger.h"
#include "../Application.h"
#include "GlobalMedia.h"
#include "LowAudioLatency.h"

using namespace boost;

namespace Core::Settings {

template <class T>
std::string_view LogSensitiveData(const T &value)
{
    return value != std::decay_t<T>{} ? "** MAYBE HAVE VALUE **" : "** MAYBE NO VALUE **";
}

void OnApply_auto_run(const Fields &newFields)
{
    SPDLOG_INFO("OnApply_auto_run: {}", newFields.auto_run);

#if !defined APD_OS_WIN
    #error "Need to port."
#endif

    QSettings regAutoRun{
        "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::Registry64Format};

    QString filePath = QDir::toNativeSeparators(ApdApplication::applicationFilePath());
    if (newFields.auto_run) {
        regAutoRun.setValue(Config::ProgramName, filePath);
    }
    else {
        regAutoRun.remove(Config::ProgramName);
    }
}

void OnApply_low_audio_latency(const Fields &newFields)
{
    SPDLOG_INFO("OnApply_low_audio_latency: {}", newFields.low_audio_latency);

    LowAudioLatency::Control(newFields.low_audio_latency);
}

void OnApply_reduce_loud_sounds(const Fields &newFields)
{
    SPDLOG_INFO("OnApply_reduce_loud_sounds: {}", newFields.reduce_loud_sounds);

    GlobalMedia::LimitVolume(
        newFields.reduce_loud_sounds ? std::optional<uint32_t>{newFields.loud_volume_level}
                                     : std::nullopt);
}

void OnApply_loud_volume_level(const Fields &newFields)
{
    SPDLOG_INFO("OnApply_loud_volume_level: {}", newFields.loud_volume_level);

    GlobalMedia::LimitVolume(
        newFields.reduce_loud_sounds ? std::optional<uint32_t>{newFields.loud_volume_level}
                                     : std::nullopt);
}

void OnApply_device_address(const Fields &newFields)
{
    SPDLOG_INFO("OnApply_device_address: {}", LogSensitiveData(newFields.device_address));

    AirPods::OnBindDeviceChanged(newFields.device_address);
}

namespace Impl {

//////////////////////////////////////////////////
// Options
//

class OnApply
{
public:
    using FnCallbackT = std::function<void(const Fields &)>;

    OnApply() = default;
    OnApply(FnCallbackT callback) : _callback{std::move(callback)} {}

    template <class... ArgsT>
    void Invoke(ArgsT &&...args) const
    {
        if (_callback) {
            _callback(std::forward<ArgsT>(args)...);
        }
    }

private:
    FnCallbackT _callback;
};

class Sensitive
{
};

//////////////////////////////////////////////////

template <class T>
class MetaField
{
public:
    template <class... ArgsT>
    MetaField(std::string_view name, T member, ArgsT &&...args)
        : _name{std::move(name)}, _member{std::move(member)}
    {
        std::initializer_list<int> ignore = {(SetOption(std::forward<ArgsT>(args)), 0)...};
    }

    const std::string_view &GetName() const
    {
        return _name;
    }

    const Helper::MemberPointerType<T> &GetValue(const Fields &fields) const
    {
        return fields.*_member;
    }

    Helper::MemberPointerType<T> &GetValue(Fields &fields)
    {
        return fields.*_member;
    }

    void SetOption(OnApply onApply)
    {
        _onApply = std::move(onApply);
    }

    void SetOption(Sensitive)
    {
        _isSensitive = true;
    }

    const OnApply &OnApply() const
    {
        return _onApply;
    }

    bool IsSensitive() const
    {
        return _isSensitive;
    }

private:
    std::string_view _name;
    T _member;
    Impl::OnApply _onApply;
    bool _isSensitive{false};
};

// TODO: Rename
struct FieldsMetaView {
#define DECLARE_META_FIELD(type, name, dft, ...)                                                   \
    MetaField<type Fields::*> name{TO_STRING(name), &Fields::name, __VA_ARGS__};
    SETTINGS_FIELDS(DECLARE_META_FIELD)
#undef DECLARE_FIELD
};

} // namespace Impl

class Manager
{
public:
    static Manager &GetInstance()
    {
        static Manager i;
        return i;
    }

    LoadResult Load()
    {
        const auto &loadKey = [&](const std::string_view &keyName, auto &value, bool restrict = false) {
            using ValueType = std::decay_t<decltype(value)>;

            QString qstrKeyName = QString::fromStdString(std::string{keyName});
            if (!_settings.contains(qstrKeyName)) {
                if (!restrict) {
                    SPDLOG_WARN(
                        "The setting key '{}' not found. Current value '{}'.", keyName, value);
                }
                else {
                    SPDLOG_WARN(
                        "The setting key '{}' not found. Current value '{}'.", keyName,
                        LogSensitiveData(value));
                }
                return false;
            }

            QVariant var = _settings.value(qstrKeyName);
            if (!var.canConvert<ValueType>() || !var.convert(qMetaTypeId<ValueType>())) {
                SPDLOG_WARN("The value of the key '{}' cannot be convert.", keyName);
                return false;
            }

            value = var.value<ValueType>();
            if (!restrict) {
                SPDLOG_INFO("Load key succeeded. Key: '{}', Value: '{}'", keyName, value);
            }
            else {
                SPDLOG_INFO(
                    "Load key succeeded. Key: '{}', Value: '{}'", keyName, LogSensitiveData(value));
            }
            return true;
        };

        std::lock_guard<std::mutex> lock{_mutex};

        std::decay_t<decltype(kFieldsAbiVersion)> abi_version = 0;
        if (!loadKey("abi_version", abi_version)) {
            SPDLOG_WARN("No abi_version key. Load default settings.");
            _fields = Fields{};
            return LoadResult::NoAbiField;
        }
        else {
            if (abi_version != kFieldsAbiVersion) {
                SPDLOG_WARN(
                    "The settings abi version is incompatible. Local: '{}', Expect: '{}'",
                    abi_version, kFieldsAbiVersion);
                return LoadResult::AbiIncompatible;
            }

            pfr::for_each_field(Impl::FieldsMetaView{}, [&](auto &field) {
                loadKey(field.GetName(), field.GetValue(_fields), field.IsSensitive());
            });
            return LoadResult::Successful;
        }
    }

    void Save(Fields newFields)
    {
        std::lock_guard<std::mutex> lock{_mutex};

        _fields = std::move(newFields);
        SaveWithoutLock();
        ApplyWithoutLock();
    }

    void Apply()
    {
        std::lock_guard<std::mutex> lock{_mutex};

        ApplyWithoutLock();
    }

    Fields GetCurrent()
    {
        std::lock_guard<std::mutex> lock{_mutex};

        return _fields;
    }

    auto ConstAccess()
    {
        return ConstSafeAccessor{_mutex, _fields};
    }

    auto ModifiableAccess()
    {
        return ModifiableSafeAccessor{_mutex, _fields};
    }

private:
    std::mutex _mutex;
    Fields _fields;
    QSettings _settings{QSettings::UserScope, Config::ProgramName, Config::ProgramName};

    void SaveWithoutLock()
    {
        const auto &saveKey = [&](const std::string_view &keyName, const auto &value,
                                  bool restrict = false) {
            QString qstrKeyName = QString::fromStdString(std::string{keyName});
            _settings.setValue(qstrKeyName, value);

            if (!restrict) {
                SPDLOG_INFO("Save key succeeded. Key: '{}', Value: {}", keyName, value);
            }
            else {
                SPDLOG_INFO(
                    "Save key succeeded. Key: '{}', Value: {}", keyName, LogSensitiveData(value));
            }
        };

        saveKey("abi_version", kFieldsAbiVersion);

        pfr::for_each_field(Impl::FieldsMetaView{}, [&](const auto &field) {
            saveKey(field.GetName(), field.GetValue(_fields), field.IsSensitive());
        });
    }

    void ApplyWithoutLock()
    {
        pfr::for_each_field(
            Impl::FieldsMetaView{},
            [&](const auto &field) { field.OnApply().Invoke(std::cref(_fields)); });
    }

    friend class ModifiableSafeAccessor;
};

ModifiableSafeAccessor::~ModifiableSafeAccessor()
{
    Manager::GetInstance().SaveWithoutLock();
    Manager::GetInstance().ApplyWithoutLock();
}

LoadResult Load()
{
    return Manager::GetInstance().Load();
}

void Save(Fields newFields)
{
    return Manager::GetInstance().Save(std::move(newFields));
}

void Apply()
{
    return Manager::GetInstance().Apply();
}

Fields GetCurrent()
{
    return Manager::GetInstance().GetCurrent();
}

Fields GetDefault()
{
    return Fields{};
}

ConstSafeAccessor ConstAccess()
{
    return Manager::GetInstance().ConstAccess();
}

ModifiableSafeAccessor ModifiableAccess()
{
    return Manager::GetInstance().ModifiableAccess();
}
} // namespace Core::Settings
