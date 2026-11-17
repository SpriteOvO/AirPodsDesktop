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

#include <QHash>
#include <QStringList>
#include <QtTest>

#if defined APD_OS_LINUX

    #include "Source/Core/GlobalMedia.h"

namespace {

const auto kPlayerA = QStringLiteral("org.mpris.MediaPlayer2.a");
const auto kPlayerB = QStringLiteral("org.mpris.MediaPlayer2.b");

const auto kPlaying = QStringLiteral("Playing");
const auto kPaused = QStringLiteral("Paused");
const auto kStopped = QStringLiteral("Stopped");

class FakeMprisBus final : public Core::GlobalMedia::Details::MprisBus
{
public:
    QStringList players;
    QHash<QString, QString> status;
    QStringList toggled;

    QStringList ListPlayers() const override
    {
        return players;
    }

    QString PlaybackStatus(const QString &service) const override
    {
        return status.value(service);
    }

    void TogglePlayback(const QString &service) override
    {
        toggled.append(service);
        status[service] = status.value(service) == kPlaying ? kPaused : kPlaying;
    }
};

} // namespace

class GlobalMediaTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void pausesOnlyPlayingPlayers();
    void resumesOnlyPlayersItPaused();
    void doesNotResumePlayersResumedBySomeoneElse();
    void dropsPlayersThatWentAway();
    void ignoresPlayersWithoutAReadableStatus();

private:
    std::shared_ptr<FakeMprisBus> _bus;
};

void GlobalMediaTests::init()
{
    _bus = std::make_shared<FakeMprisBus>();
    Core::GlobalMedia::Details::SetMprisBus(_bus);
}

void GlobalMediaTests::cleanup()
{
    Core::GlobalMedia::Details::SetMprisBus(nullptr);
}

void GlobalMediaTests::pausesOnlyPlayingPlayers()
{
    _bus->players = {kPlayerA, kPlayerB, QStringLiteral("org.mpris.MediaPlayer2.c")};
    _bus->status = {
        {kPlayerA, kPlaying},
        {kPlayerB, kPaused},
        {QStringLiteral("org.mpris.MediaPlayer2.c"), kStopped},
    };

    Core::GlobalMedia::Pause();

    QCOMPARE(_bus->toggled, QStringList{kPlayerA});
    QCOMPARE(_bus->status.value(kPlayerA), kPaused);
}

void GlobalMediaTests::resumesOnlyPlayersItPaused()
{
    _bus->players = {kPlayerA, kPlayerB};
    _bus->status = {{kPlayerA, kPlaying}, {kPlayerB, kPlaying}};

    Core::GlobalMedia::Pause();
    QCOMPARE(_bus->toggled, (QStringList{kPlayerA, kPlayerB}));

    _bus->toggled.clear();
    Core::GlobalMedia::Play();

    QCOMPARE(_bus->toggled, (QStringList{kPlayerA, kPlayerB}));
    QCOMPARE(_bus->status.value(kPlayerA), kPlaying);
    QCOMPARE(_bus->status.value(kPlayerB), kPlaying);
}

void GlobalMediaTests::doesNotResumePlayersResumedBySomeoneElse()
{
    _bus->players = {kPlayerA};
    _bus->status = {{kPlayerA, kPlaying}};

    Core::GlobalMedia::Pause();

    // The user resumed playback themselves while the AirPods were out.
    _bus->status[kPlayerA] = kPlaying;
    _bus->toggled.clear();

    Core::GlobalMedia::Play();

    QVERIFY(_bus->toggled.isEmpty());
    QCOMPARE(_bus->status.value(kPlayerA), kPlaying);
}

void GlobalMediaTests::dropsPlayersThatWentAway()
{
    _bus->players = {kPlayerA};
    _bus->status = {{kPlayerA, kPlaying}};

    Core::GlobalMedia::Pause();

    _bus->players.clear();
    _bus->toggled.clear();

    Core::GlobalMedia::Play();
    QVERIFY(_bus->toggled.isEmpty());

    // The vanished player is forgotten, so a later Play cannot touch it even if it comes back.
    _bus->players = {kPlayerA};
    _bus->status[kPlayerA] = kPaused;
    Core::GlobalMedia::Play();
    QVERIFY(_bus->toggled.isEmpty());
}

void GlobalMediaTests::ignoresPlayersWithoutAReadableStatus()
{
    _bus->players = {kPlayerA, kPlayerB};
    _bus->status = {{kPlayerA, kPlaying}}; // kPlayerB does not answer Properties.Get

    Core::GlobalMedia::Pause();

    QCOMPARE(_bus->toggled, QStringList{kPlayerA});
}

QTEST_GUILESS_MAIN(GlobalMediaTests)

    #include "GlobalMediaTests.moc"

#endif
