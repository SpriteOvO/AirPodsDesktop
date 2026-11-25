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

#include <QtTest>

#include <atomic>
#include <chrono>
#include <future>
#include <cmath>
#include <memory>
#include <mutex>

#include <QDataStream>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include "Source/Core/QuickConnect.h"
#if defined APD_OS_LINUX
    #include "Source/Core/QuickConnect_linux.h"
#endif
#include "Source/Core/PlaybackMonitor.h"
#include "Source/Gui/TrayActivation.h"

using namespace std::chrono_literals;

namespace {

using Core::QuickConnect::Controller;
using Core::QuickConnect::Device;
using Core::QuickConnect::Outcome;

// Short enough to keep the suite fast, long enough that a queued completion always lands first.
constexpr auto kTestResolveTimeout = 250ms;

class FakeBackend final : public Core::QuickConnect::Backend
{
public:
    std::vector<Device> devices{{"{A}", "AirPods", false}};
    Controller *controller{};
    std::atomic<int> listCalls{};
    std::atomic<int> reconnectCalls{};
    std::atomic<int> disconnectCalls{};
    bool disconnectResult{true};
    QString disconnectedId;
    std::atomic<int> setControllerCalls{};
    std::atomic<int> stopObservingCalls{};

    void SetController(Controller *value) override
    {
        controller = value;
        ++setControllerCalls;
    }

    std::vector<Device> ListDevices(std::stop_token) override
    {
        ++listCalls;
        return devices;
    }

    bool RequestReconnect(const QString &id, std::stop_token) override
    {
        ++reconnectCalls;
        return id == "{A}";
    }

    bool RequestDisconnect(const QString &id, std::stop_token) override
    {
        disconnectedId = id;
        ++disconnectCalls;
        return disconnectResult;
    }

    void StopObserving() override
    {
        ++stopObservingCalls;
    }
};

// Blocks inside ListDevices until released, so a test can prove the GUI thread was never in there.
class BlockingBackend final : public Core::QuickConnect::Backend
{
public:
    std::promise<void> entered;
    bool connected{};

    std::vector<Device> ListDevices(std::stop_token stopToken) override
    {
        std::stop_callback cancel{stopToken, [this] { Unblock(); }};
        entered.set_value();
        _release.wait();
        return {{"{A}", "AirPods", connected}};
    }

    bool RequestReconnect(const QString &, std::stop_token) override
    {
        return false;
    }

    void Unblock()
    {
        std::call_once(_releaseOnce, [this] { _releasePromise.set_value(); });
    }

private:
    std::promise<void> _releasePromise;
    std::shared_future<void> _release{_releasePromise.get_future()};
    std::once_flag _releaseOnce;
};

class BackendUnblockGuard final
{
public:
    explicit BackendUnblockGuard(std::shared_ptr<BlockingBackend> backend)
        : _backend{std::move(backend)}
    {
    }
    ~BackendUnblockGuard()
    {
        _backend->Unblock();
    }

private:
    std::shared_ptr<BlockingBackend> _backend;
};

} // namespace

class QuickConnectTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void nativePlaybackProbeDetectsExternalPlayback()
    {
        if (!qEnvironmentVariableIsSet("APD_RUN_AUDIO_SMOKE_TEST")) {
            QSKIP("Set APD_RUN_AUDIO_SMOKE_TEST=1 to exercise Windows audio output");
        }
#if defined APD_OS_WIN
        const auto probe = Core::GlobalMedia::CreatePlaybackProbe();
        const auto read = [&] { return std::async(std::launch::async, probe).get(); };
        const auto initial = read();
        QVERIFY(initial.has_value());
        if (*initial) {
            QSKIP("Existing audio sessions are running; stop playback before the audio smoke test");
        }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file{directory.filePath("playback.wav")};
        QVERIFY(file.open(QIODevice::WriteOnly));
        constexpr int sampleRate = 8000;
        constexpr int sampleCount = sampleRate * 2;
        QDataStream wav{&file};
        wav.setByteOrder(QDataStream::LittleEndian);
        wav.writeRawData("RIFF", 4);
        wav << quint32(36 + sampleCount * 2);
        wav.writeRawData("WAVEfmt ", 8);
        wav << quint32(16) << quint16(1) << quint16(1) << quint32(sampleRate)
            << quint32(sampleRate * 2) << quint16(2) << quint16(16);
        wav.writeRawData("data", 4);
        wav << quint32(sampleCount * 2);
        for (int i = 0; i < sampleCount; ++i) {
            wav << qint16(100 * std::sin(i * 2 * 3.141592653589793 * 250 / sampleRate));
        }
        file.close();
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("APD_PLAYBACK_TEST_WAV", file.fileName());
        QProcess player;
        player.setProcessEnvironment(environment);
        player.start(
            "powershell.exe",
            {"-NoProfile", "-NonInteractive", "-Command",
             "[System.Media.SoundPlayer]::new($env:APD_PLAYBACK_TEST_WAV).PlaySync()"});
        QVERIFY(player.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(read() == std::optional<bool>{true}, 5000);
        QVERIFY(player.waitForFinished(5000));
        QCOMPARE(player.exitCode(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(read() == std::optional<bool>{false}, 5000);
#else
        QSKIP("The native playback smoke test requires Windows");
#endif
    }

    void manualClickJoinsAutomaticConnectedCheck()
    {
        auto backend = std::make_shared<BlockingBackend>();
        backend->connected = true;
        BackendUnblockGuard guard{backend};
        Controller controller{backend, kTestResolveTimeout};
        controller.SetDeviceId("{A}");
        controller.SetEnabled(true);
        controller.SetAudioAutoConnectEnabled(true);
        QSignalSpy outcomes{&controller, &Controller::OutcomeChanged};
        controller.OnPlaybackStateChanged(true);
        auto entered = backend->entered.get_future();
        QVERIFY(entered.wait_for(1s) == std::future_status::ready);
        QCOMPARE(controller.Request(), Outcome::RequestStarted);
        backend->Unblock();
        QTRY_VERIFY(!controller.IsBusy());
        QCOMPARE(outcomes.last().at(0).value<Outcome>(), Outcome::AlreadyConnected);
    }

    void playbackConnectsIndependentlyOfTrayAndDeduplicates()
    {
        auto backend = std::make_shared<FakeBackend>();
        Controller controller{backend, kTestResolveTimeout};
        controller.SetDeviceId("{A}");
        controller.SetAudioAutoConnectEnabled(true);
        QVERIFY(!controller.IsEnabled());
        QSignalSpy outcomes{&controller, &Controller::OutcomeChanged};
        controller.OnPlaybackStateChanged(true);
        QTRY_COMPARE(outcomes.count(), 1);
        QCOMPARE(outcomes.last().at(0).value<Outcome>(), Outcome::RequestStarted);
        controller.OnPlaybackStateChanged(true);
        controller.OnPlaybackStateChanged(false);
        controller.OnPlaybackStateChanged(true);
        QCOMPARE(backend->reconnectCalls.load(), 1);
        controller.OnEndpointStateChanged("{A}", true);
        QCOMPARE(outcomes.last().at(0).value<Outcome>(), Outcome::Connected);
        controller.OnPlaybackStateChanged(true);
        QCOMPARE(backend->listCalls.load(), 1);
    }

    void automaticConnectedCheckIsSilent()
    {
        auto backend = std::make_shared<FakeBackend>();
        backend->devices.front().connected = true;
        Controller controller{backend, kTestResolveTimeout};
        controller.SetDeviceId("{A}");
        controller.SetAudioAutoConnectEnabled(true);
        QSignalSpy outcomes{&controller, &Controller::OutcomeChanged};
        controller.OnPlaybackStateChanged(true);
        QTRY_VERIFY(!controller.IsBusy());
        QCOMPARE(backend->listCalls.load(), 1);
        QCOMPARE(outcomes.count(), 0);
        controller.SetEnabled(true);
        QCOMPARE(controller.Request(), Outcome::RequestStarted);
        QTRY_VERIFY(
            !outcomes.isEmpty() &&
            outcomes.last().at(0).value<Outcome>() == Outcome::AlreadyConnected);
        QCOMPARE(backend->reconnectCalls.load(), 0);
        QCOMPARE(outcomes.count(), 1);
    }

    void disabledOrUnconfiguredAutomaticConnectDoesNothing()
    {
        auto backend = std::make_shared<FakeBackend>();
        Controller controller{backend, kTestResolveTimeout};
        controller.SetEnabled(true);
        controller.SetDeviceId("{A}");
        controller.OnPlaybackStateChanged(true);
        controller.SetDeviceId({});
        controller.SetAudioAutoConnectEnabled(true);
        controller.OnPlaybackStateChanged(false);
        controller.OnPlaybackStateChanged(true);
        QTest::qWait(20);
        QCOMPARE(backend->listCalls.load(), 0);
    }

    void automaticFailureRequiresCooldownAndNewPlayback()
    {
        auto backend = std::make_shared<FakeBackend>();
        backend->devices = {{"{B}", "Unavailable headphones", false}};
        Controller controller{backend, kTestResolveTimeout, 80ms};
        controller.SetDeviceId("{B}");
        controller.SetAudioAutoConnectEnabled(true);
        QSignalSpy outcomes{&controller, &Controller::OutcomeChanged};
        controller.OnPlaybackStateChanged(true);
        QTRY_COMPARE(outcomes.count(), 1);
        QCOMPARE(outcomes.last().at(0).value<Outcome>(), Outcome::Failed);
        controller.OnPlaybackStateChanged(false);
        controller.OnPlaybackStateChanged(true);
        QCOMPARE(backend->reconnectCalls.load(), 1);
        QTest::qWait(100);
        controller.OnPlaybackStateChanged(true);
        QCOMPARE(backend->reconnectCalls.load(), 1);
        controller.OnPlaybackStateChanged(false);
        controller.OnPlaybackStateChanged(true);
        QTRY_COMPARE(outcomes.count(), 2);
        QCOMPARE(backend->reconnectCalls.load(), 2);
        controller.SetAudioAutoConnectEnabled(false);
        QTest::qWait(100);
        controller.OnPlaybackStateChanged(false);
        controller.OnPlaybackStateChanged(true);
        QCOMPARE(backend->reconnectCalls.load(), 2);
    }

    void playbackMonitorDebouncesSilenceAndStopsWhenDisabled()
    {
        std::atomic<int> sample{1};
        std::atomic<int> polls{};
        Core::GlobalMedia::PlaybackMonitor monitor{
            [&]() -> std::optional<bool> {
                ++polls;
                const int value = sample.load();
                return value < 0 ? std::nullopt : std::optional<bool>{value != 0};
            },
            5ms, 80ms};
        QSignalSpy changed{&monitor, &Core::GlobalMedia::PlaybackMonitor::PlaybackChanged};
        QTest::qWait(10);
        QCOMPARE(polls.load(), 0);
        monitor.SetEnabled(true);
        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(changed.last().at(0).toBool(), true);
        sample = 0;
        QTest::qWait(20);
        sample = 1;
        QTest::qWait(20);
        QCOMPARE(changed.count(), 1);
        sample = -1;
        QTest::qWait(100);
        QCOMPARE(changed.count(), 1);
        sample = 0;
        QTRY_COMPARE(changed.count(), 2);
        QCOMPARE(changed.last().at(0).toBool(), false);
        monitor.SetEnabled(false);
        const int stoppedPolls = polls.load();
        sample = 1;
        QTest::qWait(30);
        QCOMPARE(polls.load(), stoppedPolls);
        QCOMPARE(changed.count(), 2);
        monitor.SetEnabled(true);
        QTRY_COMPARE(changed.count(), 3);
    }

    // Controller
    void disabledRequestDoesNotCallBackend();
    void emptyDeviceIdReturnsNoDevice();
    void configuredButMissingDeviceReportsUnavailable();
    void connectedDeviceReturnsAlreadyConnected();
    void failedReconnectReturnsFailed();
    void repeatedClicksAlwaysReportProgress();
    void pendingRequestKeepsOriginalDevice();
    void endpointActivationCompletesRequest();
    void resolveTimerFiresOnItsOwn();
    void completionStopsBackendObservation();
    void controllerDetachesBackendOnDestruction();
    void disconnectOutcomes();
    void disconnectCompletionAndTimeout();
#if defined APD_OS_LINUX
    void parsesBluezManagedObjects();
    void linuxBackendDisconnectMethod();
    void linuxDisconnectAlreadyGone();
#endif

    // Threading
    void deviceRefreshDoesNotBlockTheGuiThread();
    void reconnectRequestDoesNotBlockTheGuiThread();
    void controllerDestructionCancelsInFlightRefresh();

    // Tray activation
    void singleClickOpensMainWindowWhenQuickConnectIsDisabled();
    void singleClickRequestsQuickConnectImmediately();
    void doubleClickDoesNotOpenMainWindow();
    void contextMenuDoesNotRequestQuickConnect();
    void unknownActivationDoesNothing();
};

void QuickConnectTests::initTestCase()
{
    qRegisterMetaType<Outcome>();
}

//////////////////////////////////////////////////
// Controller
//

void QuickConnectTests::disabledRequestDoesNotCallBackend()
{
    auto backend = std::make_shared<FakeBackend>();
    Controller controller{backend, kTestResolveTimeout};
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Outcome::Disabled);
    QCOMPARE(backend->reconnectCalls.load(), 0);
}

void QuickConnectTests::emptyDeviceIdReturnsNoDevice()
{
    auto backend = std::make_shared<FakeBackend>();
    Controller controller{backend, kTestResolveTimeout};
    controller.SetEnabled(true);

    QCOMPARE(controller.Request(), Outcome::NoDevice);
    QCOMPARE(backend->reconnectCalls.load(), 0);
}

// "Nothing configured" and "the configured device is not around" are different problems and the
// tray says different things about them.
void QuickConnectTests::configuredButMissingDeviceReportsUnavailable()
{
    auto backend = std::make_shared<FakeBackend>();
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{Missing}");

    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QVERIFY(spy.wait(2'000));
    QCOMPARE(spy.at(0).at(0).value<Outcome>(), Outcome::DeviceUnavailable);
    QCOMPARE(backend->reconnectCalls.load(), 0);
}

void QuickConnectTests::connectedDeviceReturnsAlreadyConnected()
{
    auto backend = std::make_shared<FakeBackend>();
    backend->devices = {{"{A}", "AirPods", true}};
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QVERIFY(spy.wait(2'000));
    QCOMPARE(spy.at(0).at(0).value<Outcome>(), Outcome::AlreadyConnected);
    QCOMPARE(backend->reconnectCalls.load(), 0);
}

void QuickConnectTests::failedReconnectReturnsFailed()
{
    auto backend = std::make_shared<FakeBackend>();
    backend->devices = {{"{B}", "AirPods Pro", false}};
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{B}");

    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QVERIFY(spy.wait(2'000));
    QCOMPARE(spy.at(0).at(0).value<Outcome>(), Outcome::Failed);
    QCOMPARE(backend->reconnectCalls.load(), 1);
}

// An impatient user clicking again must always get an answer, and must never start a second
// reconnect - whichever internal phase the first one happens to be in.
void QuickConnectTests::repeatedClicksAlwaysReportProgress()
{
    auto backend = std::make_shared<FakeBackend>();
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    // First click starts the worker; the second lands while it is still running.
    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<Outcome>(), Outcome::RequestStarted);

    // Wait for the worker's completion, then click again while the resolve timer is pending.
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 2'000);
    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QCOMPARE(spy.count(), 3);

    QCOMPARE(backend->listCalls.load(), 1);
    QCOMPARE(backend->reconnectCalls.load(), 1);
}

void QuickConnectTests::pendingRequestKeepsOriginalDevice()
{
    auto backend = std::make_shared<FakeBackend>();
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2'000);

    // Changing the selection mid-flight must not retarget the request already in progress.
    controller.SetDeviceId("{B}");
    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QCOMPARE(backend->listCalls.load(), 1);
    QCOMPARE(backend->reconnectCalls.load(), 1);
    QCOMPARE(controller.OnEndpointStateChanged("{A}", true), Outcome::Connected);
}

void QuickConnectTests::endpointActivationCompletesRequest()
{
    auto backend = std::make_shared<FakeBackend>();
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2'000);
    spy.clear();

    // Windows reports the container id in canonical `{GUID}` form; matching is case-insensitive.
    QCOMPARE(controller.OnEndpointStateChanged("a", true), Outcome::Connected);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<Outcome>(), Outcome::Connected);
    QCOMPARE(spy.at(0).at(1).toString(), QString{"AirPods"});

    // A repeat notification for an already-completed request changes nothing.
    QCOMPARE(controller.OnEndpointStateChanged("{A}", true), Outcome::Connected);
    QCOMPARE(spy.count(), 1);
}

// Drives the real QTimer the constructor wires up, rather than calling the slot by hand.
void QuickConnectTests::resolveTimerFiresOnItsOwn()
{
    auto backend = std::make_shared<FakeBackend>();
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2'000);
    spy.clear();

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5'000);
    QCOMPARE(spy.at(0).at(0).value<Outcome>(), Outcome::TimedOut);
    QCOMPARE(spy.at(0).at(1).toString(), QString{"AirPods"});

    // The timer is single-shot; nothing else arrives.
    QCOMPARE(controller.ResolveTimedOut(), Outcome::TimedOut);
    QCOMPARE(spy.count(), 1);
}

// Leaving the backend observing after a request resolves is what made it keep doing expensive work
// for every audio device on the machine.
void QuickConnectTests::completionStopsBackendObservation()
{
    auto backend = std::make_shared<FakeBackend>();
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2'000);
    QCOMPARE(backend->stopObservingCalls.load(), 0);

    QCOMPARE(controller.OnEndpointStateChanged("{A}", true), Outcome::Connected);
    QCOMPARE(backend->stopObservingCalls.load(), 1);
}

void QuickConnectTests::controllerDetachesBackendOnDestruction()
{
    auto backend = std::make_shared<FakeBackend>();
    {
        Controller controller{backend, kTestResolveTimeout};
        QCOMPARE(backend->controller, &controller);
    }

    QVERIFY(backend->controller == nullptr);
    QCOMPARE(backend->setControllerCalls.load(), 2);
}

void QuickConnectTests::disconnectOutcomes()
{
    auto backend = std::make_shared<FakeBackend>();
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    QCOMPARE(controller.RequestDisconnect(), Outcome::Disabled);
    controller.SetEnabled(true);
    QCOMPARE(controller.RequestDisconnect(), Outcome::NoDevice);
    controller.SetDeviceId("missing");
    QCOMPARE(controller.RequestDisconnect(), Outcome::DisconnectStarted);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, 2000);
    QCOMPARE(spy.last().at(0).value<Outcome>(), Outcome::DeviceUnavailable);
    controller.SetDeviceId("{A}");
    QCOMPARE(controller.RequestDisconnect(), Outcome::DisconnectStarted);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 4, 2000);
    QCOMPARE(spy.last().at(0).value<Outcome>(), Outcome::AlreadyDisconnected);
    QCOMPARE(backend->disconnectCalls.load(), 0);
    backend->devices = {{"{A}", "AirPods", true}};
    backend->disconnectResult = false;
    QCOMPARE(controller.RequestDisconnect(), Outcome::DisconnectStarted);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 5, 2000);
    QCOMPARE(spy.last().at(0).value<Outcome>(), Outcome::DisconnectFailed);
    QCOMPARE(backend->disconnectCalls.load(), 1);
}

void QuickConnectTests::disconnectCompletionAndTimeout()
{
    auto backend = std::make_shared<FakeBackend>();
    backend->devices = {{"{A}", "AirPods", true}, {"{B}", "Other", true}};
    Controller controller{backend, kTestResolveTimeout};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");
    QCOMPARE(controller.RequestDisconnect(), Outcome::DisconnectStarted);
    QCOMPARE(controller.Request(), Outcome::DisconnectStarted);
    QTRY_COMPARE_WITH_TIMEOUT(backend->disconnectCalls.load(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 2000);
    controller.SetDeviceId("{B}");
    QCOMPARE(controller.OnEndpointStateChanged("{B}", false), Outcome::DisconnectStarted);
    QCOMPARE(controller.OnEndpointStateChanged("{A}", true), Outcome::DisconnectStarted);
    QCOMPARE(controller.OnEndpointStateChanged("a", false), Outcome::Disconnected);
    QCOMPARE(spy.last().at(0).value<Outcome>(), Outcome::Disconnected);
    QCOMPARE(spy.last().at(1).toString(), QString{"AirPods"});
    QCOMPARE(backend->disconnectedId, QString{"{A}"});
    QCOMPARE(backend->stopObservingCalls.load(), 1);
    controller.SetDeviceId("{A}");
    QCOMPARE(controller.RequestDisconnect(), Outcome::DisconnectStarted);
    QTRY_COMPARE_WITH_TIMEOUT(spy.last().at(0).value<Outcome>(), Outcome::DisconnectTimedOut, 2000);
    QCOMPARE(backend->stopObservingCalls.load(), 2);
}

#if defined APD_OS_LINUX

namespace {

// One entry of the ObjectManager tree, shaped the way Core::Bluez::GetManagedObjects() returns it.
QVariantMap DeviceObject(const QString &address, const QVariantMap &overrides = {})
{
    QVariantMap properties{
        {"Address", address},
        {"Paired", true},
        {"Connected", false},
        {"Alias", QString{}},
        {"Name", QString{}},
        {"Class", 0u},
        {"UUIDs", QStringList{}},
    };
    for (auto iter = overrides.begin(); iter != overrides.end(); ++iter) {
        properties.insert(iter.key(), iter.value());
    }
    return QVariantMap{{QStringLiteral("org.bluez.Device1"), properties}};
}

} // namespace

void QuickConnectTests::parsesBluezManagedObjects()
{
    using namespace Core::QuickConnect::Details;

    QVariantMap objects;
    objects.insert(
        QStringLiteral("/org/bluez/hci0/dev_A4_C1_38_00_00_01"),
        DeviceObject(
            "A4:C1:38:00:00:01",
            {{"Alias", "AirPods Pro"},
             {"Class", 0x240404u},
             {"UUIDs", QStringList{QStringLiteral("0000110d-0000-1000-8000-00805f9b34fb")}},
             {"Connected", true}}));
    // Audio UUID with a blank class, which is what LE-only streams often report.
    objects.insert(
        QStringLiteral("/org/bluez/hci0/dev_A4_C1_38_00_00_02"),
        DeviceObject(
            "A4:C1:38:00:00:02",
            {{"Name", "Speaker"},
             {"UUIDs", QStringList{QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb")}}}));
    // A paired keyboard: Quick Connect only offers devices that can play audio.
    objects.insert(
        QStringLiteral("/org/bluez/hci0/dev_A4_C1_38_00_00_03"),
        DeviceObject(
            "A4:C1:38:00:00:03",
            {{"Alias", "Keyboard"},
             {"Class", 0x000540u},
             {"UUIDs", QStringList{QStringLiteral("00001124-0000-1000-8000-00805f9b34fb")}}}));
    // An audio device that is not paired.
    objects.insert(
        QStringLiteral("/org/bluez/hci0/dev_A4_C1_38_00_00_04"),
        DeviceObject(
            "A4:C1:38:00:00:04", {{"Alias", "Speaker"}, {"Paired", false}, {"Class", 0x240404u}}));
    // The adapter itself carries no Device1 interface.
    objects.insert(
        QStringLiteral("/org/bluez/hci0"),
        QVariantMap{{"org.bluez.Adapter1", QVariantMap{{"Address", "A4:C1:38:00:00:00"}}}});

    const auto devices = ParseManagedObjects(objects);
    QCOMPARE(devices.size(), std::size_t{2});

    QCOMPARE(devices.at(0).id, QString{"A4:C1:38:00:00:01"});
    QCOMPARE(devices.at(0).name, QString{"AirPods Pro"});
    QCOMPARE(devices.at(0).path, QString{"/org/bluez/hci0/dev_A4_C1_38_00_00_01"});
    QVERIFY(devices.at(0).connected);

    // No alias, so the friendly name takes over.
    QCOMPARE(devices.at(1).id, QString{"A4:C1:38:00:00:02"});
    QCOMPARE(devices.at(1).name, QString{"Speaker"});
    QVERIFY(!devices.at(1).connected);

    const auto path = FindObjectPathById(objects, "a4:c1:38:00:00:02");
    QVERIFY(path.has_value());
    QCOMPARE(path.value(), QString{"/org/bluez/hci0/dev_A4_C1_38_00_00_02"});
    QVERIFY(!FindObjectPathById(objects, "AA:BB:CC:DD:EE:FF").has_value());

    // The mask keeps the last four characters, which for a MAC address ends mid-octet.
    QCOMPARE(MaskDeviceId("A4:C1:38:00:00:01"), QString{"***0:01"});
    QCOMPARE(MaskDeviceId(""), QString{"<empty>"});
}

void QuickConnectTests::linuxBackendDisconnectMethod()
{
    using namespace Core::QuickConnect;
    QVariantMap objects;
    const QString path = "/org/bluez/hci0/dev_A4_C1_38_00_00_01";
    objects.insert(path, DeviceObject("A4:C1:38:00:00:01", {{"Class", 0x240404u}, {"Connected", true}}));
    QDBusMessage sent;
    int calls = 0;
    QString errorName;
    auto backend = CreateLinuxBackend(Details::Transport{
        [&] { return objects; },
        [&](const QDBusMessage &message) {
            sent = message;
            ++calls;
            return errorName.isEmpty() ? message.createReply()
                                       : message.createErrorReply(errorName, "failed");
        }});
    QVERIFY(backend->RequestDisconnect("a4:c1:38:00:00:01", {}));
    QCOMPARE(calls, 1);
    QCOMPARE(sent.service(), QString{"org.bluez"});
    QCOMPARE(sent.path(), path);
    QCOMPARE(sent.interface(), QString{"org.bluez.Device1"});
    QCOMPARE(sent.member(), QString{"Disconnect"});
    backend->StopObserving();
    errorName = "org.freedesktop.DBus.Error.NoReply";
    QVERIFY(backend->RequestDisconnect("A4:C1:38:00:00:01", {}));
    backend->StopObserving();
    errorName = "org.bluez.Error.Failed";
    QVERIFY(!backend->RequestDisconnect("A4:C1:38:00:00:01", {}));
    QVERIFY(!backend->RequestDisconnect("missing", {}));
    QCOMPARE(calls, 3);
    std::stop_source stop;
    stop.request_stop();
    QVERIFY(!backend->RequestDisconnect("A4:C1:38:00:00:01", stop.get_token()));
    QCOMPARE(calls, 3);
}

void QuickConnectTests::linuxDisconnectAlreadyGone()
{
    using namespace Core::QuickConnect;
    QVariantMap objects;
    objects.insert("/org/bluez/hci0/dev_A4_C1_38_00_00_01",
                   DeviceObject("A4:C1:38:00:00:01", {{"Class", 0x240404u}, {"Connected", true}}));
    auto backend = CreateLinuxBackend(Details::Transport{
        [objects] { return objects; },
        [](const QDBusMessage &message) {
            return message.createErrorReply("org.bluez.Error.NotConnected", "not connected");
        }});
    Controller controller{backend, kTestResolveTimeout};
    controller.SetEnabled(true);
    controller.SetDeviceId("A4:C1:38:00:00:01");
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    QCOMPARE(controller.RequestDisconnect(), Outcome::DisconnectStarted);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
    QCOMPARE(spy.last().at(0).value<Outcome>(), Outcome::Disconnected);
}

#endif

//////////////////////////////////////////////////
// Threading
//

void QuickConnectTests::deviceRefreshDoesNotBlockTheGuiThread()
{
    auto backend = std::make_shared<BlockingBackend>();
    Controller controller{backend, kTestResolveTimeout};
    BackendUnblockGuard unblockGuard{backend};
    QSignalSpy spy{&controller, &Controller::DevicesChanged};

    controller.RefreshDevices();
    QVERIFY(backend->entered.get_future().wait_for(2s) == std::future_status::ready);
    QCOMPARE(spy.count(), 0);

    backend->Unblock();
    QVERIFY(spy.wait(2'000));
    QCOMPARE(controller.Devices().size(), std::size_t{1});
}

void QuickConnectTests::reconnectRequestDoesNotBlockTheGuiThread()
{
    auto backend = std::make_shared<BlockingBackend>();
    Controller controller{backend, kTestResolveTimeout};
    BackendUnblockGuard unblockGuard{backend};
    QSignalSpy spy{&controller, &Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Outcome::RequestStarted);
    QVERIFY(backend->entered.get_future().wait_for(2s) == std::future_status::ready);
    QCOMPARE(spy.count(), 0);

    backend->Unblock();
    QVERIFY(spy.wait(2'000));
    QCOMPARE(spy.at(0).at(0).value<Outcome>(), Outcome::Failed);
}

// Quitting while a refresh is in flight must not hang, and must not touch the dead Controller.
void QuickConnectTests::controllerDestructionCancelsInFlightRefresh()
{
    auto backend = std::make_shared<BlockingBackend>();
    {
        Controller controller{backend, kTestResolveTimeout};
        controller.RefreshDevices();
        QVERIFY(backend->entered.get_future().wait_for(2s) == std::future_status::ready);
    }

    QCoreApplication::processEvents();
}

//////////////////////////////////////////////////
// Tray activation
//
// These exercise the same activation decisions that TrayIcon drives.
//

void QuickConnectTests::singleClickOpensMainWindowWhenQuickConnectIsDisabled()
{
    QCOMPARE(
        Gui::GetTrayActivationAction(QSystemTrayIcon::Trigger, false),
        Gui::TrayActivationAction::ShowMainWindow);
}

void QuickConnectTests::singleClickRequestsQuickConnectImmediately()
{
    QCOMPARE(
        Gui::GetTrayActivationAction(QSystemTrayIcon::Trigger, true),
        Gui::TrayActivationAction::QuickConnect);
}

void QuickConnectTests::doubleClickDoesNotOpenMainWindow()
{
    for (const bool enabled : {false, true}) {
        QCOMPARE(
            Gui::GetTrayActivationAction(QSystemTrayIcon::DoubleClick, enabled),
            Gui::TrayActivationAction::None);
    }
}

void QuickConnectTests::contextMenuDoesNotRequestQuickConnect()
{
    QCOMPARE(
        Gui::GetTrayActivationAction(QSystemTrayIcon::Context, true),
        Gui::TrayActivationAction::None);
}

void QuickConnectTests::unknownActivationDoesNothing()
{
    QCOMPARE(
        Gui::GetTrayActivationAction(QSystemTrayIcon::Unknown, true),
        Gui::TrayActivationAction::None);
}

QTEST_GUILESS_MAIN(QuickConnectTests)

#include "QuickConnectTests.moc"
