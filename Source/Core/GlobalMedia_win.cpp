//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
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

#include "GlobalMedia_win.h"

#include <spdlog/spdlog.h>

#include "../Utils.h"
#include "OS/Windows.h"

namespace Core::GlobalMedia {

using namespace std::chrono_literals;
using namespace winrt::Windows::Media::Control;

namespace Details {

class MediaProgramThroughVirtualKeyAbstract : public MediaProgramAbstract {
public:
    enum WindowMatchingFlag : uint32_t {
        HasChildren = 0b00000001,
        NonEmptyTitle = 0b00000010,
    };
    Q_DECLARE_FLAGS(WindowMatchingFlags, WindowMatchingFlag)

    virtual inline ~MediaProgramThroughVirtualKeyAbstract() {}

    bool IsAvailable() override {
        if (_windowProcess.has_value()) {
            return true;
        }

        _windowProcess = FindWindowAndProcess();
        if (!_windowProcess.has_value()) {
            return false;
        }

        _audioMeterInfo = GetProcessAudioMeterInfo();
        return true;
    }

    bool IsPlaying() const override {
        auto optAudioVolume = GetProcessAudioVolume();
        return optAudioVolume.has_value() && optAudioVolume.value() != 0.f;
    }

    Status Play() override {
        SPDLOG_TRACE("Do play.");

        if (IsPlaying()) {
            SPDLOG_TRACE("The media program is already playing.");
            return Status::GlobalMediaPlayAlreadyPlaying;
        }
        return Switch();
    }

    Status Pause() override {
        SPDLOG_TRACE("Do pause.");

        if (!IsPlaying()) {
            SPDLOG_TRACE("The media program nothing is playingg.");
            return Status::GlobalMediaPauseNothingIsPlaying;
        }
        return Switch();
    }

protected:
    virtual std::wstring GetProcessName() const = 0;
    virtual std::wstring GetWindowClassName() const = 0;
    virtual std::optional<std::wstring> GetWindowTitleName() const {
        return std::nullopt;
    }
    virtual WindowMatchingFlags GetWindowMatchingFlags() const = 0;

private:
    std::optional<std::pair<HWND, uint32_t>> _windowProcess;
    OS::Windows::Com::UniquePtr<IAudioMeterInformation> _audioMeterInfo;

    OS::Windows::Com::UniquePtr<IAudioMeterInformation> GetProcessAudioMeterInfo() {
        SPDLOG_TRACE("Try to get IAudioMeterInformation of this process.");

        OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> deviceEnumerator;
        HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, deviceEnumerator.GetIID(),
            (void **)deviceEnumerator.ReleaseAndAddressOf());
        if (FAILED(result)) {
            SPDLOG_TRACE(
                "Create COM instance 'IMMDeviceEnumerator' failed. HRESULT: {:#x}", result);
            return {};
        }

        OS::Windows::Com::UniquePtr<IMMDevice> audioEndpoint;
        result = deviceEnumerator->GetDefaultAudioEndpoint(
            eRender, eMultimedia, audioEndpoint.ReleaseAndAddressOf());
        if (FAILED(result)) {
            SPDLOG_TRACE("'GetDefaultAudioEndpoint' failed. HRESULT: {:#x}", result);
            return {};
        }

        OS::Windows::Com::UniquePtr<IAudioSessionManager2> sessionMgr;
        result = audioEndpoint->Activate(
            sessionMgr.GetIID(), CLSCTX_ALL, nullptr, (void **)sessionMgr.ReleaseAndAddressOf());
        if (FAILED(result)) {
            SPDLOG_TRACE(
                "'IMMDevice::Activate' IAudioSessionManager2 failed. HRESULT: {:#x}", result);
            return {};
        }

        OS::Windows::Com::UniquePtr<IAudioSessionEnumerator> sessionEnumerator;
        result = sessionMgr->GetSessionEnumerator(sessionEnumerator.ReleaseAndAddressOf());
        if (FAILED(result)) {
            SPDLOG_TRACE(
                "'IAudioSessionManager2::GetSessionEnumerator' failed. HRESULT: {:#x}", result);
            return {};
        }

        int sessionCount = 0;
        result = sessionEnumerator->GetCount(&sessionCount);
        if (FAILED(result)) {
            SPDLOG_TRACE("'IAudioSessionEnumerator::GetCount' failed. HRESULT: {:#x}", result);
            return {};
        }

        for (int i = 0; i < sessionCount; ++i) {
            SPDLOG_TRACE("Enumerating the {}th session.", i);

            OS::Windows::Com::UniquePtr<IAudioSessionControl> session;
            result = sessionEnumerator->GetSession(i, session.ReleaseAndAddressOf());
            if (FAILED(result)) {
                SPDLOG_TRACE(
                    "'IAudioSessionEnumerator::GetSession' failed. HRESULT: {:#x}", result);
                continue;
            }

            OS::Windows::Com::UniquePtr<IAudioSessionControl2> sessionEx;
            result = session->QueryInterface(
                sessionEx.GetIID(), (void **)sessionEx.ReleaseAndAddressOf());
            if (FAILED(result)) {
                SPDLOG_TRACE(
                    "'IAudioSessionControl::QueryInterface' IAudioSessionControl2 failed. "
                    "HRESULT: {:#x}",
                    result);
                continue;
            }

            uint32_t thisProcessId = 0;
            result = sessionEx->GetProcessId((DWORD *)&thisProcessId);
            if (FAILED(result)) {
                SPDLOG_TRACE(
                    "'IAudioSessionControl2::GetProcessId' failed. HRESULT: {:#x}", result);
                continue;
            }

            SPDLOG_TRACE("Get the session process id: {}", thisProcessId);

            if (_windowProcess->second != thisProcessId) {
                SPDLOG_TRACE("Process id mismatch, try next.");
                continue;
            }

            OS::Windows::Com::UniquePtr<IAudioMeterInformation> audioMeterInfo;
            result = sessionEx->QueryInterface(
                audioMeterInfo.GetIID(), (void **)audioMeterInfo.ReleaseAndAddressOf());
            if (FAILED(result)) {
                SPDLOG_TRACE(
                    "'IAudioSessionControl2::QueryInterface' IAudioMeterInformation "
                    "failed. HRESULT: {:#x}",
                    result);
                return {};
            }

            SPDLOG_TRACE("Get IAudioMeterInformation successfully.");
            return audioMeterInfo;
        }

        SPDLOG_TRACE("The desired IAudioMeterInformation not found.");
        return {};
    }

    std::optional<float> GetProcessAudioVolume() const {
        if (!_audioMeterInfo) {
            return std::nullopt;
        }

        float peak = 0.f;
        HRESULT result = _audioMeterInfo->GetPeakValue(&peak);
        if (FAILED(result)) {
            SPDLOG_TRACE("'IAudioMeterInformation::GetPeakValue' failed. HRESULT: {:#x}", result);
            return std::nullopt;
        }

        return peak;
    }

    std::optional<std::pair<HWND, uint32_t>> FindWindowAndProcess() const {
        auto processName = GetProcessName();
        auto className = GetWindowClassName();
        auto optTitleName = GetWindowTitleName();

        SPDLOG_TRACE(
            L"Try to find the media window. Process name: '{}', Class name: '{}'", processName,
            className);

        auto windowsInfo = OS::Windows::Window::FindWindowsInfo(className, optTitleName);
        if (windowsInfo.empty()) {
            SPDLOG_TRACE(L"No matching media windows found.");
            return std::nullopt;
        }

        WindowMatchingFlags windowMatchingFlags = GetWindowMatchingFlags();

        for (const auto &info : windowsInfo) {
            // SPDLOG_TRACE(
            //     L"Enumerating window. hwnd: {}, Process id: {}, Process name: {}",
            //     (void*)info.hwnd,
            //     info.processId,
            //     info.processName
            // );

            if (Utils::Text::ToLower(info.processName) != Utils::Text::ToLower(processName)) {
                // SPDLOG_TRACE(L"The media window process name mismatch.");
                continue;
            }

            if (windowMatchingFlags.testFlag(WindowMatchingFlag::HasChildren)) {
                if (GetWindow(info.hwnd, GW_CHILD) == 0) {
                    // SPDLOG_TRACE(L"The window doesn't have any child windows.");
                    continue;
                }
            }

            if (windowMatchingFlags.testFlag(WindowMatchingFlag::NonEmptyTitle)) {
                if (info.titleName.empty()) {
                    // SPDLOG_TRACE(L"The window doesn't have title name.");
                    continue;
                }
            }

            SPDLOG_TRACE(
                L"The media window is matched. Return the information. hwnd: {}, Process "
                L"id: {}, Process name: {}",
                (void *)info.hwnd, info.processId, info.processName);

            return std::make_pair(info.hwnd, info.processId);
        }

        SPDLOG_TRACE(L"The desired media window not found.");
        return std::nullopt;
    }

    Status Switch() {
        bool postDown = PostMessageW(_windowProcess->first, WM_KEYDOWN, VK_SPACE, 0) != 0;

        std::this_thread::sleep_for(50ms);

        bool postUp = PostMessageW(_windowProcess->first, WM_KEYUP, VK_SPACE, 0) != 0;

        Status status = Status::Success;

        if (!postDown || !postUp) {
            status = Status{Status::GlobalMediaVKSwitchFailed}.SetAdditionalData(postDown, postUp);

            SPDLOG_TRACE("Switch failed. Status: {}", status);
        }

        return status;
    }
};

Q_DECLARE_OPERATORS_FOR_FLAGS(MediaProgramThroughVirtualKeyAbstract::WindowMatchingFlags)

//////////////////////////////////////////////////
// Media programs
//

class UniversalSystemSession final : public MediaProgramAbstract {
public:
    UniversalSystemSession() = default;

    bool IsAvailable() override {
        try {
            _currentSession = GetCurrentSession();
        } catch (const OS::Windows::Winrt::Exception &ex) {
            SPDLOG_WARN(
                "UniversalSystemSession get current session failed. Code: {:#x}, Message: {}",
                ex.code(), winrt::to_string(ex.message()));
            return false;
        }

        if (!_currentSession.value()) {
            _currentSession.reset();
            SPDLOG_TRACE("UniversalSystemSession current session is unavailable.");
            return false;
        }

        return true;
    }

    bool IsPlaying() const override {
        return IsPlaying(_currentSession.value());
    }

    Status Play() override {
        try {
            _currentSession->TryPlayAsync().get();
            return Status::Success;
        } catch (const OS::Windows::Winrt::Exception &ex) {
            return Status{Status::GlobalMediaUSSPlayFailed}.SetAdditionalData(ex);
        }
    }

    Status Pause() override {
        try {
            _currentSession->TryPauseAsync().get();
            return Status::Success;
        } catch (const OS::Windows::Winrt::Exception &ex) {
            return Status{Status::GlobalMediaUSSPauseFailed}.SetAdditionalData(ex);
        }
    }

    std::wstring GetProgramName() const override {
        return L"UniversalSystemSession";
    }

protected:
    Priority GetPriority() const override {
        return Priority::SystemSession;
    }

private:
    std::optional<GlobalSystemMediaTransportControlsSession> _currentSession;

    static GlobalSystemMediaTransportControlsSession GetCurrentSession() {
        return GlobalSystemMediaTransportControlsSessionManager::RequestAsync()
            .get()
            .GetCurrentSession();
    }

    static auto GetSessions() {
        auto sessions =
            GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get().GetSessions();

        std::vector<GlobalSystemMediaTransportControlsSession> result;
        result.reserve(sessions.Size());

        for (size_t i = 0; i < sessions.Size(); ++i) {
            result.emplace_back(sessions.GetAt(i));
        }

        return result;
    }

    static bool IsPlaying(const GlobalSystemMediaTransportControlsSession &gsmtcs) {
        try {
            return gsmtcs.GetPlaybackInfo().PlaybackStatus() ==
                   GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        } catch (const OS::Windows::Winrt::Exception &ex) {
            SPDLOG_WARN(
                "UniversalSystemSession Get playback status failed. Code: {:#x}, Message: {}",
                ex.code(), winrt::to_string(ex.message()));
            return false;
        }
    }
};

// clang-format on

class QQMusic final : public MediaProgramThroughVirtualKeyAbstract {
public:
    std::wstring GetProgramName() const override {
        return L"QQMusic";
    }

protected:
    std::wstring GetProcessName() const override {
        return L"QQMusic.exe";
    }
    std::wstring GetWindowClassName() const override {
        return L"TXGuiFoundation";
    }
    WindowMatchingFlags GetWindowMatchingFlags() const override {
        return WindowMatchingFlag::HasChildren | WindowMatchingFlag::NonEmptyTitle;
    }
    Priority GetPriority() const override {
        return Priority::MusicPlayer;
    }
};

class NeteaseMusic final : public MediaProgramThroughVirtualKeyAbstract {
public:
    std::wstring GetProgramName() const override {
        return L"NeteaseMusic";
    }

protected:
    std::wstring GetProcessName() const override {
        return L"cloudmusic.exe";
    }
    std::wstring GetWindowClassName() const override {
        return L"MiniPlayer";
    }
    WindowMatchingFlags GetWindowMatchingFlags() const override {
        return WindowMatchingFlag::NonEmptyTitle;
    }
    Priority GetPriority() const override {
        return Priority::MusicPlayer;
    }
};

class KuGouMusic final : public MediaProgramThroughVirtualKeyAbstract {
public:
    std::wstring GetProgramName() const override {
        return L"KuGouMusic";
    }

protected:
    std::wstring GetProcessName() const override {
        return L"KuGou.exe";
    }
    std::wstring GetWindowClassName() const override {
        return L"kugou_ui";
    }
    WindowMatchingFlags GetWindowMatchingFlags() const override {
        return WindowMatchingFlag::NonEmptyTitle;
    }
    Priority GetPriority() const override {
        return Priority::MusicPlayer;
    }
};

auto GetAvailablePrograms() {
    std::vector<std::unique_ptr<MediaProgramAbstract>> result;

#define PUSH_IF_AVAILABLE(type)                                                                    \
    {                                                                                              \
        auto ptr = std::make_unique<type>();                                                       \
        if (ptr->IsAvailable()) {                                                                  \
            result.emplace_back(std::move(ptr));                                                   \
        }                                                                                          \
    }

    PUSH_IF_AVAILABLE(UniversalSystemSession);
    PUSH_IF_AVAILABLE(QQMusic);
    PUSH_IF_AVAILABLE(NeteaseMusic);
    PUSH_IF_AVAILABLE(KuGouMusic);

#undef PUSH_IF_AVAILABLE

    if (result.empty()) {
        return result;
    }

    // Sort by priority
    //
    std::sort(result.begin(), result.end(), [](const auto &first, const auto &second) {
        return first->GetPriority() < second->GetPriority();
    });

    return result;
}

//////////////////////////////////////////////////
// VolumeLevelLimiter::Callback
//

VolumeLevelLimiter::Callback::Callback(std::function<bool(uint32_t)> volumeLevelSetter)
    : _volumeLevelSetter{std::move(volumeLevelSetter)} {}

ULONG STDMETHODCALLTYPE VolumeLevelLimiter::Callback::AddRef() {
    return ++_ref;
}

ULONG STDMETHODCALLTYPE VolumeLevelLimiter::Callback::Release() {
    auto ref = --_ref;
    if (ref == 0) {
        delete this;
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE
VolumeLevelLimiter::Callback::QueryInterface(REFIID riid, void **ppvInterface) {
    if (IID_IUnknown == riid) {
        AddRef();
        *ppvInterface = (IUnknown *)this;
    } else if (__uuidof(IAudioEndpointVolumeCallback) == riid) {
        AddRef();
        *ppvInterface = (IAudioEndpointVolumeCallback *)this;
    } else {
        *ppvInterface = nullptr;
        return E_NOINTERFACE;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
VolumeLevelLimiter::Callback::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    SPDLOG_TRACE("VolumeLevelLimiter::Callback::OnNotify() called.");

    if (pNotify == nullptr) {
        SPDLOG_WARN("VolumeLevelLimiter::Callback::OnNotify(): pNotify == nullptr");
        return E_INVALIDARG;
    }

    do {
        auto optVolumeLevel = _volumeLevel.load();

        if (!optVolumeLevel.has_value()) {
            SPDLOG_TRACE("VolumeLevelLimiter::Callback::OnNotify(): Volume level unlimited.");
            break;
        }

        if (pNotify->fMasterVolume > optVolumeLevel.value() / 100.f) {
            SPDLOG_TRACE(
                "VolumeLevelLimiter::Callback::OnNotify(): Exceeded the limit, reduce. "
                "('{}', '{}')",
                pNotify->fMasterVolume, optVolumeLevel.value());
            _volumeLevelSetter(optVolumeLevel.value());
        } else {
            SPDLOG_TRACE(
                "VolumeLevelLimiter::Callback::OnNotify(): Not exceeded the limit. ('{}', '{}')",
                pNotify->fMasterVolume, optVolumeLevel.value());
        }

    } while (false);

    return S_OK;
}

void VolumeLevelLimiter::Callback::SetMaxValue(std::optional<uint32_t> volumeLevel) {
    if (volumeLevel.has_value()) {
        volumeLevel = std::min<uint32_t>(volumeLevel.value(), 100);
    }
    _volumeLevel = std::move(volumeLevel);
}

//////////////////////////////////////////////////
// VolumeLevelLimiter
//

VolumeLevelLimiter::VolumeLevelLimiter()
    : _callback{[this](uint32_t volumeLevel) { return SetVolumeLevel(volumeLevel); }} {
    Initialize();
}

VolumeLevelLimiter::~VolumeLevelLimiter() {
    if (_endpointVolume) {
        _endpointVolume->UnregisterControlChangeNotify(&_callback);
    }
}

void VolumeLevelLimiter::SetMaxValue(std::optional<uint32_t> volumeLevel) {
    SPDLOG_INFO(
        "VolumeLevelLimiter::SetMaxValue() value: {}",
        volumeLevel.has_value() ? std::to_string(volumeLevel.value()) : "nullopt");

    if (volumeLevel.has_value()) {
        auto optCurrent = GetVolumeLevel();

        // Reduce the current volume level if it's exceeded the limit
        if (optCurrent.has_value() && optCurrent.value() > volumeLevel.value()) {
            SetVolumeLevel(volumeLevel.value());
        }
    }

    _callback.SetMaxValue(std::move(volumeLevel));
}

std::optional<uint32_t> VolumeLevelLimiter::GetVolumeLevel() const {
    float value = 0.f;
    HRESULT result = _endpointVolume->GetMasterVolumeLevelScalar(&value);
    if (FAILED(result)) {
        SPDLOG_TRACE(
            "'IAudioEndpointVolume::GetMasterVolumeLevelScalar' failed. HRESULT: {:#x}", result);
        return std::nullopt;
    }

    uint32_t return_value = (uint32_t)(value * 100.f);
    SPDLOG_TRACE("GetVolumeLevel() returns '{}'", return_value);

    return return_value;
}

bool VolumeLevelLimiter::SetVolumeLevel(uint32_t volumeLevel) const {
    SPDLOG_TRACE("SetVolumeLevel() '{}'", volumeLevel);

    HRESULT result = _endpointVolume->SetMasterVolumeLevelScalar(volumeLevel / 100.f, nullptr);
    if (FAILED(result)) {
        SPDLOG_TRACE(
            "'IAudioEndpointVolume::SetMasterVolumeLevelScalar' failed. HRESULT: {:#x}", result);
        return false;
    }
    return true;
}

// TODO: * Bind to the exact AirPods playback device insteam of default playback device
//       * Allow rebind if device unavailable
//
bool VolumeLevelLimiter::Initialize() {
    SPDLOG_INFO("VolumeLevelLimiter initializing.");

    OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> deviceEnumerator;
    HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, deviceEnumerator.GetIID(),
        (void **)deviceEnumerator.ReleaseAndAddressOf());
    if (FAILED(result)) {
        SPDLOG_WARN("Create COM instance 'IMMDeviceEnumerator' failed. HRESULT: {:#x}", result);
        return false;
    }

    OS::Windows::Com::UniquePtr<IMMDevice> audioEndpoint;
    result = deviceEnumerator->GetDefaultAudioEndpoint(
        eRender, eConsole, audioEndpoint.ReleaseAndAddressOf());
    if (FAILED(result)) {
        SPDLOG_WARN("'GetDefaultAudioEndpoint' failed. HRESULT: {:#x}", result);
        return false;
    }

    result = audioEndpoint->Activate(
        _endpointVolume.GetIID(), CLSCTX_ALL, nullptr,
        (void **)_endpointVolume.ReleaseAndAddressOf());
    if (FAILED(result)) {
        SPDLOG_WARN("'IMMDevice::Activate' IAudioEndpointVolume failed. HRESULT: {:#x}", result);
        return false;
    }

    result = _endpointVolume->RegisterControlChangeNotify(&_callback);
    if (FAILED(result)) {
        SPDLOG_WARN(
            "IAudioEndpointVolume::RegisterControlChangeNotify failed. HRESULT: {:#x}", result);
        return false;
    }

    SPDLOG_INFO("VolumeLevelLimiter initialization succeeded.");
    return true;
}
} // namespace Details

bool Initialize() {
    return OS::Windows::Winrt::Initialize();
}

void Controller::Play() {
    std::lock_guard<std::mutex> lock{_mutex};

    if (_pausedPrograms.empty()) {
        SPDLOG_TRACE(L"Paused programs vector is empty.");
        return;
    }

    for (const auto &program : _pausedPrograms) {
        Status status = program->Play();

        if (status.IsFailed()) {
            SPDLOG_WARN(
                L"Failed to play media. Program name: {}, Status: {}", program->GetProgramName(),
                status);
        } else {
            SPDLOG_TRACE(L"Media played. Program name: {}", program->GetProgramName());
        }
    }

    _pausedPrograms.clear();
}

void Controller::Pause() {
    std::lock_guard<std::mutex> lock{_mutex};

    auto programs = Details::GetAvailablePrograms();

    for (auto &&program : programs) {
        if (program->IsPlaying()) {
            Status status = program->Pause();
            if (status.IsFailed()) {
                SPDLOG_WARN(
                    L"Failed to pause media. Program name: {}, Status: {}",
                    program->GetProgramName(), status);
            } else {
                SPDLOG_TRACE(L"Media paused. Program name: {}", program->GetProgramName());
                _pausedPrograms.emplace_back(std::move(program));
            }
        }
    }
}

void Controller::LimitVolume(std::optional<uint32_t> volumeLevel) {
    _volumeLevelLimiter.SetMaxValue(std::move(volumeLevel));
}
} // namespace Core::GlobalMedia
