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

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <tlhelp32.h>
#include <winrt/Windows.Foundation.h>
#include <QString>
#include <spdlog/spdlog.h>

#include "../../Assert.h"

#pragma comment(lib, "WindowsApp.lib")

#define WINRT_TRY                   try
#define WINRT_CATCH(exception)      catch (const winrt::hresult_error &exception)
#define WINRT_CATCH_RETURN(value)   WINRT_CATCH(exception) {    \
                                        return value;           \
                                    }
#define WINRT_CATCH_RETURN_STATUS(status) \
                                    WINRT_CATCH_RETURN(Status{status}.SetAdditionalData(exception))


namespace Core::OS::Windows
{
    namespace Process
    {
        inline std::optional<std::wstring> GetNameById(uint32_t targetId)
        {
            std::optional<std::wstring> result;

            HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapshot == INVALID_HANDLE_VALUE) {
                return result;
            }

            PROCESSENTRY32W processEntry{};
            processEntry.dwSize = sizeof(PROCESSENTRY32W);

            if (Process32FirstW(hSnapshot, &processEntry)) {
                do {
                    if (processEntry.th32ProcessID == targetId) {
                        result = processEntry.szExeFile;
                        break;
                    }
                } while (Process32NextW(hSnapshot, &processEntry));
            }

            CloseHandle(hSnapshot);
            return result;
        }

    } // namespace Process

    namespace Window
    {
        struct WindowsInfo
        {
            HWND hwnd{};
            std::wstring className;
            std::wstring titleName;
            uint32_t threadId{};
            uint32_t processId{};
            std::wstring processName;
        };

        inline std::vector<WindowsInfo> FindWindowsInfo(
            const std::wstring &className,
            const std::optional<std::wstring> &optTitleName
        ) {
            std::vector<WindowsInfo> result;
            HWND lastHwnd = nullptr;

            while (true)
            {
                lastHwnd = FindWindowExW(
                    nullptr,
                    lastHwnd,
                    className.c_str(),
                    optTitleName.has_value() ? optTitleName->c_str() : nullptr
                );
                if (lastHwnd == nullptr) {
                    break;
                }

                WindowsInfo info;
                info.hwnd = lastHwnd;

                wchar_t className[256]{}, titleName[256]{};
                if (GetClassNameW(lastHwnd, className, 256) == 0) {
                    continue;
                }
                if (GetWindowTextW(lastHwnd, titleName, 256) == 0) {
                    if (GetLastError() != 0) {
                        continue;
                    }
                }

                info.className = className;
                info.titleName = titleName;

                info.threadId = GetWindowThreadProcessId(lastHwnd, (DWORD*)&info.processId);
                if (info.threadId == 0 || info.processId == 0) {
                    continue;
                }

                auto optWindowProcessName = Process::GetNameById(info.processId);
                if (!optWindowProcessName.has_value()) {
                    continue;
                }
                info.processName = std::move(optWindowProcessName.value());

                result.emplace_back(std::move(info));
            }

            return result;
        }

    } // namespace Window

    namespace Winrt
    {
        inline bool Initialize()
        {
            static bool inited = false;
            if (inited) {
                return true;
            }
            inited = true;

            WINRT_TRY {
                winrt::init_apartment();
                return true;
            }
            WINRT_CATCH(exception)
            {
                spdlog::warn(
                    "Winrt initialize failed. Code: {:#x}, Message: {}",
                    exception.code(),
                    winrt::to_string(exception.message())
                );

                // return false;
                return true; // workaround for "Cannot change thread mode after it is set."
            }
        }

    } // namespace Winrt

    namespace Com
    {
        template <class T>
        constexpr inline bool is_com_type_v = std::is_base_of_v<IUnknown, T>;

        template <class T, std::enable_if_t<is_com_type_v<T>, int> = 0>
        class UniquePtr : Helper::NonCopyable
        {
        public:
            UniquePtr() = default;

            inline UniquePtr(UniquePtr &&rhs) noexcept {
                MoveFrom(std::move(rhs));
            }

            inline UniquePtr(T *purePtr) noexcept {
                CopyFrom(purePtr);
            }

            inline ~UniquePtr() {
                Release();
            }

            inline UniquePtr& operator=(UniquePtr &&rhs) noexcept {
                Release();
                MoveFrom(std::move(rhs));
                return *this;
            }

            inline UniquePtr& operator=(T *purePtr) noexcept {
                Release();
                CopyFrom(purePtr);
                return *this;
            }

            inline explicit operator bool() const noexcept {
                return _ptr != nullptr;
            }

            inline T* operator->() const noexcept {
                APD_ASSERT(_ptr != nullptr);
                return _ptr;
            }

            inline T** ReleaseAndAddressOf() noexcept {
                Release();
                return &_ptr;
            }

            inline constexpr static auto GetIID() {
                return __uuidof(T);
            }

        private:
            T *_ptr{nullptr};

            inline void Release() noexcept
            {
                if (_ptr != nullptr) {
                    _ptr->Release();
                    _ptr = nullptr;
                }
            }

            inline void CopyFrom(T *purePtr) noexcept {
                _ptr = purePtr;
            }

            inline void MoveFrom(UniquePtr &&rhs) noexcept {
                _ptr = rhs._ptr;
                rhs._ptr = nullptr;
            }
        };

    } // namespace Com

} // namespace Core::OS::Windows

namespace Helper
{
    template <>
    inline QString ToString(const winrt::hstring &value)
    {
        return QString::fromStdString(winrt::to_string(value));
    }

    template <>
    inline QString ToString(const winrt::hresult_error &value)
    {
        return QString{"0x%1 (%2)"}
            .arg(QString::number(value.code(), 16))
            .arg(ToString(value.message()));
    }

} // namespace Helper
