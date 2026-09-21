#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include <QtTest>
#include <QTemporaryFile>

#include <Config.h>
#include "Source/Core/AirPods.h"
#include "Source/Core/Settings.h"
#include "Source/Core/SettingsRepository.h"
#include "Source/Core/Update.h"
#include "Source/Gui/MainWindowPresentation.h"
#include "Source/Gui/TaskbarGeometry.h"

namespace {

using Core::AirPods::Model;
using Core::AirPods::Side;
using Core::AirPods::Details::Advertisement;
using Core::AirPods::Details::EarDetectionTracker;
using Core::AirPods::Details::StateManager;
using ReceivedData = Core::Bluetooth::AdvertisementWatcher::ReceivedData;
using namespace std::chrono_literals;

class RecordingSettingsObserver final : public Core::Settings::ApplyObserver
{
public:
    void OnLanguageLocaleChanged(const QLocale &) override {}
    void OnAppearanceModeChanged(Core::Settings::AppearanceMode mode) override
    {
        appearanceMode = mode;
        ++appearanceModeChanges;
    }
    void OnAutoRunChanged(bool enable) override
    {
        autoRun = enable;
        ++autoRunChanges;
    }
    void OnLowAudioLatencyChanged(bool) override {}
    void OnAutomaticEarDetectionChanged(bool) override {}
    void OnRssiMinChanged(int16_t) override {}
    void OnDeviceAddressChanged(uint64_t) override {}
    void OnTrayIconBatteryChanged(Core::Settings::TrayIconBatteryBehavior) override {}
    void OnTrayQuickConnectEnabledChanged(bool enable) override
    {
        quickConnectEnabled = enable;
        ++quickConnectEnabledChanges;
    }
    void OnTrayQuickConnectDeviceChanged(const QString &deviceId) override
    {
        quickConnectDeviceId = deviceId;
        ++quickConnectDeviceChanges;
    }
    void OnTaskbarBatteryChanged(Core::Settings::TaskbarStatusBehavior) override {}

    bool autoRun{false};
    int autoRunChanges{0};
    Core::Settings::AppearanceMode appearanceMode{Core::Settings::AppearanceMode::System};
    int appearanceModeChanges{0};
    bool quickConnectEnabled{false};
    int quickConnectEnabledChanges{0};
    QString quickConnectDeviceId;
    int quickConnectDeviceChanges{0};
};

class FailingSyncRepository final : public Core::Settings::Repository
{
public:
    bool Contains(const QString &key) const override
    {
        return _values.Contains(key);
    }

    QVariant Read(const QString &key) const override
    {
        return _values.Read(key);
    }

    QStringList Keys() const override
    {
        return _values.Keys();
    }

    void Write(const QString &key, const QVariant &value) override
    {
        _values.Write(key, value);
    }

    void Remove(const QString &key) override
    {
        _values.Remove(key);
    }

    bool Sync() override
    {
        return false;
    }

private:
    Core::Settings::MemoryRepository _values;
};

std::vector<uint8_t> MakePacket(
    uint16_t modelId, Side side, uint8_t leftBattery, uint8_t rightBattery, uint8_t caseBattery,
    bool bothInCase = true, bool lidOpened = true)
{
    std::vector<uint8_t> packet(27, 0);
    packet[0] = static_cast<uint8_t>(Core::AppleCP::PacketType::ProximityPairing);
    packet[1] = 25;
    packet[3] = static_cast<uint8_t>(modelId & 0xff);
    packet[4] = static_cast<uint8_t>(modelId >> 8);

    constexpr uint8_t kCurrentInEar = 1 << 1;
    constexpr uint8_t kBothInCase = 1 << 2;
    constexpr uint8_t kAnotherInEar = 1 << 3;
    constexpr uint8_t kBroadcastFromLeft = 1 << 5;
    packet[5] = kCurrentInEar | kAnotherInEar;
    if (bothInCase) {
        packet[5] |= kBothInCase;
    }
    if (side == Side::Left) {
        packet[5] |= kBroadcastFromLeft;
        packet[6] = static_cast<uint8_t>((rightBattery << 4) | leftBattery);
    }
    else {
        packet[6] = static_cast<uint8_t>((leftBattery << 4) | rightBattery);
    }

    packet[7] = caseBattery;
    packet[8] = lidOpened ? 0 : 1 << 3;
    return packet;
}

ReceivedData MakeAdvertisementData(
    uint64_t address, int16_t rssi, Side side, uint8_t leftBattery = 8, uint8_t rightBattery = 7,
    uint8_t caseBattery = 5, uint16_t modelId = 0x2014)
{
    ReceivedData data;
    data.address = address;
    data.rssi = rssi;
    data.manufacturerDataMap.emplace(
        Core::AppleCP::VendorId, MakePacket(modelId, side, leftBattery, rightBattery, caseBattery));
    return data;
}

// Replays the ProximityPairing payload captured from AirPods Pro 2 (USB-C, model id 0x2024) in
// the trace behind issue #86. `statusByte` carries the in-ear and broadcast-side bits,
// `batteryByte` the two pod battery nibbles, `chargingByte` the case battery nibble and the
// charging flags.
ReceivedData MakeTraceAdvertisementData(
    uint64_t address, int16_t rssi, uint8_t statusByte, uint8_t batteryByte = 0x88,
    uint8_t chargingByte = 0x8f)
{
    std::vector<uint8_t> packet = {0x07,        0x19,         0x01, 0x24, 0x20, statusByte,
                                   batteryByte, chargingByte, 0x11, 0x00, 0x05};
    packet.resize(27, 0);

    ReceivedData data;
    data.address = address;
    data.rssi = rssi;
    data.manufacturerDataMap.emplace(Core::AppleCP::VendorId, std::move(packet));
    return data;
}

// Address hashes of the two pods in the issue #86 logs. The left pod broadcasts with bit 5 set
// (0x2b/0x23/0x29), the right pod with it clear (0x0b/0x03/0x09).
constexpr uint64_t kTraceLeftPod = 11520552839697973932ULL;
constexpr uint64_t kTraceRightPod = 16108278626836700337ULL;

struct TraceStep {
    uint64_t address;
    int16_t rssi;
    uint8_t status;
    // Idle gap before the advertisement, long enough for shortened `StateManager` timers to fire.
    std::chrono::milliseconds idleBefore{0};
};

// Feeds the steps through the manager and the tracker the way `Manager::OnStateChanged` does and
// returns the "both in ear" transitions in order.
std::vector<bool> ReplayTrace(
    StateManager &manager, EarDetectionTracker &tracker, const std::vector<TraceStep> &steps)
{
    std::vector<bool> transitions;
    for (const auto &step : steps) {
        if (step.idleBefore > 0ms) {
            QTest::qSleep(static_cast<int>(step.idleBefore.count()));
        }
        const auto update = manager.OnAdvReceived(
            Advertisement{MakeTraceAdvertisementData(step.address, step.rssi, step.status, 0xaa)});
        if (!update.has_value()) {
            continue;
        }
        if (const auto changed = tracker.Update(update->newState)) {
            transitions.push_back(*changed);
        }
    }
    return transitions;
}

} // namespace

class AirPodsDomainTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void RejectsMalformedPackets();
    void RecognizesAirPods5();
    void ResolvesAirPods5DisplayName();
    void RecognizesAirPodsMaxUsbC();
    void ResolvesAirPodsMaxUsbCDisplayName();
    void ParsesAdvertisementState();
    void ParsesInEarBitsPerBroadcastSide();
    void FiltersDuplicateAndWeakAdvertisements();
    void AcceptsWeakAdvertisementFromTrackedAddress();
    void RemembersSideAddressAcrossStateReset();
    void AcceptsUnknownRssiFromTrackedAddress();
    void MergesAdvertisementsFromBothSides();
    void RejectsAdvertisementsFromDifferentModels();
    void AcceptsKnownModelAfterUnknownAdvertisement();
    void EarDetectionTrackerSurvivesStateLoss();
    void TraceReplayPausesOnceAndResumesOnce();
    void TraceReplayFollowsAlternatingBroadcaster();
    void PackagesCompatibleLowLatencySilence();
    void LoadsSettingsThroughRepository();
    void MigratesLegacySettingsRepository();
    void PreservesExistingSettingsDuringMigration();
    void RollsBackFailedSettingsMigration();
    void PropagatesQuickConnectSettings();
    void RejectsNullSettingsRepository();
    void ParsesUpdateVersions();
    void ParsesGitHubReleaseMetadata();
    void MatchesInstallerAssetsByArchitecture();
    void RejectsUpdateAssetForAnotherArchitecture();
    void RejectsReleaseMetadataFromAnotherRepository();
    void RejectsUpdateAssetsWithoutDigest();
    void VerifiesUpdateFileDigest();
    void PresentsMainWindowLifecycleStates();
    void PresentsMainWindowDeviceState();
    void PresentsMainWindowCaseBattery();
    void MapsMainWindowAnimationResources();
    void ConvertsTaskbarGeometryAcrossDpiBoundaries();
};

void AirPodsDomainTests::RejectsMalformedPackets()
{
    const std::vector<uint8_t> empty;
    QVERIFY(!Core::AppleCP::AirPods::IsValid(empty));

    auto packet = MakePacket(0x2014, Side::Left, 8, 7, 5);
    QVERIFY(Core::AppleCP::AirPods::IsValid(packet));

    packet[0] = static_cast<uint8_t>(Core::AppleCP::PacketType::AirDrop);
    QVERIFY(!Core::AppleCP::AirPods::IsValid(packet));

    packet = MakePacket(0x2014, Side::Left, 8, 7, 5);
    packet[1] = 24;
    QVERIFY(!Core::AppleCP::AirPods::IsValid(packet));
}

void AirPodsDomainTests::RecognizesAirPods5()
{
    QCOMPARE(Core::AppleCP::AirPods::GetModel(0x2030), Model::AirPods_5);
    QCOMPARE(Core::AppleCP::AirPods::GetModel(0x2036), Model::AirPods_5);
    QCOMPARE(Helper::ToString(Model::AirPods_5), QString{"AirPods 5"});

    for (const auto modelId : {uint16_t{0x2030}, uint16_t{0x2036}}) {
        const Advertisement advertisement{
            MakeAdvertisementData(0x1234, -45, Side::Left, 8, 7, 5, modelId)};
        QCOMPARE(advertisement.GetAdvState().model, Model::AirPods_5);
        QCOMPARE(advertisement.GetAdvState().pods.left.battery.Value(), 80u);
        QCOMPARE(advertisement.GetAdvState().pods.right.battery.Value(), 70u);
        QCOMPARE(advertisement.GetAdvState().caseBox.battery.Value(), 50u);
    }

    // Sanitized captures from an AirPods 5 with Wireless Charging Case. Bytes 11-26 are
    // intentionally zeroed because the protocol's trailing payload may identify the device.
    constexpr std::array<uint8_t, 27> capturedLeft{
        0x07, 0x19, 0x01, 0x30, 0x20, 0x35, 0xaa, 0xba, 0x32, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    constexpr std::array<uint8_t, 27> capturedRight{
        0x07, 0x19, 0x01, 0x30, 0x20, 0x55, 0xaa, 0xba, 0x32, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    StateManager manager;
    manager.OnRssiMinChanged(-80);
    for (const auto &[side, address, packet] : {
             std::tuple{Side::Left, uint64_t{0x1111}, capturedLeft},
             std::tuple{Side::Right, uint64_t{0x2222}, capturedRight},
         })
    {
        ReceivedData data;
        data.address = address;
        data.rssi = -60;
        data.manufacturerDataMap.emplace(
            Core::AppleCP::VendorId, std::vector<uint8_t>{packet.cbegin(), packet.cend()});
        const Advertisement captured{data};
        const auto &state = captured.GetAdvState();
        QCOMPARE(state.model, Model::AirPods_5);
        QCOMPARE(state.side, side);
        QCOMPARE(state.pods.left.battery.Value(), 100u);
        QCOMPARE(state.pods.right.battery.Value(), 100u);
        QCOMPARE(state.caseBox.battery.Value(), 100u);
        QVERIFY(state.pods.left.isCharging);
        QVERIFY(state.pods.right.isCharging);
        QVERIFY(!state.caseBox.isCharging);
        QVERIFY(!state.pods.left.isInEar);
        QVERIFY(!state.pods.right.isInEar);
        QVERIFY(state.caseBox.isBothPodsInCase);
        QVERIFY(state.caseBox.isLidOpened);
        const auto update = manager.OnAdvReceived(captured);
        if (side == Side::Left) {
            QVERIFY(update.has_value());
        }
    }
    QCOMPARE(manager.GetCurrentState()->model, Model::AirPods_5);
}

void AirPodsDomainTests::ResolvesAirPods5DisplayName()
{
    QCOMPARE(
        Core::AirPods::Details::ResolveDisplayName("AirPods", Model::AirPods_5),
        QString{"AirPods 5"});
    QCOMPARE(
        Core::AirPods::Details::ResolveDisplayName(
            "Kashionz's AirPods - Find My", Model::AirPods_5),
        QString{"Kashionz's AirPods"});
    QCOMPARE(
        Core::AirPods::Details::ResolveDisplayName({}, Model::AirPods_5), QString{"AirPods 5"});
}

void AirPodsDomainTests::RecognizesAirPodsMaxUsbC()
{
    QCOMPARE(Core::AppleCP::AirPods::GetModel(0x201F), Model::AirPods_Max_USB_C);
    QCOMPARE(Helper::ToString(Model::AirPods_Max_USB_C), QString{"AirPods Max (USB-C)"});
}

void AirPodsDomainTests::ResolvesAirPodsMaxUsbCDisplayName()
{
    QCOMPARE(
        Core::AirPods::Details::ResolveDisplayName("AirPods Max", Model::AirPods_Max_USB_C),
        QString{"AirPods Max (USB-C)"});
    QCOMPARE(
        Core::AirPods::Details::ResolveDisplayName(
            "Studio AirPods Max - Find My", Model::AirPods_Max_USB_C),
        QString{"Studio AirPods Max"});
    QCOMPARE(
        Core::AirPods::Details::ResolveDisplayName("AirPods Max", Model::AirPods_Max),
        QString{"AirPods Max"});
}

void AirPodsDomainTests::PackagesCompatibleLowLatencySilence()
{
    QFile qrc{QString{APD_SOURCE_DIR} + "/Source/Resource/Resource.qrc"};
    QVERIFY(qrc.open(QIODevice::ReadOnly | QIODevice::Text));

    const auto qrcContents = QString::fromUtf8(qrc.readAll());
    QVERIFY(qrcContents.contains("<file>Audio/Silence.mp3</file>"));
    QVERIFY(QFile::exists(QString{APD_SOURCE_DIR} + "/Source/Resource/Audio/Silence.mp3"));
}

void AirPodsDomainTests::ParsesAdvertisementState()
{
    const Advertisement advertisement{MakeAdvertisementData(0x1234, -45, Side::Left)};
    const auto &state = advertisement.GetAdvState();

    QCOMPARE(state.model, Model::AirPods_Pro_2);
    QCOMPARE(state.side, Side::Left);
    QCOMPARE(state.pods.left.battery.Value(), 80U);
    QCOMPARE(state.pods.right.battery.Value(), 70U);
    QCOMPARE(state.caseBox.battery.Value(), 50U);
    QVERIFY(state.pods.left.isInEar);
    QVERIFY(state.pods.right.isInEar);
    QVERIFY(state.caseBox.isBothPodsInCase);
    QVERIFY(state.caseBox.isLidOpened);
}

void AirPodsDomainTests::ParsesInEarBitsPerBroadcastSide()
{
    // The packet only knows "current" (broadcasting) and "another" pod, so the same bit maps to
    // a different side depending on the broadcast-from flag (bit 5).
    const auto parse = [](uint8_t statusByte, uint8_t chargingByte = 0x8f) {
        return Advertisement{
            MakeTraceAdvertisementData(0x1234, -45, statusByte, 0x88, chargingByte)}
            .GetAdvState();
    };

    auto state = parse(0x2b);
    QCOMPARE(state.model, Model::AirPods_Pro_2_USB_C);
    QCOMPARE(state.side, Side::Left);
    QVERIFY(state.pods.left.isInEar);
    QVERIFY(state.pods.right.isInEar);

    state = parse(0x23);
    QVERIFY(state.pods.left.isInEar);
    QVERIFY(!state.pods.right.isInEar);

    state = parse(0x29);
    QVERIFY(!state.pods.left.isInEar);
    QVERIFY(state.pods.right.isInEar);

    state = parse(0x0b);
    QCOMPARE(state.side, Side::Right);
    QVERIFY(state.pods.left.isInEar);
    QVERIFY(state.pods.right.isInEar);

    state = parse(0x03);
    QVERIFY(!state.pods.left.isInEar);
    QVERIFY(state.pods.right.isInEar);

    state = parse(0x09);
    QVERIFY(state.pods.left.isInEar);
    QVERIFY(!state.pods.right.isInEar);

    // A charging pod reports a stale in-ear bit, so charging masks it.
    state = parse(0x2b, 0x9f);
    QVERIFY(state.pods.left.isCharging);
    QVERIFY(!state.pods.left.isInEar);
    QVERIFY(state.pods.right.isInEar);
}

void AirPodsDomainTests::FiltersDuplicateAndWeakAdvertisements()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    auto first =
        manager.OnAdvReceived(Advertisement{MakeAdvertisementData(0x1234, -45, Side::Left)});
    QVERIFY(first.has_value());
    QVERIFY(!first->oldState.has_value());

    auto duplicate =
        manager.OnAdvReceived(Advertisement{MakeAdvertisementData(0x1234, -45, Side::Left)});
    QVERIFY(!duplicate.has_value());

    auto weak = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x5678, -90, Side::Left, 7, 7, 5)});
    QVERIFY(!weak.has_value());
    QCOMPARE(manager.GetCurrentState()->pods.left.battery.Value(), 80U);
}

void AirPodsDomainTests::AcceptsWeakAdvertisementFromTrackedAddress()
{
    // Issue #86: the RSSI floor rejected the in-ear change broadcast by the pods we were already
    // tracking, and starved the lost timer until the state was wiped.
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    const auto first =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xAAAA, -76, 0x2b)});
    QVERIFY(first.has_value());
    QVERIFY(first->newState.pods.left.isInEar);
    QVERIFY(first->newState.pods.right.isInEar);

    const auto weak =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xAAAA, -88, 0x23)});
    QVERIFY(weak.has_value());
    QVERIFY(weak->oldState.has_value());
    QVERIFY(weak->oldState->pods.left.isInEar);
    QVERIFY(weak->oldState->pods.right.isInEar);
    QVERIFY(weak->newState.pods.left.isInEar);
    QVERIFY(!weak->newState.pods.right.isInEar);

    // An unknown address still has to pass the RSSI floor...
    const auto stranger =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xBBBB, -88, 0x2b)});
    QVERIFY(!stranger.has_value());
    QVERIFY(!manager.GetCurrentState()->pods.right.isInEar);

    // ...and then the address-change heuristics: a battery jump is rejected, a plausible
    // advertisement is accepted.
    const auto strangerBattery =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xBBBB, -70, 0x2b, 0x68)});
    QVERIFY(!strangerBattery.has_value());

    const auto strangerAccepted =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xBBBB, -70, 0x2b)});
    QVERIFY(strangerAccepted.has_value());
    QVERIFY(strangerAccepted->newState.pods.right.isInEar);
}

void AirPodsDomainTests::RemembersSideAddressAcrossStateReset()
{
    // A side that stops broadcasting for a while is dropped from the advertisement cache by
    // `DoStateReset`. Its address must still be recognised when it comes back weakly.
    StateManager manager{{.lost = 10s, .stateReset = 50ms}};
    manager.OnRssiMinChanged(-80);

    const auto first =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xAAAA, -76, 0x2b)});
    QVERIFY(first.has_value());

    QTest::qSleep(250);

    const auto weak =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xAAAA, -88, 0x21)});
    QVERIFY(weak.has_value());
    QVERIFY(weak->oldState.has_value());
    QVERIFY(!weak->newState.pods.left.isInEar);
    QVERIFY(!weak->newState.pods.right.isInEar);

    // Only the classic Bluetooth disconnect forgets the addresses.
    manager.Disconnect();
    const auto afterDisconnect =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xAAAA, -88, 0x2b)});
    QVERIFY(!afterDisconnect.has_value());
}

void AirPodsDomainTests::AcceptsUnknownRssiFromTrackedAddress()
{
    // Windows reports -127 when it has no RSSI for a packet, often on the very advertisement
    // that carries an in-ear change.
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    const auto first =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xAAAA, -76, 0x2b)});
    QVERIFY(first.has_value());

    const auto unknownRssi =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xAAAA, -127, 0x29)});
    QVERIFY(unknownRssi.has_value());
    QVERIFY(!unknownRssi->newState.pods.left.isInEar);
    QVERIFY(unknownRssi->newState.pods.right.isInEar);

    const auto stranger =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xBBBB, -127, 0x2b)});
    QVERIFY(!stranger.has_value());

    // The other pod shows up afresh; -127 must not feed the rssiDiff heuristic against it.
    const auto otherSide =
        manager.OnAdvReceived(Advertisement{MakeTraceAdvertisementData(0xCCCC, -70, 0x0b)});
    QVERIFY(otherSide.has_value());
    QVERIFY(otherSide->newState.pods.left.isInEar);
}

void AirPodsDomainTests::MergesAdvertisementsFromBothSides()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    QVERIFY(
        manager
            .OnAdvReceived(Advertisement{MakeAdvertisementData(0x1111, -45, Side::Left, 8, 6, 4)})
            .has_value());

    auto update = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x2222, -46, Side::Right, 9, 7, 5)});
    QVERIFY(update.has_value());
    QVERIFY(update->oldState.has_value());
    QCOMPARE(update->newState.pods.left.battery.Value(), 90U);
    QCOMPARE(update->newState.pods.right.battery.Value(), 70U);
    QCOMPARE(update->newState.caseBox.battery.Value(), 50U);
}

void AirPodsDomainTests::RejectsAdvertisementsFromDifferentModels()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    const auto first =
        manager.OnAdvReceived(Advertisement{MakeAdvertisementData(0x1111, -45, Side::Left)});
    QVERIFY(first.has_value());
    QCOMPARE(first->newState.model, Model::AirPods_Pro_2);

    const auto differentModel = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x2222, -46, Side::Right, 8, 7, 5, 0x2013)});
    QVERIFY(!differentModel.has_value());
    QCOMPARE(manager.GetCurrentState()->model, Model::AirPods_Pro_2);
}

void AirPodsDomainTests::AcceptsKnownModelAfterUnknownAdvertisement()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    const auto unknown = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x1111, -45, Side::Left, 8, 7, 5, 0xffff)});
    QVERIFY(unknown.has_value());
    QCOMPARE(unknown->newState.model, Model::Unknown);

    const auto known =
        manager.OnAdvReceived(Advertisement{MakeAdvertisementData(0x2222, -46, Side::Right)});
    QVERIFY(known.has_value());
    QCOMPARE(known->newState.model, Model::AirPods_Pro_2);
}

void AirPodsDomainTests::EarDetectionTrackerSurvivesStateLoss()
{
    const auto makeState = [](bool leftInEar, bool rightInEar) {
        Core::AirPods::State state;
        state.pods.left.isInEar = leftInEar;
        state.pods.right.isInEar = rightInEar;
        return state;
    };

    Core::AirPods::Details::EarDetectionTracker tracker;

    // The first observation has nothing to compare against.
    QCOMPARE(tracker.Update(makeState(true, true)), std::optional<bool>{});
    QCOMPARE(tracker.Update(makeState(true, false)), std::optional<bool>{false});
    QCOMPARE(tracker.Update(makeState(true, false)), std::optional<bool>{});
    QCOMPARE(tracker.Update(makeState(false, false)), std::optional<bool>{});
    QCOMPARE(tracker.Update(makeState(true, true)), std::optional<bool>{true});

    tracker.Reset();
    QCOMPARE(tracker.Update(makeState(true, true)), std::optional<bool>{});
}

void AirPodsDomainTests::TraceReplayPausesOnceAndResumesOnce()
{
    // Replays the advertisement sequence from the issue #86 trace (07:58:55 - 07:59:36): the
    // right pod is taken out (0x23), then swapped for the left one (0x29), then both go back in
    // (0x2b). The pods stayed below the -80 dBm floor most of the time, so the original code
    // starved the lost timer twice and never saw the in-ear transitions. The lost timer is
    // shortened so that the two "Device is lost" gaps in the trace can be reproduced.
    StateManager manager{{.lost = 100ms, .stateReset = 10s}};
    manager.OnRssiMinChanged(-80);
    EarDetectionTracker tracker;

    const std::vector<TraceStep> steps = {
        // Both in ear; only the -76 advertisement clears the floor.
        {kTraceLeftPod, -82, 0x2b},
        {kTraceLeftPod, -84, 0x2b},
        {kTraceLeftPod, -76, 0x2b},
        {kTraceLeftPod, -88, 0x2b},
        {kTraceLeftPod, -82, 0x2b},
        {kTraceLeftPod, -82, 0x2b},
        {kTraceLeftPod, -88, 0x2b},
        {kTraceLeftPod, -82, 0x2b},
        // Right pod out.
        {kTraceLeftPod, -82, 0x23},
        {kTraceLeftPod, -88, 0x23},
        {kTraceLeftPod, -88, 0x23},
        // "Device is lost" at 07:59:09.
        {kTraceLeftPod, -80, 0x23, 300ms},
        {kTraceLeftPod, -84, 0x23},
        {kTraceLeftPod, -84, 0x23},
        {kTraceLeftPod, -74, 0x23},
        {kTraceLeftPod, -96, 0x23},
        {kTraceLeftPod, -90, 0x23},
        {kTraceLeftPod, -84, 0x23},
        {kTraceLeftPod, -84, 0x23},
        {kTraceLeftPod, -88, 0x23},
        // Right pod back in, left pod out.
        {kTraceLeftPod, -90, 0x29},
        {kTraceLeftPod, -82, 0x29},
        // "Device is lost" at 07:59:22.
        {kTraceLeftPod, -86, 0x29, 300ms},
        {kTraceLeftPod, -88, 0x29},
        {kTraceLeftPod, -92, 0x29},
        {kTraceLeftPod, -86, 0x29},
        {kTraceLeftPod, -88, 0x29},
        // Both back in.
        {kTraceLeftPod, -78, 0x2b},
        {kTraceLeftPod, -78, 0x2b},
        {kTraceLeftPod, -76, 0x2b},
        {kTraceLeftPod, -80, 0x2b},
        {kTraceLeftPod, -74, 0x2b},
    };

    QCOMPARE(ReplayTrace(manager, tracker, steps), (std::vector<bool>{false, true}));
}

void AirPodsDomainTests::TraceReplayFollowsAlternatingBroadcaster()
{
    // Replays the hardware test from 08:53:25 to 08:54:20: the pods alternate which one
    // broadcasts. While the right pod broadcasts, the left side is dropped by `DoStateReset`
    // (08:53:40); when the left pod comes back at -84..-88 with the right pod still out (0x23)
    // it has to be accepted without a spurious transition. The timers are shortened so that
    // the state reset and the lost timeout can be exercised: had the weak left advertisements
    // been rejected, the lost timer would have starved during the idle gaps.
    StateManager manager{{.lost = 500ms, .stateReset = 50ms}};
    manager.OnRssiMinChanged(-80);
    EarDetectionTracker tracker;

    const std::vector<TraceStep> untilRightPodOut = {
        {kTraceLeftPod, -78, 0x2b},
        {kTraceLeftPod, -82, 0x2b},
        // Left pod out.
        {kTraceLeftPod, -78, 0x29},
        {kTraceLeftPod, -96, 0x29},
        // The right pod takes over broadcasting and confirms it.
        {kTraceRightPod, -68, 0x03},
        {kTraceRightPod, -80, 0x03},
        // Left pod back in.
        {kTraceRightPod, -70, 0x0b},
        {kTraceRightPod, -70, 0x0b},
        {kTraceRightPod, -70, 0x0b},
        {kTraceRightPod, -74, 0x0b},
        {kTraceRightPod, -76, 0x0b},
        // "DoStateReset called. Side: Left" at 08:53:40.
        {kTraceRightPod, -82, 0x0b, 150ms},
        {kTraceRightPod, -72, 0x0b},
        {kTraceRightPod, -86, 0x0b},
        {kTraceRightPod, -78, 0x0b},
        // Right pod out.
        {kTraceRightPod, -80, 0x09},
        {kTraceRightPod, -80, 0x09},
        // The left pod takes over again, weakly; each advertisement must feed the lost timer.
        {kTraceLeftPod, -84, 0x23, 250ms},
        {kTraceLeftPod, -86, 0x23, 250ms},
        {kTraceLeftPod, -88, 0x23, 250ms},
    };
    QCOMPARE(
        ReplayTrace(manager, tracker, untilRightPodOut), (std::vector<bool>{false, true, false}));
    QVERIFY(manager.GetCurrentState().has_value());
    QVERIFY(!manager.GetCurrentState()->pods.right.isInEar);

    const std::vector<TraceStep> untilBothIn = {
        {kTraceLeftPod, -78, 0x23},
        {kTraceLeftPod, -100, 0x23},
        {kTraceLeftPod, -84, 0x23},
        // "DoStateReset called. Side: Right" at 08:53:59.
        {kTraceLeftPod, -90, 0x23, 150ms},
        {kTraceLeftPod, -92, 0x23},
        {kTraceLeftPod, -92, 0x23},
        {kTraceLeftPod, -84, 0x23},
        {kTraceLeftPod, -82, 0x23},
        {kTraceLeftPod, -78, 0x23},
        {kTraceLeftPod, -80, 0x23},
        {kTraceLeftPod, -78, 0x23},
        {kTraceLeftPod, -76, 0x23},
        {kTraceLeftPod, -88, 0x23},
        {kTraceLeftPod, -80, 0x23},
        // Both back in at 08:54:20.
        {kTraceLeftPod, -80, 0x2b},
    };
    QCOMPARE(ReplayTrace(manager, tracker, untilBothIn), (std::vector<bool>{true}));
}

void AirPodsDomainTests::LoadsSettingsThroughRepository()
{
    auto repository = std::make_unique<Core::Settings::MemoryRepository>();
    repository->Write("abi_version", Core::Settings::kFieldsAbiVersion);
    repository->Write("auto_run", true);
    repository->Write("appearance_mode", QString{"Dark"});
    Core::Settings::SetRepository(std::move(repository));

    QCOMPARE(Core::Settings::Load(), Core::Settings::LoadResult::Successful);
    QVERIFY(Core::Settings::GetCurrent().auto_run);
    QCOMPARE(Core::Settings::GetCurrent().appearance_mode, Core::Settings::AppearanceMode::Dark);

    RecordingSettingsObserver observer;
    Core::Settings::SetApplyObserver(&observer);
    Core::Settings::Apply();
    QCOMPARE(observer.autoRunChanges, 1);
    QVERIFY(observer.autoRun);
    QCOMPARE(observer.appearanceModeChanges, 1);
    QCOMPARE(observer.appearanceMode, Core::Settings::AppearanceMode::Dark);

    Core::Settings::SetApplyObserver(nullptr);
    Core::Settings::SetRepository(Core::Settings::CreatePersistentRepository());
}

void AirPodsDomainTests::MigratesLegacySettingsRepository()
{
    Core::Settings::MemoryRepository current;
    Core::Settings::MemoryRepository legacy;
    legacy.Write("auto_run", true);
    legacy.Write("device_address", QVariant::fromValue<qulonglong>(0x12345678));
    legacy.Write("abi_version", Core::Settings::kFieldsAbiVersion);

    QVERIFY(Core::Settings::Details::MigrateLegacySettings(current, legacy));
    QCOMPARE(current.Read("auto_run").toBool(), true);
    QCOMPARE(current.Read("device_address").toULongLong(), qulonglong{0x12345678});
    QCOMPARE(current.Read("abi_version").toUInt(), Core::Settings::kFieldsAbiVersion);
}

void AirPodsDomainTests::PreservesExistingSettingsDuringMigration()
{
    Core::Settings::MemoryRepository current;
    Core::Settings::MemoryRepository legacy;
    current.Write("auto_run", false);
    legacy.Write("abi_version", Core::Settings::kFieldsAbiVersion);
    legacy.Write("auto_run", true);
    legacy.Write("language_locale", QStringLiteral("zh_TW"));

    QVERIFY(Core::Settings::Details::MigrateLegacySettings(current, legacy));
    QCOMPARE(current.Read("auto_run").toBool(), false);
    QCOMPARE(current.Read("language_locale").toString(), QStringLiteral("zh_TW"));

    current.Write("auto_run", true);
    QVERIFY(!Core::Settings::Details::MigrateLegacySettings(current, legacy));
    QCOMPARE(current.Read("auto_run").toBool(), true);
}

void AirPodsDomainTests::RollsBackFailedSettingsMigration()
{
    FailingSyncRepository current;
    Core::Settings::MemoryRepository legacy;
    legacy.Write("abi_version", Core::Settings::kFieldsAbiVersion);
    legacy.Write("auto_run", true);

    QVERIFY(!Core::Settings::Details::MigrateLegacySettings(current, legacy));
    QVERIFY(current.Keys().isEmpty());
}

void AirPodsDomainTests::PropagatesQuickConnectSettings()
{
    auto repository = std::make_unique<Core::Settings::MemoryRepository>();
    repository->Write("abi_version", Core::Settings::kFieldsAbiVersion);
    repository->Write("tray_quick_connect_enabled", true);
    repository->Write("tray_quick_connect_device_id", "{A}");
    Core::Settings::SetRepository(std::move(repository));

    QCOMPARE(Core::Settings::Load(), Core::Settings::LoadResult::Successful);

    RecordingSettingsObserver observer;
    Core::Settings::SetApplyObserver(&observer);
    Core::Settings::Apply();
    QVERIFY(observer.quickConnectEnabled);
    QCOMPARE(observer.quickConnectEnabledChanges, 1);
    QCOMPARE(observer.quickConnectDeviceId, QString{"{A}"});
    QCOMPARE(observer.quickConnectDeviceChanges, 1);

    Core::Settings::SetApplyObserver(nullptr);
    Core::Settings::SetRepository(Core::Settings::CreatePersistentRepository());
}

void AirPodsDomainTests::RejectsNullSettingsRepository()
{
    auto repository = std::make_unique<Core::Settings::MemoryRepository>();
    repository->Write("abi_version", Core::Settings::kFieldsAbiVersion);
    repository->Write("auto_run", true);
    Core::Settings::SetRepository(std::move(repository));

    Core::Settings::SetRepository(nullptr);

    QCOMPARE(Core::Settings::Load(), Core::Settings::LoadResult::Successful);
    QVERIFY(Core::Settings::GetCurrent().auto_run);
    Core::Settings::SetRepository(Core::Settings::CreatePersistentRepository());
}

void AirPodsDomainTests::ParsesUpdateVersions()
{
    QCOMPARE(Core::Update::ToVersionNumber("v0.5.0"), QVersionNumber(0, 5, 0));
    QCOMPARE(Core::Update::ToVersionNumber("0.5.0"), QVersionNumber(0, 5, 0));
}

void AirPodsDomainTests::ParsesGitHubReleaseMetadata()
{
    const auto metadata = QString{R"json({
        "tag_name": "v0.5.0",
        "body": "## Change log\n- Add automatic updates\n\nInstallation notes",
        "html_url": "%1/tag/v0.5.0",
        "prerelease": false,
        "assets": [{
            "name": "AirPodsDesktop-0.5.0-win64.exe",
            "size": 123456,
            "digest": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "browser_download_url": "%1/download/v0.5.0/AirPodsDesktop-0.5.0-win64.exe"
        }]
    })json"}
                              .arg(Config::UrlReleases);
    const auto release = Core::Update::Details::ParseSingleReleaseResponse(metadata.toStdString());

    QVERIFY(release.has_value());
    QCOMPARE(release->version, QVersionNumber(0, 5, 0));
    QCOMPARE(release->fileName, QString{"AirPodsDesktop-0.5.0-win64.exe"});
    QCOMPARE(release->fileSize, size_t{123456});
    QCOMPARE(
        release->sha256,
        QString{"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"});
    QCOMPARE(release->changeLog, QString{"- Add automatic updates"});
    QVERIFY(release->CanAutoUpdate());
    QVERIFY(!release->isPreRelease);

    const auto generatedNotes = QString{R"json({
        "tag_name": "v0.5.0",
        "body": "## What's Changed\n* Improve update handling\n\n**Full Changelog**: https://example.invalid/compare",
        "html_url": "%1/tag/v0.5.0",
        "prerelease": false,
        "assets": []
    })json"}
                                    .arg(Config::UrlReleases);
    const auto generatedRelease =
        Core::Update::Details::ParseSingleReleaseResponse(generatedNotes.toStdString());

    QVERIFY(generatedRelease.has_value());
    QCOMPARE(generatedRelease->changeLog, QString{"* Improve update handling"});

    // Shaped after the real 0.4.2 release body: CRLF line endings and an emoji shortcode
    // between the hashes and the words.
    const auto decorated = QString{R"json({
        "tag_name": "0.4.2",
        "body": "Beta notice\r\n\r\n## :scroll: Change log\r\n1. Supported AirPods 4\r\n\r\nSorry",
        "html_url": "%1/tag/0.4.2",
        "prerelease": false,
        "assets": []
    })json"}
                               .arg(Config::UrlReleases);
    const auto decoratedRelease =
        Core::Update::Details::ParseSingleReleaseResponse(decorated.toStdString());

    QVERIFY(decoratedRelease.has_value());
    QCOMPARE(decoratedRelease->changeLog, QString{"1. Supported AirPods 4"});
}

void AirPodsDomainTests::RejectsUpdateAssetForAnotherArchitecture()
{
    const auto metadata = QString{R"json({
        "tag_name": "v0.5.0",
        "body": "Change log\nLegacy bridge",
        "html_url": "%1/tag/v0.5.0",
        "prerelease": false,
        "assets": [{
            "name": "AirPodsDesktop-0.5.0-win32-bridge.exe",
            "size": 123456,
            "digest": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "browser_download_url": "%1/download/v0.5.0/AirPodsDesktop-0.5.0-win32-bridge.exe"
        }]
    })json"}
                              .arg(Config::UrlReleases);

    const auto release = Core::Update::Details::ParseSingleReleaseResponse(metadata.toStdString());
    QVERIFY(release.has_value());
    QVERIFY(release->fileName.isEmpty());
    QVERIFY(!release->CanAutoUpdate());
}

void AirPodsDomainTests::MatchesInstallerAssetsByArchitecture()
{
    using Core::Update::Details::IsCompatibleInstallerAsset;

    const auto installer = QStringLiteral("AirPodsDesktop-0.5.0-win64.exe");
    const auto portable = QStringLiteral("AirPodsDesktop-0.5.0-win64-portable.zip");
    const auto bridge = QStringLiteral("AirPodsDesktop-0.5.0-win32-bridge.exe");

    QVERIFY(IsCompatibleInstallerAsset(installer, QStringLiteral("win64")));
    QVERIFY(!IsCompatibleInstallerAsset(bridge, QStringLiteral("win64")));
    QVERIFY(!IsCompatibleInstallerAsset(portable, QStringLiteral("win64")));
    QVERIFY(IsCompatibleInstallerAsset(bridge, QStringLiteral("win32")));
    QVERIFY(!IsCompatibleInstallerAsset(installer, QStringLiteral("win32")));
}

void AirPodsDomainTests::RejectsReleaseMetadataFromAnotherRepository()
{
    const auto release = Core::Update::Details::ParseSingleReleaseResponse(R"json({
        "tag_name": "v9.9.9",
        "body": "Change log\nUntrusted release",
        "html_url": "https://github.com/AnotherOwner/AirPodsDesktop/releases/tag/v9.9.9",
        "prerelease": false,
        "assets": []
    })json");

    QVERIFY(!release.has_value());
}

void AirPodsDomainTests::RejectsUpdateAssetsWithoutDigest()
{
    const auto metadata = QString{R"json({
        "tag_name": "v0.5.0",
        "body": "Change log\nUnsigned asset",
        "html_url": "%1/tag/v0.5.0",
        "prerelease": false,
        "assets": [{
            "name": "AirPodsDesktop-0.5.0-win64.exe",
            "size": 123456,
            "browser_download_url": "%1/download/v0.5.0/AirPodsDesktop-0.5.0-win64.exe"
        }]
    })json"}
                              .arg(Config::UrlReleases);
    const auto release = Core::Update::Details::ParseSingleReleaseResponse(metadata.toStdString());

    QVERIFY(release.has_value());
    QVERIFY(!release->CanAutoUpdate());
}

void AirPodsDomainTests::VerifiesUpdateFileDigest()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    QCOMPARE(file.write("test"), qint64{4});
    file.close();

    const QString expected = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
    QVERIFY(Core::Update::Details::VerifyFileSha256(file.fileName(), expected));
    QVERIFY(!Core::Update::Details::VerifyFileSha256(
        file.fileName(), "0000000000000000000000000000000000000000000000000000000000000000"));
}

void AirPodsDomainTests::PresentsMainWindowLifecycleStates()
{
    Gui::MainWindowViewModel viewModel;

    auto presentation = viewModel.Present();
    QCOMPARE(presentation.title, QString{"Unavailable"});
    QCOMPARE(presentation.buttonAction, Gui::ButtonAction::NoButton);
    QVERIFY(!presentation.animationModel.has_value());

    viewModel.Available();
    QCOMPARE(viewModel.Present().title, QString{"Disconnected"});

    viewModel.Unbind();
    presentation = viewModel.Present();
    QCOMPARE(presentation.title, QString{"Waiting for Binding"});
    QCOMPARE(presentation.buttonAction, Gui::ButtonAction::Bind);

    viewModel.Disconnect();
    QCOMPARE(viewModel.Present(), presentation);

    viewModel.Bind();
    QCOMPARE(viewModel.Present().title, QString{"Disconnected"});
}

void AirPodsDomainTests::PresentsMainWindowDeviceState()
{
    Core::AirPods::State state;
    state.displayName = "Office AirPods";
    state.model = Core::AirPods::Model::AirPods_Pro_2;
    state.pods.left.battery = 80;
    state.pods.left.isCharging = true;
    state.pods.right.battery = 60;

    Gui::MainWindowViewModel viewModel;
    viewModel.UpdateState(state);
    const auto presentation = viewModel.Present();

    QCOMPARE(presentation.title, state.displayName);
    QCOMPARE(presentation.animationModel, std::optional{state.model});
    QVERIFY(presentation.leftBattery.visible);
    QVERIFY(presentation.leftBattery.charging);
    QCOMPARE(presentation.leftBattery.value, 80U);
    QVERIFY(presentation.rightBattery.visible);
    QCOMPARE(presentation.rightBattery.value, 60U);
    QVERIFY(!presentation.caseBattery.visible);
}

void AirPodsDomainTests::PresentsMainWindowCaseBattery()
{
    // Mirrors `PresentsMainWindowDeviceState`, which reports the pods but not the case.
    // Reporting only the case pins each slot to its own source.
    Core::AirPods::State state;
    state.displayName = "Office AirPods";
    state.model = Core::AirPods::Model::AirPods_4_ANC;
    state.caseBox.battery = 40;
    state.caseBox.isCharging = true;

    Gui::MainWindowViewModel viewModel;
    viewModel.UpdateState(state);
    auto presentation = viewModel.Present();

    QVERIFY(presentation.caseBattery.visible);
    QVERIFY(presentation.caseBattery.charging);
    QCOMPARE(presentation.caseBattery.value, 40U);
    QVERIFY(!presentation.leftBattery.visible);
    QVERIFY(!presentation.rightBattery.visible);

    state.caseBox.isCharging = false;
    viewModel.UpdateState(state);
    presentation = viewModel.Present();

    QVERIFY(presentation.caseBattery.visible);
    QVERIFY(!presentation.caseBattery.charging);
}

void AirPodsDomainTests::MapsMainWindowAnimationResources()
{
    const auto airPods5 = Gui::GetAnimationPresentation(Core::AirPods::Model::AirPods_5);
    QCOMPARE(airPods5.resource, QString{"qrc:/Resource/Video/AirPods_4_ANC.avi"});
    QCOMPARE(airPods5.sourceSize, QSize(900, 450));

    const auto pro = Gui::GetAnimationPresentation(Core::AirPods::Model::AirPods_Pro_2_USB_C);
    QCOMPARE(pro.resource, QString{"qrc:/Resource/Video/AirPods_Pro_2.avi"});
    QCOMPARE(pro.sourceSize, QSize(900, 450));

    const auto maxUsbC = Gui::GetAnimationPresentation(Core::AirPods::Model::AirPods_Max_USB_C);
    QCOMPARE(maxUsbC.resource, QString{"qrc:/Resource/Video/AirPods_Max.avi"});
    QCOMPARE(maxUsbC.sourceSize, QSize(600, 650));

    const auto fallback = Gui::GetAnimationPresentation(Core::AirPods::Model::Unknown);
    QCOMPARE(fallback.resource, QString{"qrc:/Resource/Video/AirPods_1.avi"});
    QCOMPARE(fallback.sourceSize, QSize(800, 400));

    // The qrc is linked into the application, not into this binary, so read it from the source
    // tree: a mapping naming a resource the qrc does not carry would ship a blank animation.
    QFile qrc{QString{APD_SOURCE_DIR} + "/Source/Resource/Resource.qrc"};
    QVERIFY(qrc.open(QIODevice::ReadOnly | QIODevice::Text));
    const auto qrcContents = QString::fromUtf8(qrc.readAll());

    for (uint32_t i = 0; i < static_cast<uint32_t>(Core::AirPods::Model::_Max); ++i) {
        const auto resource =
            Gui::GetAnimationPresentation(static_cast<Core::AirPods::Model>(i)).resource;
        const auto entry = QString{resource}.replace("qrc:/Resource/", "");
        QVERIFY2(
            qrcContents.contains("<file>" + entry + "</file>"),
            qPrintable(QString{"animation not listed in Resource.qrc: %1"}.arg(resource)));
        QVERIFY2(
            QFile::exists(QString{APD_SOURCE_DIR} + "/Source/Resource/" + entry),
            qPrintable(QString{"animation file missing: %1"}.arg(entry)));
    }
}

void AirPodsDomainTests::ConvertsTaskbarGeometryAcrossDpiBoundaries()
{
    // Reproduces issue #217: one 2560x1440 monitor at 125% display scaling. Win32 reports
    // physical pixels, while QWidget geometry is expressed in device-independent pixels.
    const auto layout =
        Gui::TaskbarGeometry::CalculateLayout(QSize{2560, 50}, QRect{160, 0, 2325, 50}, 120, true);

    QCOMPARE(layout.statusLogical, QRect(1988, 0, 60, 40));
    QCOMPARE(layout.taskButtonsNative, QRect(160, 0, 2325, 50));

    QCOMPARE(Gui::TaskbarGeometry::LogicalToNative(layout.statusLogical.right() + 1, 120), 2560);

    const auto restored =
        Gui::TaskbarGeometry::RestoreTaskButtons(QSize{2560, 50}, layout.taskButtonsNative, true);
    QCOMPARE(restored, QRect(160, 0, 2400, 50));

    const auto vertical =
        Gui::TaskbarGeometry::CalculateLayout(QSize{50, 1440}, QRect{0, 120, 50, 1270}, 120, false);
    QCOMPARE(vertical.statusLogical, QRect(0, 1112, 40, 40));
    QCOMPARE(vertical.taskButtonsNative, QRect(0, 120, 50, 1270));
}

QTEST_GUILESS_MAIN(AirPodsDomainTests)

#include "AirPodsDomainTests.moc"
