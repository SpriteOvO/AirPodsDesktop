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

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>

#include "../Logger.h"

namespace Core::GlobalMedia {

namespace {

constexpr auto kMprisPrefix = "org.mpris.MediaPlayer2.";
constexpr auto kMprisPath = "/org/mpris/MediaPlayer2";
constexpr auto kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr int kTimeoutMs = 1500;

QDBusConnection Bus()
{
    return QDBusConnection::sessionBus();
}

QStringList ListPlayers()
{
    QStringList players;
    const auto *iface = Bus().interface();
    if (iface == nullptr) {
        return players;
    }

    const auto names = iface->registeredServiceNames();
    if (!names.isValid()) {
        return players;
    }
    for (const auto &name : names.value()) {
        if (name.startsWith(kMprisPrefix)) {
            players.push_back(name);
        }
    }
    return players;
}

QString GetPlaybackStatus(const QString &service)
{
    auto message = QDBusMessage::createMethodCall(
        service, kMprisPath, "org.freedesktop.DBus.Properties", "Get");
    message << QString{kPlayerInterface} << QStringLiteral("PlaybackStatus");

    const auto reply = Bus().call(message, QDBus::Block, kTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().first().value<QDBusVariant>().variant().toString();
}

bool Invoke(const QString &service, const char *method)
{
    const auto message = QDBusMessage::createMethodCall(service, kMprisPath, kPlayerInterface, method);
    return Bus().call(message, QDBus::Block, kTimeoutMs).type() == QDBusMessage::ReplyMessage;
}

} // namespace

void Controller::Play()
{
    std::lock_guard<std::mutex> lock{_mutex};

    if (_pausedPlayers.empty()) {
        LOG(Trace, "Paused players vector is empty.");
        return;
    }

    for (const auto &name : _pausedPlayers) {
        const auto service = QString::fromStdString(name);

        // Someone else may have resumed it, or the player may be gone.
        if (GetPlaybackStatus(service) != QStringLiteral("Paused")) {
            continue;
        }

        if (Invoke(service, "Play")) {
            LOG(Trace, "Media played. Player: '{}'", name);
        }
        else {
            LOG(Warn, "Failed to play media. Player: '{}'", name);
        }
    }

    _pausedPlayers.clear();
}

void Controller::Pause()
{
    std::lock_guard<std::mutex> lock{_mutex};

    for (const auto &service : ListPlayers()) {
        if (GetPlaybackStatus(service) != QStringLiteral("Playing")) {
            continue;
        }

        if (Invoke(service, "Pause")) {
            LOG(Trace, "Media paused. Player: '{}'", service);
            _pausedPlayers.push_back(service.toStdString());
        }
        else {
            LOG(Warn, "Failed to pause media. Player: '{}'", service);
        }
    }
}
} // namespace Core::GlobalMedia
