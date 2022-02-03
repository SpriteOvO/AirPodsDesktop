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

#include <iostream>

#include <Windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>

#include <QDir>
#include <QString>

#include "../../Logger.h"
#include "../../Assert.h"

#pragma comment(lib, "WindowsApp.lib")

namespace Core::OS::Windows {
namespace Process {

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

inline void AttachConsole()
{
    if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
#if defined APD_DEBUG
        std::cout << "Not attached to the cosole of the parent process" << std::endl;
#endif
        return;
    }

    FILE *_;
    freopen_s(&_, "CONOUT$", "w", stdout);
    freopen_s(&_, "CONOUT$", "w", stderr);

#if defined APD_DEBUG
    std::cout << "Attached to the cosole of the parent process" << std::endl;
#endif
}

} // namespace Process

namespace Window {

struct WindowsInfo {
    HWND hwnd{};
    std::wstring className;
    std::wstring titleName;
    uint32_t threadId{};
    uint32_t processId{};
    std::wstring processName;
};

inline std::vector<WindowsInfo>
FindWindowsInfo(const std::wstring &className, const std::optional<std::wstring> &optTitleName)
{
    std::vector<WindowsInfo> result;
    HWND lastHwnd = nullptr;

    while (true) {
        lastHwnd = FindWindowExW(
            nullptr, lastHwnd, className.c_str(),
            optTitleName.has_value() ? optTitleName->c_str() : nullptr);
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

        info.threadId = GetWindowThreadProcessId(lastHwnd, (DWORD *)&info.processId);
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

namespace File {

inline bool OpenFileLocation(const QDir &directory)
{
    QString arguments = "/select,\"" + QDir::toNativeSeparators(directory.absolutePath()) + "\"";

    return (uintptr_t)ShellExecuteW(
               nullptr, nullptr, L"explorer.exe", arguments.toStdWString().c_str(), nullptr,
               SW_SHOWDEFAULT) > 32;
}
} // namespace File

namespace Winrt {

using Exception = winrt::hresult_error;

inline void Initialize()
{
    static std::once_flag onceFlag;

    std::call_once(onceFlag, []() {
        try {
            winrt::init_apartment();
        }
        catch (const Exception &ex) {
            LOG(Warn, "Winrt initialize failed. Code: {:#x}, Message: {}", ex.code(),
                winrt::to_string(ex.message()));
        }
    });
}
} // namespace Winrt

namespace Com {

template <class T>
concept KindOfComType = std::derived_from<T, IUnknown>;

template <KindOfComType T>
class UniquePtr : Helper::NonCopyable
{
public:
    UniquePtr() = default;

    inline UniquePtr(UniquePtr &&rhs) noexcept
    {
        MoveFrom(std::move(rhs));
    }

    inline UniquePtr(T *purePtr) noexcept
    {
        CopyFrom(purePtr);
    }

    inline ~UniquePtr()
    {
        Release();
    }

    inline UniquePtr &operator=(UniquePtr &&rhs) noexcept
    {
        Release();
        MoveFrom(std::move(rhs));
        return *this;
    }

    inline UniquePtr &operator=(T *purePtr) noexcept
    {
        Release();
        CopyFrom(purePtr);
        return *this;
    }

    inline explicit operator bool() const noexcept
    {
        return _ptr != nullptr;
    }

    inline T *operator->() const noexcept
    {
        APD_ASSERT(_ptr != nullptr);
        return _ptr;
    }

    inline T **ReleaseAndAddressOf() noexcept
    {
        Release();
        return &_ptr;
    }

    inline constexpr static auto GetIID()
    {
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

    inline void CopyFrom(T *purePtr) noexcept
    {
        _ptr = purePtr;
    }

    inline void MoveFrom(UniquePtr &&rhs) noexcept
    {
        _ptr = rhs._ptr;
        rhs._ptr = nullptr;
    }
};
} // namespace Com
} // namespace Core::OS::Windows

namespace Helper {

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
