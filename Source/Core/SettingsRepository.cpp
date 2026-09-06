//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "SettingsRepository.h"

#include <QSettings>

#include <Config.h>

namespace Core::Settings {

namespace {

class QSettingsRepository final : public Repository
{
public:
    explicit QSettingsRepository(QSettings::Format format)
        : _settings{format, QSettings::UserScope, Config::ProgramName, Config::ProgramName}
    {
    }

    bool Contains(const QString &key) const override
    {
        return _settings.contains(key);
    }

    QVariant Read(const QString &key) const override
    {
        return _settings.value(key);
    }

    QStringList Keys() const override
    {
        return _settings.allKeys();
    }

    void Write(const QString &key, const QVariant &value) override
    {
        _settings.setValue(key, value);
    }

    void Remove(const QString &key) override
    {
        _settings.remove(key);
    }

    bool Sync() override
    {
        _settings.sync();
        return _settings.status() == QSettings::NoError;
    }

private:
    mutable QSettings _settings;
};

} // namespace

bool MemoryRepository::Contains(const QString &key) const
{
    return _values.contains(key);
}

QVariant MemoryRepository::Read(const QString &key) const
{
    return _values.value(key);
}

QStringList MemoryRepository::Keys() const
{
    return _values.keys();
}

void MemoryRepository::Write(const QString &key, const QVariant &value)
{
    _values.insert(key, value);
}

void MemoryRepository::Remove(const QString &key)
{
    _values.remove(key);
}

bool MemoryRepository::Sync()
{
    return true;
}

namespace Details {

bool MigrateLegacySettings(Repository &current, Repository &legacy)
{
    constexpr auto abiKey = "abi_version";
    if (current.Contains(abiKey) || !legacy.Contains(abiKey)) {
        return false;
    }

    auto legacyKeys = legacy.Keys();
    legacyKeys.removeAll(abiKey);
    QStringList copiedKeys;
    for (const auto &key : legacyKeys) {
        if (!current.Contains(key)) {
            current.Write(key, legacy.Read(key));
            copiedKeys.push_back(key);
        }
    }
    // Write the ABI marker last so an interrupted copy is retried on the next launch.
    current.Write(abiKey, legacy.Read(abiKey));

    if (current.Sync()) {
        return true;
    }

    for (const auto &key : copiedKeys) {
        current.Remove(key);
    }
    current.Remove(abiKey);
    current.Sync();
    return false;
}

} // namespace Details

std::unique_ptr<Repository> CreatePersistentRepository()
{
#if defined APD_OS_WIN
    auto current = std::make_unique<QSettingsRepository>(QSettings::Registry64Format);
    QSettingsRepository legacy{QSettings::Registry32Format};
    Details::MigrateLegacySettings(*current, legacy);
    return current;
#else
    return std::make_unique<QSettingsRepository>(QSettings::NativeFormat);
#endif
}

} // namespace Core::Settings
