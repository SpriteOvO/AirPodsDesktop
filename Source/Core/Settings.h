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

#include <memory>
#include <mutex>

#include <QCoreApplication>
#include <QLocale>

#include "../Helper.h"

namespace Core::Settings {

class Repository;

enum class TrayIconBatteryBehavior : uint32_t { Disable, WhenLowBattery, Always };
enum class TaskbarStatusBehavior : uint32_t { Disable, Text, Icon };
enum class AppearanceMode : uint32_t { System, Light, Dark };
enum class LoadResult : uint32_t { AbiIncompatible, NoAbiField, Successful };

// Receives settings side effects so that this module doesn't need to know
// about GUI or other application services.
//
class ApplyObserver
{
public:
    virtual ~ApplyObserver() = default;

    virtual void OnLanguageLocaleChanged(const QLocale &locale) = 0;
    virtual void OnAppearanceModeChanged(AppearanceMode mode) = 0;
    virtual void OnAutoRunChanged(bool enable) = 0;
    virtual void OnLowAudioLatencyChanged(bool enable) = 0;
    virtual void OnAutomaticEarDetectionChanged(bool enable) = 0;
    virtual void OnRssiMinChanged(int16_t rssiMin) = 0;
    virtual void OnDeviceAddressChanged(uint64_t address) = 0;
    virtual void OnTrayIconBatteryChanged(TrayIconBatteryBehavior behavior) = 0;
    virtual void OnTrayQuickConnectEnabledChanged(bool enable) = 0;
    virtual void OnTrayQuickConnectDeviceChanged(const QString &deviceId) = 0;
    virtual void OnTaskbarBatteryChanged(TaskbarStatusBehavior behavior) = 0;
};

void SetApplyObserver(ApplyObserver *observer);
void SetRepository(std::unique_ptr<Repository> repository);

// TODO: in [v1.0.0] [kFieldsAbiVersion = 2]
//        - Rename `tray_icon_battery` to `battery_on_tray_icon`

// clang-format off
#define SETTINGS_FIELDS(callback)                                                                  \
    callback(QString, language_locale, {}, Impl::OnApply(&OnApply_language_locale))                \
    callback(AppearanceMode, appearance_mode, {AppearanceMode::System},                            \
        Impl::OnApply(&OnApply_appearance_mode))                                                   \
    callback(bool, auto_run, {false}, Impl::OnApply(&OnApply_auto_run))                            \
    callback(bool, low_audio_latency, {false},                                                     \
        Impl::OnApply(&OnApply_low_audio_latency),                                                 \
        Impl::Desc{QT_TRANSLATE_NOOP("QObject", "Keeps the audio device awake while your AirPods are connected so short sounds start immediately. This may produce audible hiss and use more battery.")}) \
    callback(bool, automatic_ear_detection, {true},                                                \
        Impl::OnApply(&OnApply_automatic_ear_detection),                                           \
        Impl::Desc{QT_TRANSLATE_NOOP("QObject", "It automatically pauses or resumes media when your AirPods are taken out or put in your ears.")}) \
    callback(QString, skipped_version, {})                                                         \
    callback(int16_t, rssi_min, {-80}, Impl::OnApply(&OnApply_rssi_min))                           \
    callback(bool, reduce_loud_sounds, {false}, Impl::Deprecated())                                \
    callback(uint32_t, loud_volume_level, {40}, Impl::Deprecated())                                \
    callback(uint64_t, device_address, {0},                                                        \
        Impl::OnApply(&OnApply_device_address),                                                    \
        Impl::Sensitive{})                                                                         \
    callback(TrayIconBatteryBehavior, tray_icon_battery, {TrayIconBatteryBehavior::Disable},       \
        Impl::OnApply(&OnApply_tray_icon_battery))                                                 \
    callback(bool, tray_quick_connect_enabled, {false},                                            \
        Impl::OnApply(&OnApply_tray_quick_connect_enabled),                                         \
        Impl::Desc{QT_TRANSLATE_NOOP("QObject", "A click on the tray icon connects the selected device instead of opening the main window. Double-click still opens it.")}) \
    callback(QString, tray_quick_connect_device_id, {},                                             \
        Impl::OnApply(&OnApply_tray_quick_connect_device_id),                                       \
        Impl::Sensitive{})                                                                          \
    callback(TaskbarStatusBehavior, battery_on_taskbar, {TaskbarStatusBehavior::Disable},          \
        Impl::OnApply(&OnApply_battery_on_taskbar))
// clang-format on

struct Fields {
#define DECLARE_FIELD(type, name, dft, ...) type name dft;
    SETTINGS_FIELDS(DECLARE_FIELD)
#undef DECLARE_FIELD
};

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

class Desc
{
public:
    Desc() = default;
    Desc(const char *sourceText) : _sourceText{sourceText} {}

    QString Description() const
    {
        return _sourceText ? QCoreApplication::translate("QObject", _sourceText) : QString{};
    }

private:
    const char *_sourceText{nullptr};
};

// clang-format off
class Sensitive {};
class Deprecated {};
// clang-format on

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

    void SetOption(Desc description)
    {
        _description = std::move(description);
    }

    void SetOption(Sensitive)
    {
        _isSensitive = true;
    }

    void SetOption(Deprecated)
    {
        _isDeprecated = true;
    }

    const OnApply &OnApply() const
    {
        return _onApply;
    }

    QString Description() const
    {
        return _description.Description();
    }

    bool IsSensitive() const
    {
        return _isSensitive;
    }

    bool IsDeprecated() const
    {
        return _isDeprecated;
    }

private:
    std::string_view _name;
    T _member;
    Impl::OnApply _onApply;
    Impl::Desc _description;
    bool _isSensitive{false}, _isDeprecated{false};
};

template <class T>
class BasicSafeAccessor
{
public:
    BasicSafeAccessor(std::mutex &lock, T &fields) : _lock{lock}, _fields{fields} {}
    BasicSafeAccessor(const BasicSafeAccessor &rhs) = delete;

    T *operator->()
    {
        return &_fields;
    }

private:
    std::lock_guard<std::mutex> _lock;
    T &_fields;
};
} // namespace Impl

// Increase this value when the current ABI cannot be backward compatible
// For example, the name or type of an old key has changed
//
constexpr inline uint32_t kFieldsAbiVersion = 1;

void OnApply_language_locale(const Fields &newFields);
void OnApply_appearance_mode(const Fields &newFields);
void OnApply_auto_run(const Fields &newFields);
void OnApply_low_audio_latency(const Fields &newFields);
void OnApply_automatic_ear_detection(const Fields &newFields);
void OnApply_rssi_min(const Fields &newFields);
void OnApply_device_address(const Fields &newFields);
void OnApply_tray_icon_battery(const Fields &newFields);
void OnApply_tray_quick_connect_enabled(const Fields &newFields);
void OnApply_tray_quick_connect_device_id(const Fields &newFields);
void OnApply_battery_on_taskbar(const Fields &newFields);

struct MetaFields {
#define DECLARE_META_FIELD(type, name, dft, ...)                                                   \
    Impl::MetaField<type Fields::*> name{TO_STRING(name), &Fields::name, __VA_ARGS__};
    SETTINGS_FIELDS(DECLARE_META_FIELD)
#undef DECLARE_FIELD
};

inline const auto &GetConstMetaFields()
{
    static MetaFields i;
    return i;
}

LoadResult Load();
void Save(Fields newFields);
void Apply();
Fields GetCurrent();
Fields GetDefault();

using ConstSafeAccessor = Impl::BasicSafeAccessor<const Fields>;

class ModifiableSafeAccessor : public Impl::BasicSafeAccessor<Fields>
{
public:
    using Impl::BasicSafeAccessor<Fields>::BasicSafeAccessor;

    ModifiableSafeAccessor(std::mutex &lock, Fields &fields);
    ~ModifiableSafeAccessor();

private:
    Fields _oldFields;
};

// ConstSafeAccessor ConstAccess();
ModifiableSafeAccessor ModifiableAccess();

} // namespace Core::Settings
