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

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include <spdlog/spdlog.h>

#include "../Utils.h"
#include "OS/Windows.h"


namespace Core::GlobalMedia
{
    using namespace std::chrono_literals;
    using namespace winrt::Windows::Media::Control;

    namespace Details
    {
        class MediaProgramThroughVirtualKeyAbstract : public MediaProgramAbstract
        {
        public:
            enum WindowMatchingFlag : uint32_t {
                HasChildren = 0b00000001,
                NonEmptyTitle = 0b00000010,
            };
            Q_DECLARE_FLAGS(WindowMatchingFlags, WindowMatchingFlag)

            virtual ~MediaProgramThroughVirtualKeyAbstract() = 0;

            bool IsAvailable() override
            {
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

            Status Play() override
            {
                spdlog::trace("Do play.");

                if (IsPlaying()) {
                    spdlog::trace("The media program is already playing.");
                    return Status::GlobalMediaPlayAlreadyPlaying;
                }
                return Switch();
            }

            Status Pause() override
            {
                spdlog::trace("Do pause.");

                if (!IsPlaying()) {
                    spdlog::trace("The media program nothing is playingg.");
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

            OS::Windows::Com::UniquePtr<IAudioMeterInformation>
                GetProcessAudioMeterInfo()
            {
                spdlog::trace("Try to get IAudioMeterInformation of this process.");

                OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> deviceEnumerator;
                HRESULT result = CoCreateInstance(
                    __uuidof(MMDeviceEnumerator),
                    nullptr,
                    CLSCTX_ALL,
                    deviceEnumerator.GetIID(),
                    (void**)deviceEnumerator.ReleaseAndAddressOf()
                );
                if (FAILED(result)) {
                    spdlog::trace(
                        "Create COM instance 'IMMDeviceEnumerator' failed. HRESULT: {:#x}",
                        result
                    );
                    return {};
                }

                OS::Windows::Com::UniquePtr<IMMDevice> audioEndpoint;
                result = deviceEnumerator->GetDefaultAudioEndpoint(
                    eRender,
                    eMultimedia,
                    audioEndpoint.ReleaseAndAddressOf()
                );
                if (FAILED(result)) {
                    spdlog::trace("'GetDefaultAudioEndpoint' failed. HRESULT: {:#x}", result);
                    return {};
                }

                OS::Windows::Com::UniquePtr<IAudioSessionManager2> sessionMgr;
                result = audioEndpoint->Activate(
                    sessionMgr.GetIID(),
                    CLSCTX_ALL,
                    nullptr,
                    (void**)sessionMgr.ReleaseAndAddressOf()
                );
                if (FAILED(result)) {
                    spdlog::trace(
                        "'IMMDevice::Activate' IAudioSessionManager2 failed. HRESULT: {:#x}",
                        result
                    );
                    return {};
                }

                OS::Windows::Com::UniquePtr<IAudioSessionEnumerator> sessionEnumerator;
                result = sessionMgr->GetSessionEnumerator(sessionEnumerator.ReleaseAndAddressOf());
                if (FAILED(result)) {
                    spdlog::trace(
                        "'IAudioSessionManager2::GetSessionEnumerator' failed. HRESULT: {:#x}",
                        result
                    );
                    return {};
                }

                int sessionCount = 0;
                result = sessionEnumerator->GetCount(&sessionCount);
                if (FAILED(result)) {
                    spdlog::trace(
                        "'IAudioSessionEnumerator::GetCount' failed. HRESULT: {:#x}",
                        result
                    );
                    return {};
                }

                for (int i = 0; i < sessionCount; ++i)
                {
                    spdlog::trace("Enumerating the {}th session.", i);

                    OS::Windows::Com::UniquePtr<IAudioSessionControl> session;
                    result = sessionEnumerator->GetSession(i, session.ReleaseAndAddressOf());
                    if (FAILED(result)) {
                        spdlog::trace(
                            "'IAudioSessionEnumerator::GetSession' failed. HRESULT: {:#x}",
                            result
                        );
                        continue;
                    }

                    OS::Windows::Com::UniquePtr<IAudioSessionControl2> sessionEx;
                    result = session->QueryInterface(
                        sessionEx.GetIID(),
                        (void**)sessionEx.ReleaseAndAddressOf()
                    );
                    if (FAILED(result)) {
                        spdlog::trace(
                            "'IAudioSessionControl::QueryInterface' IAudioSessionControl2 failed. HRESULT: {:#x}",
                            result
                        );
                        continue;
                    }

                    uint32_t thisProcessId = 0;
                    result = sessionEx->GetProcessId((DWORD*)&thisProcessId);
                    if (FAILED(result)) {
                        spdlog::trace(
                            "'IAudioSessionControl2::GetProcessId' failed. HRESULT: {:#x}",
                            result
                        );
                        continue;
                    }

                    spdlog::trace("Get the session process id: {}", thisProcessId);

                    if (_windowProcess->second != thisProcessId) {
                        spdlog::trace("Process id mismatch, try next.");
                        continue;
                    }

                    OS::Windows::Com::UniquePtr<IAudioMeterInformation> audioMeterInfo;
                    result = sessionEx->QueryInterface(
                        audioMeterInfo.GetIID(),
                        (void**)audioMeterInfo.ReleaseAndAddressOf()
                    );
                    if (FAILED(result)) {
                        spdlog::trace(
                            "'IAudioSessionControl2::QueryInterface' IAudioMeterInformation failed. HRESULT: {:#x}",
                            result
                        );
                        return {};
                    }

                    spdlog::trace("Get IAudioMeterInformation successfully.");
                    return audioMeterInfo;
                }

                spdlog::trace("The desired IAudioMeterInformation not found.");
                return {};
            }

            std::optional<float> GetProcessAudioVolume() const
            {
                if (!_audioMeterInfo) {
                    return std::nullopt;
                }

                float peak = 0.f;
                HRESULT result = _audioMeterInfo->GetPeakValue(&peak);
                if (FAILED(result)) {
                    spdlog::trace(
                        "'IAudioMeterInformation::GetPeakValue' failed. HRESULT: {:#x}",
                        result
                    );
                    return std::nullopt;
                }

                return peak;
            }

            std::optional<std::pair<HWND, uint32_t>> FindWindowAndProcess() const
            {
                auto processName = GetProcessName();
                auto className = GetWindowClassName();
                auto optTitleName = GetWindowTitleName();

                spdlog::trace(
                    L"Try to find the media window. Process name: '{}', Class name: '{}'",
                    processName,
                    className
                );

                auto windowsInfo = OS::Windows::Window::FindWindowsInfo(className, optTitleName);
                if (windowsInfo.empty()) {
                    spdlog::trace(L"No matching media windows found.");
                    return std::nullopt;
                }

                WindowMatchingFlags windowMatchingFlags = GetWindowMatchingFlags();

                for (const auto &info : windowsInfo)
                {
                    // spdlog::trace(
                    //     L"Enumerating window. hwnd: {}, Process id: {}, Process name: {}",
                    //     (void*)info.hwnd,
                    //     info.processId,
                    //     info.processName
                    // );

                    if (Utils::Text::ToLower(info.processName) !=
                        Utils::Text::ToLower(processName))
                    {
                        // spdlog::trace(L"The media window process name mismatch.");
                        continue;
                    }

                    
                    if (windowMatchingFlags.testFlag(WindowMatchingFlag::HasChildren)) {
                        if (GetWindow(info.hwnd, GW_CHILD) == 0) {
                            // spdlog::trace(L"The window doesn't have any child windows.");
                            continue;
                        }
                    }

                    if (windowMatchingFlags.testFlag(WindowMatchingFlag::NonEmptyTitle)) {
                        if (info.titleName.empty()) {
                            // spdlog::trace(L"The window doesn't have title name.");
                            continue;
                        }
                    }

                    spdlog::trace(
                        L"The media window is matched. Return the information. hwnd: {}, Process id: {}, Process name: {}",
                        (void*)info.hwnd,
                        info.processId,
                        info.processName
                    );

                    return std::make_pair(info.hwnd, info.processId);
                }

                spdlog::trace(L"The desired media window not found.");
                return std::nullopt;
            }

            Status Switch()
            {
                bool postDown = PostMessageW(_windowProcess->first, WM_KEYDOWN, VK_SPACE, 0) != 0;

                std::this_thread::sleep_for(50ms);

                bool postUp = PostMessageW(_windowProcess->first, WM_KEYUP, VK_SPACE, 0) != 0;

                Status status = Status::Success;

                if (!postDown || !postUp)
                {
                    status = Status{
                        Status::GlobalMediaVKSwitchFailed
                    }.SetAdditionalData(postDown, postUp);

                    spdlog::trace("Switch failed. Status: {}", status);
                }

                return status;
            }
        };
        MediaProgramThroughVirtualKeyAbstract::~MediaProgramThroughVirtualKeyAbstract() {}

        Q_DECLARE_OPERATORS_FOR_FLAGS(MediaProgramThroughVirtualKeyAbstract::WindowMatchingFlags)

        //////////////////////////////////////////////////
        // Media programs
        //

        class UniversalSystemSession final : public MediaProgramAbstract
        {
        public:
            UniversalSystemSession() = default;

            bool IsAvailable() override
            {
                WINRT_TRY {
                    _currentSession = GetCurrentSession();
                }
                WINRT_CATCH(exception) {
                    spdlog::warn(
                        "UniversalSystemSession get current session failed. Code: {:#x}, Message: {}",
                        exception.code(), winrt::to_string(exception.message())
                    );
                    return false;
                }

                if (!_currentSession.value()) {
                    _currentSession.reset();
                    spdlog::trace("UniversalSystemSession current session is unavailable.");
                    return false;
                }

                return true;
            }

            bool IsPlaying() const override {
                return IsPlaying(_currentSession.value());
            }

            Status Play() override
            {
                WINRT_TRY {
                    _currentSession->TryPlayAsync().get();
                    return Status::Success;
                }
                WINRT_CATCH_RETURN_STATUS(Status::GlobalMediaUSSPlayFailed)
            }

            Status Pause() override
            {
                WINRT_TRY {
                    _currentSession->TryPauseAsync().get();
                    return Status::Success;
                }
                WINRT_CATCH_RETURN_STATUS(Status::GlobalMediaUSSPauseFailed)
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

            static auto GetSessions()
            {
                auto sessions = GlobalSystemMediaTransportControlsSessionManager::RequestAsync()
                    .get().GetSessions();

                std::vector<GlobalSystemMediaTransportControlsSession> result;
                result.reserve(sessions.Size());

                for (size_t i = 0; i < sessions.Size(); ++i) {
                    result.emplace_back(sessions.GetAt(i));
                }

                return result;
            }

            static bool IsPlaying(const GlobalSystemMediaTransportControlsSession &gsmtcs)
            {
                WINRT_TRY {
                    return gsmtcs.GetPlaybackInfo().PlaybackStatus() ==
                        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                }
                WINRT_CATCH(exception) {
                    spdlog::warn(
                        "UniversalSystemSession Get playback status failed. Code: {:#x}, Message: {}",
                        exception.code(), winrt::to_string(exception.message())
                    );
                    return false;
                }
            }
        };

        class QQMusic final : public MediaProgramThroughVirtualKeyAbstract
        {
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

        class NeteaseMusic final : public MediaProgramThroughVirtualKeyAbstract
        {
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

        class KuGouMusic final : public MediaProgramThroughVirtualKeyAbstract
        {
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

        auto GetAvailablePrograms()
        {
            std::vector<std::unique_ptr<MediaProgramAbstract>> result;

#define PUSH_IF_AVAILABLE(type) {                           \
                auto ptr = std::make_unique<type>();        \
                if (ptr->IsAvailable()) {                   \
                    result.emplace_back(std::move(ptr));    \
                }                                           \
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
            std::sort(
                result.begin(), result.end(),
                [](const auto &first, const auto &second) {
                    return first->GetPriority() < second->GetPriority();
                }
            );

            return result;
        }

    } // namespace Details


    bool Initialize()
    {
        return OS::Windows::Winrt::Initialize();
    }

    void Controller::Play()
    {
        std::lock_guard<std::mutex> lock{_mutex};

        if (_pausedPrograms.empty()) {
            spdlog::trace(L"Paused programs vector is empty.");
            return;
        }

        for (const auto &program : _pausedPrograms)
        {
            Status status = program->Play();

            if (status.IsFailed()) {
                spdlog::warn(
                    L"Failed to play media. Program name: {}, Status: {}",
                    program->GetProgramName(),
                    status
                );
            }
            else {
                spdlog::trace(
                    L"Media played. Program name: {}",
                    program->GetProgramName()
                );
            }
        }

        _pausedPrograms.clear();
    }

    void Controller::Pause()
    {
        std::lock_guard<std::mutex> lock{_mutex};

        auto programs = Details::GetAvailablePrograms();

        for (auto &&program : programs)
        {
            if (program->IsPlaying())
            {
                Status status = program->Pause();
                if (status.IsFailed()) {
                    spdlog::warn(
                        L"Failed to pause media. Program name: {}, Status: {}",
                        program->GetProgramName(),
                        status
                    );
                }
                else {
                    spdlog::trace(
                        L"Media paused. Program name: {}",
                        program->GetProgramName()
                    );
                    _pausedPrograms.emplace_back(std::move(program));
                }
            }
        }
    }

} // namespace Core::GlobalMedia
