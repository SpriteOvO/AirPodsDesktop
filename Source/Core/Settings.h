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

namespace Core::Settings {

namespace Impl {

template <class T>
class BasicSafeAccessor
{
public:
    BasicSafeAccessor(std::recursive_mutex &lock, T &fields) : _lock{lock}, _fields{fields} {}
    BasicSafeAccessor(const BasicSafeAccessor &rhs) = delete;

    T *operator->()
    {
        return &_fields;
    }

private:
    std::lock_guard<std::recursive_mutex> _lock;
    T &_fields;
};
} // namespace Impl

// Increase this value when the current ABI cannot be backward compatible
// For example, the name or type of an old key has changed
//
constexpr inline uint32_t kFieldsAbiVersion = 1;

// clang-format off
#define SETTINGS_FIELDS(callback)                                                                  \
    callback(bool, auto_run, {false}, Impl::OnApply(&OnApply_auto_run))                            \
    callback(bool, low_audio_latency, {false}, Impl::OnApply(&OnApply_low_audio_latency))          \
    callback(bool, automatic_ear_detection, {true})                                                \
    callback(QString, skipped_version, {})                                                         \
    callback(int16_t, rssi_min, {-80})                                                             \
    callback(bool, reduce_loud_sounds, {false}, Impl::OnApply(&OnApply_reduce_loud_sounds))        \
    callback(uint32_t, loud_volume_level, {40}, Impl::OnApply(&OnApply_loud_volume_level))         \
    callback(uint64_t, device_address, {0}, Impl::OnApply(&OnApply_device_address), Impl::Sensitive{})\
    callback(bool, tray_icon_battery, {false}, Impl::OnApply(&OnApply_tray_icon_battery))
// clang-format on

struct Fields {
#define DECLARE_FIELD(type, name, dft, ...) type name dft;
    SETTINGS_FIELDS(DECLARE_FIELD)
#undef DECLARE_FIELD
};

enum class LoadResult : uint32_t { AbiIncompatible, NoAbiField, Successful };

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

    ModifiableSafeAccessor(std::recursive_mutex &lock, Fields &fields);
    ~ModifiableSafeAccessor();

private:
    Fields _oldFields;
};

ConstSafeAccessor ConstAccess();
ModifiableSafeAccessor ModifiableAccess();

} // namespace Core::Settings
