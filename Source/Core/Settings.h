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

#include <mutex>

#include <QSettings>

#include "../Helper.h"

namespace Core::Settings {

enum class TrayIconBatteryBehavior : uint32_t { Disable, WhenLowBattery, Always };
enum class LoadResult : uint32_t { AbiIncompatible, NoAbiField, Successful };

// clang-format off
#define SETTINGS_FIELDS(callback)                                                                  \
    callback(bool, auto_run, {false}, Impl::OnApply(&OnApply_auto_run))                            \
    callback(bool, low_audio_latency, {false},                                                     \
        Impl::OnApply(&OnApply_low_audio_latency),                                                 \
        Impl::Desc{QObject::tr("It fixes short audio playback problems, but may increase battery consumption.")}) \
    callback(bool, automatic_ear_detection, {true},                                                \
        Impl::OnApply(&OnApply_automatic_ear_detection),                                           \
        Impl::Desc{QObject::tr("It automatically pauses or resumes media when your AirPods are taken out or put in your ears.")}) \
    callback(QString, skipped_version, {})                                                         \
    callback(int16_t, rssi_min, {-80}, Impl::OnApply(&OnApply_rssi_min))                           \
    callback(bool, reduce_loud_sounds, {false}, Impl::Deprecated())                                \
    callback(uint32_t, loud_volume_level, {40}, Impl::Deprecated())                                \
    callback(uint64_t, device_address, {0},                                                        \
        Impl::OnApply(&OnApply_device_address),                                                    \
        Impl::Sensitive{})                                                                         \
    callback(TrayIconBatteryBehavior, tray_icon_battery, {TrayIconBatteryBehavior::Disable},       \
        Impl::OnApply(&OnApply_tray_icon_battery))
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
    Desc(QString description) : _description{std::move(description)} {}

    const QString &Description() const
    {
        return _description;
    }

private:
    QString _description;
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

    const QString &Description() const
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

void OnApply_auto_run(const Fields &newFields);
void OnApply_low_audio_latency(const Fields &newFields);
void OnApply_automatic_ear_detection(const Fields &newFields);
void OnApply_rssi_min(const Fields &newFields);
void OnApply_device_address(const Fields &newFields);
void OnApply_tray_icon_battery(const Fields &newFields);

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
