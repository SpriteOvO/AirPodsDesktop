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

#include "Settings.h"

#include <mutex>
#include <boost/pfr.hpp>
#include <magic_enum.hpp>

#include "../Logger.h"
#include "SettingsRepository.h"

using namespace boost;

namespace Core::Settings {

namespace {

ApplyObserver *_applyObserver = nullptr;

} // namespace

void SetApplyObserver(ApplyObserver *observer)
{
    _applyObserver = observer;
}

template <class T>
std::string_view LogSensitiveData(const T &value)
{
    return value != std::decay_t<T>{} ? "** MAYBE HAVE VALUE **" : "** MAYBE NO VALUE **";
}

namespace Impl {

ApplyObserver *GetApplyObserver()
{
    return _applyObserver;
}

void NotifyApplyObserver(auto &&invoke)
{
    if (auto *observer = GetApplyObserver(); observer) {
        invoke(*observer);
    }
}

} // namespace Impl

void OnApply_language_locale(const Fields &newFields)
{
    LOG(Info, "OnApply_language_locale: {}", newFields.language_locale);

    const QLocale locale =
        newFields.language_locale.isEmpty() ? QLocale{} : QLocale{newFields.language_locale};
    Impl::NotifyApplyObserver(
        [&](ApplyObserver &observer) { observer.OnLanguageLocaleChanged(locale); });
}

void OnApply_auto_run(const Fields &newFields)
{
    LOG(Info, "OnApply_auto_run: {}", newFields.auto_run);

    Impl::NotifyApplyObserver(
        [&](ApplyObserver &observer) { observer.OnAutoRunChanged(newFields.auto_run); });
}

void OnApply_low_audio_latency(const Fields &newFields)
{
    LOG(Info, "OnApply_low_audio_latency: {}", newFields.low_audio_latency);

    Impl::NotifyApplyObserver([&](ApplyObserver &observer) {
        observer.OnLowAudioLatencyChanged(newFields.low_audio_latency);
    });
}

void OnApply_automatic_ear_detection(const Fields &newFields)
{
    LOG(Info, "OnApply_automatic_ear_detection: {}", newFields.automatic_ear_detection);

    Impl::NotifyApplyObserver([&](ApplyObserver &observer) {
        observer.OnAutomaticEarDetectionChanged(newFields.automatic_ear_detection);
    });
}

void OnApply_rssi_min(const Fields &newFields)
{
    LOG(Info, "OnApply_rssi_min: {}", newFields.rssi_min);

    Impl::NotifyApplyObserver(
        [&](ApplyObserver &observer) { observer.OnRssiMinChanged(newFields.rssi_min); });
}

void OnApply_device_address(const Fields &newFields)
{
    LOG(Info, "OnApply_device_address: {}", LogSensitiveData(newFields.device_address));

    Impl::NotifyApplyObserver([&](ApplyObserver &observer) {
        observer.OnDeviceAddressChanged(newFields.device_address);
    });
}

void OnApply_tray_icon_battery(const Fields &newFields)
{
    LOG(Info, "OnApply_tray_icon_battery: {}", newFields.tray_icon_battery);

    Impl::NotifyApplyObserver([&](ApplyObserver &observer) {
        observer.OnTrayIconBatteryChanged(newFields.tray_icon_battery);
    });
}

void OnApply_tray_quick_connect_enabled(const Fields &newFields)
{
    LOG(Info, "OnApply_tray_quick_connect_enabled: {}", newFields.tray_quick_connect_enabled);

    Impl::NotifyApplyObserver([&](ApplyObserver &observer) {
        observer.OnTrayQuickConnectEnabledChanged(newFields.tray_quick_connect_enabled);
    });
}

void OnApply_tray_quick_connect_device_id(const Fields &newFields)
{
    LOG(Info, "OnApply_tray_quick_connect_device_id: {}", newFields.tray_quick_connect_device_id);

    Impl::NotifyApplyObserver([&](ApplyObserver &observer) {
        observer.OnTrayQuickConnectDeviceChanged(newFields.tray_quick_connect_device_id);
    });
}

void OnApply_battery_on_taskbar(const Fields &newFields)
{
    LOG(Info, "OnApply_battery_on_taskbar: {}", newFields.battery_on_taskbar);

    Impl::NotifyApplyObserver([&](ApplyObserver &observer) {
        observer.OnTaskbarBatteryChanged(newFields.battery_on_taskbar);
    });
}

class Manager : public Helper::Singleton<Manager>
{
protected:
    Manager() : _repository{CreatePersistentRepository()} {}
    friend Helper::Singleton<Manager>;

public:
    LoadResult Load()
    {
        const auto &loadKey = [&](const std::string_view &keyName, auto &value,
                                  bool isSensitive = false) {
            using ValueType = std::decay_t<decltype(value)>;
            using ValueStorageType =
                std::conditional_t<!std::is_enum_v<ValueType>, ValueType, QString>;

            QString qstrKeyName = QString::fromStdString(std::string{keyName});
            if (!_repository->Contains(qstrKeyName)) {
                if (!isSensitive) {
                    LOG(Warn, "The setting key '{}' not found. Current value '{}'.", keyName,
                        value);
                }
                else {
                    LOG(Warn, "The setting key '{}' not found. Current value '{}'.", keyName,
                        LogSensitiveData(value));
                }
                return false;
            }

            QVariant var = _repository->Read(qstrKeyName);
            if (!var.canConvert<ValueStorageType>() ||
                !var.convert(qMetaTypeId<ValueStorageType>()))
            {
                LOG(Warn, "The value of the key '{}' cannot be convert.", keyName);
                return false;
            }

            if constexpr (!std::is_enum_v<ValueType>) {
                value = var.value<ValueStorageType>();
            }
            else {
                auto optValue =
                    magic_enum::enum_cast<ValueType>(var.value<ValueStorageType>().toStdString());
                if (!optValue.has_value()) {
                    LOG(Warn, "enum_cast the value of the key '{}' failed.", keyName);
                    return false;
                }
                value = optValue.value();
            }

            if (!isSensitive) {
                LOG(Info, "Load key succeeded. Key: '{}', Value: '{}'", keyName, value);
            }
            else {
                LOG(Info, "Load key succeeded. Key: '{}', Value: '{}'", keyName,
                    LogSensitiveData(value));
            }
            return true;
        };

        std::lock_guard<std::mutex> lock{_mutex};

        std::decay_t<decltype(kFieldsAbiVersion)> abi_version = 0;
        if (!loadKey("abi_version", abi_version)) {
            LOG(Warn, "No abi_version key. Load default settings.");
            _fields = Fields{};
            return LoadResult::NoAbiField;
        }
        else {
            if (abi_version != kFieldsAbiVersion) {
                LOG(Warn, "The settings abi version is incompatible. Local: '{}', Expect: '{}'",
                    abi_version, kFieldsAbiVersion);
                return LoadResult::AbiIncompatible;
            }

            pfr::for_each_field(_fieldsMeta, [&](auto &field) {
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

    void SetRepository(std::unique_ptr<Repository> repository)
    {
        if (!repository) {
            return;
        }
        std::lock_guard<std::mutex> lock{_mutex};
        _repository = std::move(repository);
        _fields = Fields{};
    }

private:
    MetaFields _fieldsMeta;

    std::mutex _mutex;
    Fields _fields;
    std::unique_ptr<Repository> _repository;

    void SaveWithoutLock()
    {
        const auto &saveKey = [&]<class T>(
                                  const std::string_view &keyName, const T &value,
                                  bool isSensitive = false, bool isDeprecated = false) {
            QString qstrKeyName = QString::fromStdString(std::string{keyName});

            if (isDeprecated) {
                _repository->Remove(qstrKeyName);
                LOG(Info, "Remove deprecated key succeeded. Key: '{}'", keyName);
                return;
            }

            if constexpr (!std::is_enum_v<T>) {
                _repository->Write(qstrKeyName, value);
            }
            else {
                _repository->Write(
                    qstrKeyName, QString::fromStdString(std::string{magic_enum::enum_name(value)}));
            }

            if (!isSensitive) {
                LOG(Info, "Save key succeeded. Key: '{}', Value: {}", keyName, value);
            }
            else {
                LOG(Info, "Save key succeeded. Key: '{}', Value: {}", keyName,
                    LogSensitiveData(value));
            }
        };

        saveKey("abi_version", kFieldsAbiVersion);

        pfr::for_each_field(_fieldsMeta, [&](const auto &fieldMeta) {
            saveKey(
                fieldMeta.GetName(), fieldMeta.GetValue(_fields), fieldMeta.IsSensitive(),
                fieldMeta.IsDeprecated());
        });
    }

    void ApplyWithoutLock()
    {
        LOG(Info, "ApplyWithoutLock");

        pfr::for_each_field(_fieldsMeta, [&](const auto &fieldMeta) {
            fieldMeta.OnApply().Invoke(std::cref(_fields));
        });
    }

    void ApplyChangedFieldsOnlyWithoutLock(const Fields &oldFields)
    {
        LOG(Info, "ApplyChangedFieldsOnlyWithoutLock");

        pfr::for_each_field(_fieldsMeta, [&](const auto &fieldMeta) {
            if (fieldMeta.GetValue(oldFields) != fieldMeta.GetValue(_fields)) {
                LOG(Info, "Changed field: {}", fieldMeta.GetName());
                fieldMeta.OnApply().Invoke(std::cref(_fields));
            }
        });
    }

    friend class ModifiableSafeAccessor;
};

ModifiableSafeAccessor::ModifiableSafeAccessor(std::mutex &lock, Fields &fields)
    : Impl::BasicSafeAccessor<Fields>{lock, fields}, _oldFields{fields}
{
}

ModifiableSafeAccessor::~ModifiableSafeAccessor()
{
    Manager::GetInstance().SaveWithoutLock();
    Manager::GetInstance().ApplyChangedFieldsOnlyWithoutLock(_oldFields);
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

void SetRepository(std::unique_ptr<Repository> repository)
{
    Manager::GetInstance().SetRepository(std::move(repository));
}
} // namespace Core::Settings
