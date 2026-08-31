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

#pragma once

#if !defined APD_OS_WIN
    #error "This file shouldn't be compiled."
#endif

#include "QuickConnect.h"
#include "OS/Windows.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>

#include <mmdeviceapi.h>

namespace Core::QuickConnect {

class WindowsBackend final : public Backend, public IMMNotificationClient
{
public:
    WindowsBackend();
    ~WindowsBackend() override;

    void SetController(Controller *controller) override;
    std::vector<Device> ListDevices(std::stop_token stopToken) override;
    bool RequestReconnect(const QString &id, std::stop_token stopToken) override;
    void StopObserving() override;

    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override;

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override;
    HRESULT STDMETHODCALLTYPE
    OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR defaultDeviceId) override;
    HRESULT STDMETHODCALLTYPE
    OnPropertyValueChanged(LPCWSTR deviceId, const PROPERTYKEY key) override;

private:
    // What `OnDeviceStateChanged` needs in order to decide, without touching COM.
    //
    // The MMDevice API forbids a notification callback from blocking, from releasing the final
    // reference on an MMDevice object, and from calling back into the API. Resolving an endpoint's
    // container id inside the callback would do all three, so `RequestReconnect` resolves the
    // endpoint ids up front and the callback only compares strings against this snapshot.
    struct Observation {
        QString containerId;           // canonical `{GUID}`, handed to the Controller
        std::set<QString> endpointIds; // exact IMMDevice ids belonging to that container
    };

    OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> _deviceEnumerator;
    std::atomic<std::shared_ptr<const Observation>> _observation;
    Controller *_controller{};
    std::atomic<ULONG> _refCount{1};
    std::mutex _controllerMutex;
    bool _comInitialized{};

    void RegisterNotifications();
    void UnregisterNotifications();
    void QueueEndpointState(const QString &containerId, bool connected);
};

} // namespace Core::QuickConnect
