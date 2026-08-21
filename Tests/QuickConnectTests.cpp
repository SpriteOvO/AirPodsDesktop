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

#include "Source/Core/QuickConnect.h"

class FakeBackend final : public Core::QuickConnect::Backend
{
public:
    std::vector<Core::QuickConnect::Device> devices{{"{A}", "AirPods", false}};
    int reconnectCalls{};

    std::vector<Core::QuickConnect::Device> ListDevices() override
    {
        return devices;
    }

    bool RequestReconnect(const QString &id) override
    {
        ++reconnectCalls;
        return id == "{A}";
    }
};

class QuickConnectTests : public QObject
{
    Q_OBJECT

private slots:
    void disabledRequestDoesNotCallBackend();
    void missingDeviceReturnsNoDevice();
    void connectedDeviceReturnsAlreadyConnected();
    void failedReconnectReturnsFailed();
    void pendingRequestIsDeduplicated();
};

void QuickConnectTests::disabledRequestDoesNotCallBackend()
{
    FakeBackend backend;
    Core::QuickConnect::Controller controller{backend};
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::Disabled);
    QCOMPARE(backend.reconnectCalls, 0);
}

void QuickConnectTests::missingDeviceReturnsNoDevice()
{
    FakeBackend backend;
    Core::QuickConnect::Controller controller{backend};
    controller.SetEnabled(true);
    controller.SetDeviceId("{Missing}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::NoDevice);
    QCOMPARE(backend.reconnectCalls, 0);
}

void QuickConnectTests::connectedDeviceReturnsAlreadyConnected()
{
    FakeBackend backend;
    backend.devices = {{"{A}", "AirPods", true}};
    Core::QuickConnect::Controller controller{backend};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::AlreadyConnected);
    QCOMPARE(backend.reconnectCalls, 0);
}

void QuickConnectTests::failedReconnectReturnsFailed()
{
    FakeBackend backend;
    Core::QuickConnect::Controller controller{backend};
    controller.SetEnabled(true);
    controller.SetDeviceId("{B}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::NoDevice);
    QCOMPARE(backend.reconnectCalls, 0);

    backend.devices = {{"{B}", "AirPods Pro", false}};

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::Failed);
    QCOMPARE(backend.reconnectCalls, 1);
}

void QuickConnectTests::pendingRequestIsDeduplicated()
{
    FakeBackend backend;
    Core::QuickConnect::Controller controller{backend};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::RequestStarted);
    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::RequestStarted);
    QCOMPARE(backend.reconnectCalls, 1);
}

QTEST_APPLESS_MAIN(QuickConnectTests)

#include "QuickConnectTests.moc"
