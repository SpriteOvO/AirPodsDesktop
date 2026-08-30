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

#include "QuickConnect_win.h"

#include <Functiondiscoverykeys_devpkey.h>
#include <devicetopology.h>
#include <ks.h>
#include <ksmedia.h>
#include <propvarutil.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <map>
#include <set>

#include <QMetaObject>

#include "../Logger.h"

namespace Core::QuickConnect {
namespace {

constexpr auto kPropertyAepContainerId = L"System.Devices.Aep.ContainerId";

struct EndpointInfo {
    QString id;
    QString containerId;
    DWORD state{};
};

class ScopedComApartment final
{
public:
    ScopedComApartment()
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        _uninitialize = SUCCEEDED(result);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
            LOG(Warn, "CoInitializeEx failed. HRESULT: {:#x}", result);
        }
    }

    ~ScopedComApartment()
    {
        if (_uninitialize) {
            CoUninitialize();
        }
    }

private:
    bool _uninitialize{};
};

QString GuidToString(const GUID &guid)
{
    wchar_t buffer[39]{};
    if (StringFromGUID2(guid, buffer, 39) == 0) {
        return {};
    }
    return QString::fromWCharArray(buffer);
}

GUID ToGuid(const winrt::guid &guid)
{
    return {
        guid.Data1,
        guid.Data2,
        guid.Data3,
        {
            guid.Data4[0],
            guid.Data4[1],
            guid.Data4[2],
            guid.Data4[3],
            guid.Data4[4],
            guid.Data4[5],
            guid.Data4[6],
            guid.Data4[7],
        },
    };
}

QString NormalizeContainerId(QString id)
{
    id = id.trimmed();
    if (id.startsWith('{') && id.endsWith('}')) {
        id = id.mid(1, id.size() - 2);
    }
    return id.toLower();
}

QString CanonicalContainerId(const QString &id)
{
    const auto normalized = NormalizeContainerId(id);
    if (normalized.isEmpty()) {
        return {};
    }
    return "{" + normalized.toUpper() + "}";
}

QString ContainerIdFromInspectable(const winrt::Windows::Foundation::IInspectable &inspectable)
{
    if (!inspectable) {
        return {};
    }

    try {
        return QString::fromWCharArray(winrt::unbox_value<winrt::hstring>(inspectable).c_str());
    }
    catch (const winrt::hresult_error &) {
    }

    try {
        return GuidToString(ToGuid(winrt::unbox_value<winrt::guid>(inspectable)));
    }
    catch (const winrt::hresult_error &) {
    }

    return {};
}

QString ContainerIdFromProperty(const PROPVARIANT &value)
{
    if (value.vt == VT_CLSID && value.puuid != nullptr) {
        return GuidToString(*value.puuid);
    }
    if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        return QString::fromWCharArray(value.pwszVal);
    }
    if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
        return QString::fromWCharArray(value.bstrVal);
    }
    return {};
}

OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> CreateDeviceEnumerator()
{
    OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> enumerator;
    const HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, enumerator.GetIID(),
        reinterpret_cast<void **>(enumerator.ReleaseAndAddressOf()));
    if (FAILED(result)) {
        LOG(Warn, "Create MMDeviceEnumerator failed. HRESULT: {:#x}", result);
        return {};
    }
    return enumerator;
}

std::optional<QString> GetEndpointContainerId(IMMDevice &endpoint)
{
    OS::Windows::Com::UniquePtr<IPropertyStore> propertyStore;
    HRESULT result = endpoint.OpenPropertyStore(STGM_READ, propertyStore.ReleaseAndAddressOf());
    if (FAILED(result)) {
        LOG(Warn, "Open endpoint property store failed. HRESULT: {:#x}", result);
        return std::nullopt;
    }

    PROPVARIANT containerValue;
    PropVariantInit(&containerValue);
    result = propertyStore->GetValue(PKEY_Device_ContainerId, &containerValue);
    if (FAILED(result)) {
        LOG(Warn, "Read endpoint container id failed. HRESULT: {:#x}", result);
        PropVariantClear(&containerValue);
        return std::nullopt;
    }

    const auto containerId = CanonicalContainerId(ContainerIdFromProperty(containerValue));
    PropVariantClear(&containerValue);
    if (containerId.isEmpty()) {
        return std::nullopt;
    }
    return containerId;
}

std::vector<EndpointInfo> EnumerateAudioEndpoints(std::stop_token stopToken)
{
    ScopedComApartment apartment;

    std::vector<EndpointInfo> endpoints;
    if (stopToken.stop_requested()) {
        return endpoints;
    }

    auto enumerator = CreateDeviceEnumerator();
    if (!enumerator) {
        return endpoints;
    }

    OS::Windows::Com::UniquePtr<IMMDeviceCollection> collection;
    HRESULT result =
        enumerator->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, collection.ReleaseAndAddressOf());
    if (FAILED(result)) {
        LOG(Warn, "EnumAudioEndpoints failed. HRESULT: {:#x}", result);
        return endpoints;
    }

    UINT count{};
    result = collection->GetCount(&count);
    if (FAILED(result)) {
        LOG(Warn, "IMMDeviceCollection::GetCount failed. HRESULT: {:#x}", result);
        return endpoints;
    }

    endpoints.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        if (stopToken.stop_requested()) {
            break;
        }

        OS::Windows::Com::UniquePtr<IMMDevice> endpoint;
        result = collection->Item(i, endpoint.ReleaseAndAddressOf());
        if (FAILED(result)) {
            LOG(Warn, "IMMDeviceCollection::Item failed. HRESULT: {:#x}", result);
            continue;
        }

        LPWSTR rawEndpointId{};
        result = endpoint->GetId(&rawEndpointId);
        if (FAILED(result)) {
            LOG(Warn, "IMMDevice::GetId failed. HRESULT: {:#x}", result);
            continue;
        }
        const QString endpointId = QString::fromWCharArray(rawEndpointId);
        CoTaskMemFree(rawEndpointId);

        auto containerId = GetEndpointContainerId(*endpoint.operator->());
        if (!containerId.has_value()) {
            continue;
        }

        DWORD state{};
        result = endpoint->GetState(&state);
        if (FAILED(result)) {
            LOG(Warn, "IMMDevice::GetState failed. HRESULT: {:#x}", result);
            continue;
        }

        endpoints.push_back({endpointId, containerId.value(), state});
    }

    return endpoints;
}

std::map<QString, Device> EnumeratePairedBluetoothDevices(std::stop_token stopToken)
{
    OS::Windows::Winrt::Initialize();

    namespace WinrtBluetooth = winrt::Windows::Devices::Bluetooth;
    namespace WinrtDevices = winrt::Windows::Devices::Enumeration;

    std::map<QString, Device> devices;

    try {
        auto properties = winrt::single_threaded_vector<winrt::hstring>();
        properties.Append(kPropertyAepContainerId);

        const auto operation = WinrtDevices::DeviceInformation::FindAllAsync(
            WinrtBluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(true), properties);
        std::stop_callback cancelOperation{stopToken, [operation]() mutable { operation.Cancel(); }};
        const auto collection = operation.get();

        for (uint32_t i = 0; i < collection.Size(); ++i) {
            if (stopToken.stop_requested()) {
                break;
            }

            const auto info = collection.GetAt(i);
            const auto rawContainerId =
                ContainerIdFromInspectable(info.Properties().TryLookup(kPropertyAepContainerId));
            const auto containerId = CanonicalContainerId(rawContainerId);
            const auto containerKey = NormalizeContainerId(containerId);
            if (containerKey.isEmpty()) {
                continue;
            }

            auto name = QString::fromWCharArray(info.Name().c_str());
            if (name.isEmpty()) {
                name = containerId;
            }

            devices.try_emplace(containerKey, Device{containerId, name, false});
        }
    }
    catch (const OS::Windows::Winrt::Exception &ex) {
        if (!stopToken.stop_requested()) {
            LOG(Warn, "Bluetooth paired device enumeration failed. {}", Helper::ToString(ex));
        }
    }

    return devices;
}

std::optional<QString> GetEndpointContainerIdById(const QString &endpointId)
{
    ScopedComApartment apartment;

    auto enumerator = CreateDeviceEnumerator();
    if (!enumerator) {
        return std::nullopt;
    }

    OS::Windows::Com::UniquePtr<IMMDevice> endpoint;
    const auto endpointIdW = endpointId.toStdWString();
    const HRESULT result = enumerator->GetDevice(endpointIdW.c_str(), endpoint.ReleaseAndAddressOf());
    if (FAILED(result)) {
        LOG(Warn, "IMMDeviceEnumerator::GetDevice failed. HRESULT: {:#x}", result);
        return std::nullopt;
    }

    return GetEndpointContainerId(*endpoint.operator->());
}

HRESULT RequestEndpointReconnect(const QString &endpointId, std::stop_token stopToken)
{
    ScopedComApartment apartment;

    if (stopToken.stop_requested()) {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    auto enumerator = CreateDeviceEnumerator();
    if (!enumerator) {
        return E_FAIL;
    }

    OS::Windows::Com::UniquePtr<IMMDevice> endpoint;
    const auto endpointIdW = endpointId.toStdWString();
    HRESULT result = enumerator->GetDevice(endpointIdW.c_str(), endpoint.ReleaseAndAddressOf());
    if (FAILED(result)) {
        LOG(Warn, "Get endpoint for reconnect failed. HRESULT: {:#x}", result);
        return result;
    }
    if (stopToken.stop_requested()) {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    OS::Windows::Com::UniquePtr<IDeviceTopology> topology;
    result = endpoint->Activate(
        topology.GetIID(), CLSCTX_ALL, nullptr,
        reinterpret_cast<void **>(topology.ReleaseAndAddressOf()));
    if (FAILED(result)) {
        LOG(Warn, "Activate IDeviceTopology failed. Endpoint: {}, HRESULT: {:#x}", endpointId, result);
        return result;
    }
    if (stopToken.stop_requested()) {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    OS::Windows::Com::UniquePtr<IConnector> connector;
    result = topology->GetConnector(0, connector.ReleaseAndAddressOf());
    if (FAILED(result)) {
        LOG(Warn, "IDeviceTopology::GetConnector failed. Endpoint: {}, HRESULT: {:#x}", endpointId, result);
        return result;
    }
    if (stopToken.stop_requested()) {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    OS::Windows::Com::UniquePtr<IConnector> connectedConnector;
    result = connector->GetConnectedTo(connectedConnector.ReleaseAndAddressOf());
    if (FAILED(result)) {
        LOG(Warn, "IConnector::GetConnectedTo failed. Endpoint: {}, HRESULT: {:#x}", endpointId, result);
        return result;
    }
    if (stopToken.stop_requested()) {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    OS::Windows::Com::UniquePtr<IPart> part;
    result = connectedConnector->QueryInterface(
        part.GetIID(), reinterpret_cast<void **>(part.ReleaseAndAddressOf()));
    if (FAILED(result)) {
        LOG(Warn, "Query IPart failed. Endpoint: {}, HRESULT: {:#x}", endpointId, result);
        return result;
    }
    if (stopToken.stop_requested()) {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    OS::Windows::Com::UniquePtr<IDeviceTopology> connectedTopology;
    result = part->GetTopologyObject(connectedTopology.ReleaseAndAddressOf());
    if (FAILED(result)) {
        LOG(Warn, "Get connected topology failed. Endpoint: {}, HRESULT: {:#x}", endpointId, result);
        return result;
    }
    if (stopToken.stop_requested()) {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    LPWSTR rawConnectedDeviceId{};
    result = connectedTopology->GetDeviceId(&rawConnectedDeviceId);
    if (FAILED(result)) {
        LOG(Warn, "Get connected device id failed. Endpoint: {}, HRESULT: {:#x}", endpointId, result);
        return result;
    }

    OS::Windows::Com::UniquePtr<IMMDevice> connectedDevice;
    result = enumerator->GetDevice(rawConnectedDeviceId, connectedDevice.ReleaseAndAddressOf());
    CoTaskMemFree(rawConnectedDeviceId);
    if (FAILED(result)) {
        LOG(Warn, "Get connected device failed. Endpoint: {}, HRESULT: {:#x}", endpointId, result);
        return result;
    }
    if (stopToken.stop_requested()) {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    OS::Windows::Com::UniquePtr<IKsControl> ksControl;
    result = connectedDevice->Activate(
        ksControl.GetIID(), CLSCTX_ALL, nullptr,
        reinterpret_cast<void **>(ksControl.ReleaseAndAddressOf()));
    if (FAILED(result)) {
        LOG(Warn, "Activate IKsControl failed. Endpoint: {}, HRESULT: {:#x}", endpointId, result);
        return result;
    }
    if (stopToken.stop_requested()) {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    KSPROPERTY property{
        KSPROPSETID_BtAudio,
        KSPROPERTY_ONESHOT_RECONNECT,
        KSPROPERTY_TYPE_GET,
    };
    ULONG returned{};
    result = ksControl->KsProperty(
        reinterpret_cast<PKSPROPERTY>(&property), sizeof(property), nullptr, 0, &returned);
    if (FAILED(result)) {
        LOG(Warn, "BtAudio reconnect request failed. Endpoint: {}, HRESULT: {:#x}", endpointId, result);
    }
    return result;
}

} // namespace

WindowsBackend::WindowsBackend()
{
    const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    _comInitialized = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        LOG(Warn, "WindowsBackend COM initialization failed. HRESULT: {:#x}", result);
    }

    RegisterNotifications();
}

WindowsBackend::~WindowsBackend()
{
    UnregisterNotifications();
    if (_comInitialized) {
        CoUninitialize();
    }
}

void WindowsBackend::SetController(Controller *controller)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _controller = controller;
}

std::vector<Device> WindowsBackend::ListDevices(std::stop_token stopToken)
{
    auto pairedDevices = EnumeratePairedBluetoothDevices(stopToken);
    if (stopToken.stop_requested()) {
        return {};
    }
    const auto endpoints = EnumerateAudioEndpoints(stopToken);

    std::set<QString> endpointContainerKeys;
    std::set<QString> activeEndpointContainerKeys;
    for (const auto &endpoint : endpoints) {
        const auto key = NormalizeContainerId(endpoint.containerId);
        if (key.isEmpty()) {
            continue;
        }
        endpointContainerKeys.insert(key);
        if (endpoint.state == DEVICE_STATE_ACTIVE) {
            activeEndpointContainerKeys.insert(key);
        }
    }

    std::vector<Device> devices;
    devices.reserve(pairedDevices.size());
    for (auto &[containerKey, device] : pairedDevices) {
        if (!endpointContainerKeys.contains(containerKey)) {
            continue;
        }

        device.connected = activeEndpointContainerKeys.contains(containerKey);
        devices.push_back(device);
    }

    return devices;
}

bool WindowsBackend::RequestReconnect(const QString &id, std::stop_token stopToken)
{
    if (stopToken.stop_requested()) {
        return false;
    }

    const auto selectedContainerKey = NormalizeContainerId(id);
    if (selectedContainerKey.isEmpty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock{_mutex};
        _observedContainerKey = selectedContainerKey;
    }

    bool accepted = false;
    const auto endpoints = EnumerateAudioEndpoints(stopToken);
    for (const auto &endpoint : endpoints) {
        if (stopToken.stop_requested()) {
            break;
        }

        if (NormalizeContainerId(endpoint.containerId) != selectedContainerKey) {
            continue;
        }

        const HRESULT result = RequestEndpointReconnect(endpoint.id, stopToken);
        accepted = accepted || result == S_OK;
    }

    if (!accepted) {
        std::lock_guard<std::mutex> lock{_mutex};
        _observedContainerKey.clear();
        if (!stopToken.stop_requested()) {
            LOG(Warn, "No endpoint accepted BtAudio reconnect request. Container: {}", id);
        }
    }
    return accepted;
}

ULONG WindowsBackend::AddRef()
{
    return ++_refCount;
}

ULONG WindowsBackend::Release()
{
    const auto count = --_refCount;
    return count;
}

HRESULT WindowsBackend::QueryInterface(REFIID riid, void **object)
{
    if (object == nullptr) {
        return E_POINTER;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
        *object = static_cast<IMMNotificationClient *>(this);
        AddRef();
        return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
}

HRESULT WindowsBackend::OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState)
{
    if (deviceId == nullptr) {
        return S_OK;
    }

    QString observedContainerKey;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        observedContainerKey = _observedContainerKey;
    }

    if (observedContainerKey.isEmpty()) {
        return S_OK;
    }

    const auto containerId = GetEndpointContainerIdById(QString::fromWCharArray(deviceId));
    if (!containerId.has_value() || NormalizeContainerId(containerId.value()) != observedContainerKey) {
        return S_OK;
    }

    QueueEndpointState(CanonicalContainerId(containerId.value()), newState == DEVICE_STATE_ACTIVE);
    return S_OK;
}

HRESULT WindowsBackend::OnDeviceAdded(LPCWSTR deviceId)
{
    Q_UNUSED(deviceId)
    return S_OK;
}

HRESULT WindowsBackend::OnDeviceRemoved(LPCWSTR deviceId)
{
    Q_UNUSED(deviceId)
    return S_OK;
}

HRESULT WindowsBackend::OnDefaultDeviceChanged(
    EDataFlow flow, ERole role, LPCWSTR defaultDeviceId)
{
    Q_UNUSED(flow)
    Q_UNUSED(role)
    Q_UNUSED(defaultDeviceId)
    return S_OK;
}

HRESULT WindowsBackend::OnPropertyValueChanged(LPCWSTR deviceId, const PROPERTYKEY key)
{
    Q_UNUSED(deviceId)
    Q_UNUSED(key)
    return S_OK;
}

void WindowsBackend::RegisterNotifications()
{
    _deviceEnumerator = CreateDeviceEnumerator();
    if (!_deviceEnumerator) {
        return;
    }

    const HRESULT result = _deviceEnumerator->RegisterEndpointNotificationCallback(this);
    if (FAILED(result)) {
        LOG(Warn, "RegisterEndpointNotificationCallback failed. HRESULT: {:#x}", result);
        _deviceEnumerator = nullptr;
    }
}

void WindowsBackend::UnregisterNotifications()
{
    if (!_deviceEnumerator) {
        return;
    }

    const HRESULT result = _deviceEnumerator->UnregisterEndpointNotificationCallback(this);
    if (FAILED(result)) {
        LOG(Warn, "UnregisterEndpointNotificationCallback failed. HRESULT: {:#x}", result);
    }
    _deviceEnumerator = nullptr;
}

void WindowsBackend::QueueEndpointState(const QString &containerId, bool connected)
{
    std::lock_guard<std::mutex> lock{_mutex};
    const QPointer<Controller> controller = _controller;
    if (controller.isNull()) {
        return;
    }

    QMetaObject::invokeMethod(
        controller,
        [controller, containerId, connected]() {
            if (!controller.isNull()) {
                controller->OnEndpointStateChanged(containerId, connected);
            }
        },
        Qt::QueuedConnection);
}

} // namespace Core::QuickConnect
