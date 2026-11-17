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

#include "GlobalMedia_linux.h"

#if defined APD_OS_LINUX

#include <mutex>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QStringList>

#include "../Logger.h"

namespace Core::GlobalMedia {

namespace {

constexpr auto kMprisServicePrefix = "org.mpris.MediaPlayer2.";
constexpr auto kMprisPath = "/org/mpris/MediaPlayer2";
constexpr auto kMprisPlayerInterface = "org.mpris.MediaPlayer2.Player";

// These calls run on the GUI thread, because that is where advertisements are processed. A media
// player that stops answering must delay the interface by a bounded amount, not by QtDBus's
// 25-second default.
constexpr int kCallTimeoutMs = 1'000;

class SessionMprisBus final : public Details::MprisBus
{
public:
    QStringList ListPlayers() const override
    {
        const auto message = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"));

        const auto reply =
            QDBusConnection::sessionBus().call(message, QDBus::Block, kCallTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return {};
        }

        QStringList result;
        for (const auto &service : reply.arguments().at(0).toStringList()) {
            if (service.startsWith(kMprisServicePrefix)) {
                result.append(service);
            }
        }
        return result;
    }

    QString PlaybackStatus(const QString &service) const override
    {
        auto message = QDBusMessage::createMethodCall(
            service, kMprisPath, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Get"));
        message.setArguments(
            {QString::fromLatin1(kMprisPlayerInterface), QStringLiteral("PlaybackStatus")});

        const auto reply =
            QDBusConnection::sessionBus().call(message, QDBus::Block, kCallTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            // A confined player, such as a snap package, refuses messages from unconfined
            // processes. Say why instead of quietly treating it as "not playing".
            LOG(Warn, "Cannot read MPRIS playback status from '{}': {} '{}'.",
                service.toStdString(), reply.errorName().toStdString(),
                reply.errorMessage().toStdString());
            return {};
        }

        // Properties.Get answers with a variant, which QtDBus delivers in its own wrapper type.
        auto value = reply.arguments().at(0);
        if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
            value = value.value<QDBusVariant>().variant();
        }
        return value.toString();
    }

    void TogglePlayback(const QString &service) override
    {
        // Fire and forget: the player's reply carries nothing we use, and waiting for it would
        // extend the GUI-thread stall.
        QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(
            service, kMprisPath, QString::fromLatin1(kMprisPlayerInterface),
            QStringLiteral("PlayPause")));
    }
};

} // namespace

namespace Details {

namespace {

std::mutex g_busMutex;
std::shared_ptr<MprisBus> g_injectedBus;

} // namespace

std::shared_ptr<MprisBus> GetMprisBus()
{
    std::lock_guard<std::mutex> lock{g_busMutex};

    if (g_injectedBus != nullptr) {
        return g_injectedBus;
    }

    static const auto sessionBus = std::make_shared<SessionMprisBus>();
    return sessionBus;
}

void SetMprisBus(std::shared_ptr<MprisBus> bus)
{
    std::lock_guard<std::mutex> lock{g_busMutex};
    g_injectedBus = std::move(bus);
}

} // namespace Details

struct Controller::Impl {
    std::mutex mutex;
    QStringList pausedServices;
};

Controller::Controller() : _impl{std::make_unique<Impl>()}
{
}

Controller::~Controller() = default;

void Controller::Pause()
{
    const auto bus = Details::GetMprisBus();
    std::lock_guard<std::mutex> lock{_impl->mutex};

    _impl->pausedServices.clear();

    for (const auto &service : bus->ListPlayers()) {
        if (bus->PlaybackStatus(service) != QStringLiteral("Playing")) {
            continue;
        }

        LOG(Info, "Pause MPRIS player '{}'.", service.toStdString());
        bus->TogglePlayback(service);
        _impl->pausedServices.append(service);
    }
}

void Controller::Play()
{
    const auto bus = Details::GetMprisBus();
    std::lock_guard<std::mutex> lock{_impl->mutex};

    const auto services = bus->ListPlayers();
    QStringList stillPaused;

    for (const auto &service : _impl->pausedServices) {
        if (!services.contains(service)) {
            continue;
        }
        if (bus->PlaybackStatus(service) != QStringLiteral("Paused")) {
            continue;
        }

        LOG(Info, "Resume MPRIS player '{}'.", service.toStdString());
        bus->TogglePlayback(service);
        stillPaused.append(service);
    }

    _impl->pausedServices = stillPaused;
}

} // namespace Core::GlobalMedia

#endif
