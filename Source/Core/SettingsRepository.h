//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#pragma once

#include <memory>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace Core::Settings {

class Repository
{
public:
    virtual ~Repository() = default;

    virtual bool Contains(const QString &key) const = 0;
    virtual QVariant Read(const QString &key) const = 0;
    virtual QStringList Keys() const = 0;
    virtual void Write(const QString &key, const QVariant &value) = 0;
    virtual void Remove(const QString &key) = 0;
    virtual bool Sync() = 0;
};

class MemoryRepository final : public Repository
{
public:
    bool Contains(const QString &key) const override;
    QVariant Read(const QString &key) const override;
    QStringList Keys() const override;
    void Write(const QString &key, const QVariant &value) override;
    void Remove(const QString &key) override;
    bool Sync() override;

private:
    QHash<QString, QVariant> _values;
};

namespace Details {

bool MigrateLegacySettings(Repository &current, Repository &legacy);

} // namespace Details

std::unique_ptr<Repository> CreatePersistentRepository();

} // namespace Core::Settings
