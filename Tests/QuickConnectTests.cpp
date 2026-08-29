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

#include <chrono>
#include <future>

#include "Source/Core/QuickConnect.h"
#include "Source/Gui/TrayActivation.h"

class FakeBackend final : public Core::QuickConnect::Backend
{
public:
    std::vector<Core::QuickConnect::Device> devices{{"{A}", "AirPods", false}};
    Core::QuickConnect::Controller *controller{};
    int listCalls{};
    int reconnectCalls{};
    int setControllerCalls{};

    void SetController(Core::QuickConnect::Controller *value) override
    {
        controller = value;
        ++setControllerCalls;
    }

    std::vector<Core::QuickConnect::Device> ListDevices() override
    {
        ++listCalls;
        return devices;
    }

    bool RequestReconnect(const QString &id) override
    {
        ++reconnectCalls;
        return id == "{A}";
    }
};

class BlockingBackend final : public Core::QuickConnect::Backend
{
private:
    std::promise<void> releasePromise;

public:
    std::promise<void> entered;
    std::shared_future<void> release{releasePromise.get_future()};

    std::vector<Core::QuickConnect::Device> ListDevices() override
    {
        entered.set_value();
        release.wait();
        return {{"{A}", "AirPods", false}};
    }

    bool RequestReconnect(const QString &) override { return false; }

    void Unblock() { releasePromise.set_value(); }
};

class QuickConnectTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void disabledRequestDoesNotCallBackend();
    void emptyDeviceIdReturnsNoDevice();
    void missingDeviceReturnsNoDevice();
    void connectedDeviceReturnsAlreadyConnected();
    void connectedDeviceDoesNotReconnect();
    void failedReconnectReturnsFailed();
    void pendingRequestIsDeduplicated();
    void pendingRequestKeepsOriginalDevice();
    void endpointActivationCompletesRequest();
    void timeoutCompletesRequestOnce();
    void controllerDetachesBackendOnDestruction();
    void trayActivationPreservesDefaultBehaviorWhenDisabled();
    void trayDoubleClickCancelsPendingQuickConnect();
    void deviceRefreshDoesNotBlockTheGuiThread();
};

void QuickConnectTests::initTestCase()
{
    qRegisterMetaType<Core::QuickConnect::Outcome>();
}

void QuickConnectTests::disabledRequestDoesNotCallBackend()
{
    FakeBackend backend;
    Core::QuickConnect::Controller controller{backend};
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::Disabled);
    QCOMPARE(backend.reconnectCalls, 0);
}

void QuickConnectTests::emptyDeviceIdReturnsNoDevice()
{
    FakeBackend backend;
    Core::QuickConnect::Controller controller{backend};
    controller.SetEnabled(true);

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::NoDevice);
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

void QuickConnectTests::connectedDeviceDoesNotReconnect()
{
    FakeBackend backend;
    backend.devices[0].connected = true;
    Core::QuickConnect::Controller controller{backend};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::AlreadyConnected);
    QCOMPARE(backend.reconnectCalls, 0);
}

void QuickConnectTests::failedReconnectReturnsFailed()
{
    FakeBackend backend;
    backend.devices = {{"{B}", "AirPods Pro", false}};
    Core::QuickConnect::Controller controller{backend};
    controller.SetEnabled(true);
    controller.SetDeviceId("{B}");

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
    QCOMPARE(backend.listCalls, 1);
    QCOMPARE(backend.reconnectCalls, 1);
}

void QuickConnectTests::pendingRequestKeepsOriginalDevice()
{
    FakeBackend backend;
    Core::QuickConnect::Controller controller{backend};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::RequestStarted);
    backend.devices[0].connected = true;
    controller.SetDeviceId("{B}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::RequestStarted);
    QCOMPARE(backend.listCalls, 1);
    QCOMPARE(backend.reconnectCalls, 1);
    QCOMPARE(controller.OnEndpointStateChanged("{A}", true), Core::QuickConnect::Outcome::Connected);
}

void QuickConnectTests::endpointActivationCompletesRequest()
{
    FakeBackend backend;
    Core::QuickConnect::Controller controller{backend};
    QSignalSpy spy{&controller, &Core::QuickConnect::Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::RequestStarted);
    spy.clear();
    QCOMPARE(controller.OnEndpointStateChanged("{A}", true), Core::QuickConnect::Outcome::Connected);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<Core::QuickConnect::Outcome>(), Core::QuickConnect::Outcome::Connected);
    QCOMPARE(spy.at(0).at(1).toString(), QString("AirPods"));
    QCOMPARE(controller.OnEndpointStateChanged("{A}", true), Core::QuickConnect::Outcome::Connected);
    QCOMPARE(spy.count(), 1);
}

void QuickConnectTests::timeoutCompletesRequestOnce()
{
    FakeBackend backend;
    Core::QuickConnect::Controller controller{backend};
    QSignalSpy spy{&controller, &Core::QuickConnect::Controller::OutcomeChanged};
    controller.SetEnabled(true);
    controller.SetDeviceId("{A}");

    QCOMPARE(controller.Request(), Core::QuickConnect::Outcome::RequestStarted);
    spy.clear();
    QCOMPARE(controller.ResolveTimedOut(), Core::QuickConnect::Outcome::TimedOut);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<Core::QuickConnect::Outcome>(), Core::QuickConnect::Outcome::TimedOut);
    QCOMPARE(spy.at(0).at(1).toString(), QString("AirPods"));
    QCOMPARE(controller.ResolveTimedOut(), Core::QuickConnect::Outcome::TimedOut);
    QCOMPARE(spy.count(), 1);
}

void QuickConnectTests::controllerDetachesBackendOnDestruction()
{
    FakeBackend backend;
    {
        Core::QuickConnect::Controller controller{backend};
        QCOMPARE(backend.controller, &controller);
    }

    QVERIFY(backend.controller == nullptr);
    QCOMPARE(backend.setControllerCalls, 2);
}

void QuickConnectTests::trayActivationPreservesDefaultBehaviorWhenDisabled()
{
    using Action = Gui::TrayActivationAction;

    QCOMPARE(Gui::RouteTrayActivation(QSystemTrayIcon::Trigger, false), Action::ShowMainWindow);
    QCOMPARE(Gui::RouteTrayActivation(QSystemTrayIcon::Trigger, true), Action::QuickConnect);
    QCOMPARE(Gui::RouteTrayActivation(QSystemTrayIcon::DoubleClick, true), Action::ShowMainWindow);
    QCOMPARE(Gui::RouteTrayActivation(QSystemTrayIcon::MiddleClick, true), Action::ShowMainWindow);
    QCOMPARE(Gui::RouteTrayActivation(QSystemTrayIcon::Unknown, true), Action::None);
}

void QuickConnectTests::trayDoubleClickCancelsPendingQuickConnect()
{
    using Action = Gui::TrayActivationAction;

    Gui::TrayActivationState state;
    QCOMPARE(state.OnActivation(QSystemTrayIcon::Trigger, true), Action::None);
    QCOMPARE(state.OnActivation(QSystemTrayIcon::DoubleClick, true), Action::ShowMainWindow);
    QCOMPARE(state.OnSingleClickTimeout(), Action::None);

    QCOMPARE(state.OnActivation(QSystemTrayIcon::Trigger, true), Action::None);
    QCOMPARE(state.OnSingleClickTimeout(), Action::QuickConnect);
}

void QuickConnectTests::deviceRefreshDoesNotBlockTheGuiThread()
{
    using namespace std::chrono_literals;

    BlockingBackend backend;
    Core::QuickConnect::Controller controller{backend};
    QSignalSpy spy{&controller, &Core::QuickConnect::Controller::DevicesChanged};

    controller.RefreshDevices();
    QVERIFY(backend.entered.get_future().wait_for(1s) == std::future_status::ready);
    QCOMPARE(spy.count(), 0);

    backend.Unblock();
    QVERIFY(spy.wait(1'000));
    QCOMPARE(controller.Devices().size(), std::size_t{1});
}

QTEST_APPLESS_MAIN(QuickConnectTests)

#include "QuickConnectTests.moc"
