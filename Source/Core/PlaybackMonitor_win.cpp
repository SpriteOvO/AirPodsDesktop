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

#include "PlaybackMonitor.h"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>

namespace Core::GlobalMedia {
namespace {

using Microsoft::WRL::ComPtr;

std::optional<bool> ReadPlaybackActivity()
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) {
        return std::nullopt;
    }
    struct Apartment {
        ~Apartment()
        {
            CoUninitialize();
        }
    } apartment;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
    {
        return std::nullopt;
    }
    ComPtr<IMMDeviceCollection> endpoints;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &endpoints))) {
        return std::nullopt;
    }
    UINT count{};
    if (FAILED(endpoints->GetCount(&count))) {
        return std::nullopt;
    }
    bool readable = count == 0;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> endpoint;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(endpoints->Item(i, &endpoint)) ||
            FAILED(endpoint->Activate(
                __uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                reinterpret_cast<void **>(manager.GetAddressOf()))) ||
            FAILED(manager->GetSessionEnumerator(&sessions)))
        {
            continue;
        }
        int sessionCount{};
        if (FAILED(sessions->GetCount(&sessionCount))) {
            continue;
        }
        readable = true;
        for (int j = 0; j < sessionCount; ++j) {
            ComPtr<IAudioSessionControl> session;
            ComPtr<IAudioSessionControl2> details;
            if (FAILED(sessions->GetSession(j, &session)) || FAILED(session.As(&details))) {
                continue;
            }
            DWORD processId{};
            // Ignore this application's own connection sounds to avoid feedback loops.
            if (FAILED(details->GetProcessId(&processId)) || processId == GetCurrentProcessId()) {
                continue;
            }
            // Session activity tracks playback even through quiet passages or muted output.
            AudioSessionState state{};
            if (SUCCEEDED(session->GetState(&state)) && state == AudioSessionStateActive) {
                return true;
            }
        }
    }
    return readable ? std::optional<bool>{false} : std::nullopt;
}

} // namespace

PlaybackProbe CreatePlaybackProbe()
{
    return ReadPlaybackActivity;
}

} // namespace Core::GlobalMedia
