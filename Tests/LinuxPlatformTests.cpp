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

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServer>
#include <QDBusVariant>
#include <QTemporaryDir>

#include "Source/Core/AutoStart.h"
#include "Source/Core/Bluetooth.h"
#include "Source/Core/OS/BlueZ.h"

namespace BlueZ = Core::OS::BlueZ;

namespace {

// Answers with a{qv} of `ay` variants, exactly the shape BlueZ puts on the wire for
// ManufacturerData.
class ManufacturerDataService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.apd.Test")

public:
    QMap<quint16, QByteArray> entries;

public slots:
    QMap<quint16, QDBusVariant> Get()
    {
        QMap<quint16, QDBusVariant> result;
        for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
            result.insert(iter.key(), QDBusVariant{iter.value()});
        }
        return result;
    }
};

// A demarshalled `QDBusArgument` can only come out of a received message, so send the data
// through a real (peer-to-peer, no bus daemon needed) D-Bus connection and hand back what the
// client sees.
QVariant RoundTrip(const QMap<quint16, QByteArray> &entries)
{
    static QTemporaryDir directory;
    static QDBusServer server{QString{"unix:tmpdir=%1"}.arg(directory.path())};
    static ManufacturerDataService service;
    static bool exported = false;
    static QDBusConnection serverConnection{QString{}};

    if (!exported) {
        qDBusRegisterMetaType<QMap<quint16, QDBusVariant>>();
        QObject::connect(&server, &QDBusServer::newConnection, [](const QDBusConnection &c) {
            serverConnection = c;
            serverConnection.registerObject("/", &service, QDBusConnection::ExportAllSlots);
        });
        exported = true;
    }
    service.entries = entries;

    auto client = QDBusConnection::connectToPeer(server.address(), "apd-test-client");
    QTest::qWait(100); // lets the server side accept and register the object
    auto message = QDBusMessage::createMethodCall(QString{}, "/", "org.apd.Test", "Get");
    QDBusPendingCallWatcher watcher{client.asyncCall(message, 5000)};
    QSignalSpy spy{&watcher, &QDBusPendingCallWatcher::finished};
    spy.wait(5000);

    QDBusPendingReply<> reply = watcher;
    QDBusConnection::disconnectFromPeer("apd-test-client");
    if (reply.isError() || reply.reply().arguments().isEmpty()) {
        return {};
    }
    return reply.reply().arguments().first();
}

} // namespace

class LinuxPlatformTests : public QObject
{
    Q_OBJECT

private slots:
    void parsesBluetoothAddresses();
    void rejectsMalformedAddresses();
    void formatsAddressesUppercase();
    void parsesModalias();
    void rejectsMalformedModalias();
    void parsesManufacturerData();
    void ignoresNonMapManufacturerData();
    void buildsDeviceFromBlueZProperties();
    void deviceFallsBackToNameAndPathAddress();
    void autoStartWritesAndRemovesDesktopEntry();
    void autoStartQuotesExecWithSpaces();
};

void LinuxPlatformTests::parsesBluetoothAddresses()
{
    QCOMPARE(BlueZ::ParseAddress("AA:BB:CC:DD:EE:FF"), std::optional<uint64_t>{0xAABBCCDDEEFF});
    QCOMPARE(BlueZ::ParseAddress("11:22:33:44:55:66"), std::optional<uint64_t>{0x112233445566});
    QCOMPARE(BlueZ::ParseAddress("  00:00:00:00:00:01 "), std::optional<uint64_t>{1});
}

void LinuxPlatformTests::rejectsMalformedAddresses()
{
    QVERIFY(!BlueZ::ParseAddress("").has_value());
    QVERIFY(!BlueZ::ParseAddress("AA:BB:CC:DD:EE").has_value());
    QVERIFY(!BlueZ::ParseAddress("AA-BB-CC-DD-EE-FF").has_value());
    QVERIFY(!BlueZ::ParseAddress("GG:BB:CC:DD:EE:FF").has_value());
    QVERIFY(!BlueZ::ParseAddress("AA:BB:CC:DD:EE:FF:00").has_value());
}

void LinuxPlatformTests::formatsAddressesUppercase()
{
    QCOMPARE(BlueZ::FormatAddress(0xAABBCCDDEEFF), QString{"AA:BB:CC:DD:EE:FF"});
    QCOMPARE(BlueZ::FormatAddress(0x112233445566), QString{"11:22:33:44:55:66"});
    QCOMPARE(BlueZ::FormatAddress(1), QString{"00:00:00:00:00:01"});
}

void LinuxPlatformTests::parsesModalias()
{
    const auto bluetooth = BlueZ::ParseModalias("bluetooth:v004Cp200Ed0100");
    QVERIFY(bluetooth.has_value());
    QCOMPARE(bluetooth->vendorId, uint16_t{0x004C});
    QCOMPARE(bluetooth->productId, uint16_t{0x200E});

    const auto usb = BlueZ::ParseModalias("usb:v05ACp2014d0001");
    QVERIFY(usb.has_value());
    QCOMPARE(usb->vendorId, uint16_t{0x05AC});
    QCOMPARE(usb->productId, uint16_t{0x2014});
}

void LinuxPlatformTests::rejectsMalformedModalias()
{
    QVERIFY(!BlueZ::ParseModalias("").has_value());
    QVERIFY(!BlueZ::ParseModalias("pci:v00001234p00005678d0001").has_value());
    QVERIFY(!BlueZ::ParseModalias("bluetooth:v004C").has_value());
}

void LinuxPlatformTests::parsesManufacturerData()
{
    const QByteArray apple{"\x07\x19\x01\x14\x20", 5};
    const QByteArray other{"\x01\x02", 2};

    const auto parsed = BlueZ::ParseManufacturerData(RoundTrip({{0x004C, apple}, {0x0006, other}}));

    QCOMPARE(parsed.size(), size_t{2});
    QCOMPARE(parsed.at(0x004C), (std::vector<uint8_t>{0x07, 0x19, 0x01, 0x14, 0x20}));
    QCOMPARE(parsed.at(0x0006), (std::vector<uint8_t>{0x01, 0x02}));
}

void LinuxPlatformTests::ignoresNonMapManufacturerData()
{
    QVERIFY(BlueZ::ParseManufacturerData(QVariant{}).empty());
    QVERIFY(BlueZ::ParseManufacturerData(QVariant{42}).empty());
    QVERIFY(BlueZ::ParseManufacturerData(QVariant{QString{"nope"}}).empty());
}

void LinuxPlatformTests::buildsDeviceFromBlueZProperties()
{
    BlueZ::DeviceInfo info;
    info.path = "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_01";
    info.properties = {
        {"Address", "AA:BB:CC:DD:EE:01"},
        {"Alias", "My AirPods Pro"},
        {"Name", "AirPods Pro"},
        {"Modalias", "bluetooth:v004Cp2014d0100"},
        {"Connected", true},
    };

    const Core::Bluetooth::Device device{info};
    QCOMPARE(device.GetAddress(), uint64_t{0xAABBCCDDEE01});
    QCOMPARE(device.GetName(), std::string{"My AirPods Pro"});
    QCOMPARE(device.GetVendorId(), uint16_t{0x004C});
    QCOMPARE(device.GetProductId(), uint16_t{0x2014});
}

void LinuxPlatformTests::deviceFallsBackToNameAndPathAddress()
{
    BlueZ::DeviceInfo info;
    info.path = "/org/bluez/hci0/dev_11_22_33_44_55_66";
    info.properties = {{"Name", "Some Headset"}};

    const Core::Bluetooth::Device device{info};
    QCOMPARE(device.GetAddress(), uint64_t{0x112233445566});
    QCOMPARE(device.GetName(), std::string{"Some Headset"});
    QCOMPARE(device.GetVendorId(), uint16_t{0});
    QCOMPARE(device.GetProductId(), uint16_t{0});
}

void LinuxPlatformTests::autoStartWritesAndRemovesDesktopEntry()
{
    QTemporaryDir config;
    QVERIFY(config.isValid());
    qputenv("XDG_CONFIG_HOME", config.path().toLocal8Bit());

    const auto entry = config.filePath("autostart/AirPodsDesktop.desktop");
    const auto service = Core::AutoStart::CreateAutoStartService();

    service->SetEnabled(true);
    QVERIFY(QFile::exists(entry));

    QFile file{entry};
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const auto content = QString::fromUtf8(file.readAll());
    QVERIFY(content.startsWith("[Desktop Entry]\n"));
    QVERIFY(content.contains("Type=Application\n"));
    QVERIFY(content.contains("Exec="));
    QVERIFY(content.contains("Terminal=false\n"));
    file.close();

    // Enabling twice rewrites rather than appends.
    service->SetEnabled(true);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(file.readAll()).count("[Desktop Entry]"), 1);
    file.close();

    service->SetEnabled(false);
    QVERIFY(!QFile::exists(entry));

    // Disabling when nothing is there is fine.
    service->SetEnabled(false);
}

void LinuxPlatformTests::autoStartQuotesExecWithSpaces()
{
    QTemporaryDir config;
    QVERIFY(config.isValid());
    qputenv("XDG_CONFIG_HOME", config.path().toLocal8Bit());

    const auto service = Core::AutoStart::CreateAutoStartService();
    service->SetEnabled(true);

    QFile file{config.filePath("autostart/AirPodsDesktop.desktop")};
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString exec;
    for (const auto &line : QString::fromUtf8(file.readAll()).split('\n')) {
        if (line.startsWith("Exec=")) {
            exec = line.mid(5);
        }
    }

    // Quoting is only required when the path of this very executable needs it.
    const auto path = QCoreApplication::applicationFilePath();
    if (path.contains(' ')) {
        QVERIFY(exec.startsWith('"') && exec.endsWith('"'));
    }
    else {
        QCOMPARE(exec, QString{path}.replace("%", "%%"));
    }
}

QTEST_GUILESS_MAIN(LinuxPlatformTests)

#include "LinuxPlatformTests.moc"
