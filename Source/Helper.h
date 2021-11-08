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

#include <mutex>
#include <vector>
#include <chrono>
#include <thread>
#include <future>
#include <functional>
#include <condition_variable>

#include <QString>

#define __TO_STRING(expr) #expr
#define TO_STRING(expr) __TO_STRING(expr)

namespace Helper {

template <class E>
[[nodiscard]] constexpr decltype(auto) ToUnderlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

//////////////////////////////////////////////////

template <class T>
[[nodiscard]] constexpr decltype(auto) Hash(const T &value) noexcept
{
    return std::hash<T>{}(value);
}

//////////////////////////////////////////////////

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

//////////////////////////////////////////////////

class NonCopyable
{
protected:
    NonCopyable() = default;

    NonCopyable(const NonCopyable &) = delete;
    NonCopyable &operator=(const NonCopyable &) = delete;
};

//////////////////////////////////////////////////

template <class D>
class Singleton : Helper::NonCopyable
{
public:
    static D &GetInstance()
    {
        static D i;
        return i;
    }

protected:
    Singleton() = default;
};

#define SINGLETON_EXPOSE_FUNCTION(class_name, function)                                            \
    template <class... Args>                                                                       \
    inline decltype(auto) function(Args &&...args)                                                 \
    {                                                                                              \
        return class_name::GetInstance().function(std::forward<Args>(args)...);                    \
    }

//////////////////////////////////////////////////

namespace Impl {
template <class T>
class MemberPointerType
{
private:
    template <class ClassT, class MemberT>
    static MemberT ExtractType(MemberT ClassT::*);

public:
    using Type = decltype(ExtractType(static_cast<T>(nullptr)));
};
} // namespace Impl

template <class T>
using MemberPointerType = typename Impl::MemberPointerType<T>::Type;

//////////////////////////////////////////////////

template <class T>
inline bool IsFutureReady(const std::future<T> &future)
{
    return future.wait_for(0s) == std::future_status::ready;
}

//////////////////////////////////////////////////

template <class T>
QString ToString(const T &value);

template <>
inline QString ToString<std::vector<uint8_t>>(const std::vector<uint8_t> &value)
{
    QString result;

    size_t bytesSize = value.size();

    for (size_t i = 0; i < bytesSize; ++i) {
        result += QString::number(value.at(i), 16).rightJustified(2, '0');

        if (i + 1 != bytesSize) {
            result += ' ';
        }
    }

    return result;
}

template <>
inline QString ToString<Qt::ApplicationState>(const Qt::ApplicationState &value)
{
    switch (value) {
    case Qt::ApplicationState::ApplicationSuspended:
        return "Qt::ApplicationState::ApplicationSuspended";
    case Qt::ApplicationState::ApplicationHidden:
        return "Qt::ApplicationState::ApplicationHidden";
    case Qt::ApplicationState::ApplicationInactive:
        return "Qt::ApplicationState::ApplicationInactive";
    case Qt::ApplicationState::ApplicationActive:
        return "Qt::ApplicationState::ApplicationActive";
    default:
        return QString{"Unhandled 'Qt::ApplicationState' value: '%1'"}.arg(value);
    }
}

//////////////////////////////////////////////////

template <class T>
struct Sides {
    using Type = T;

    T left, right;
};

//////////////////////////////////////////////////

using CbHandle = uint64_t;

template <class Function>
class Callback
{
public:
    inline CbHandle Register(Function &&callback)
    {
        std::lock_guard<std::mutex> lock{_mutex};

        auto thisHandle = _nextHandle++;
        _callbacks.emplace_back(thisHandle, std::move(callback));
        return thisHandle;
    }

    inline bool Unregister(CbHandle handle)
    {
        std::lock_guard<std::mutex> lock{_mutex};

        auto iter =
            std::find_if(_callbacks.begin(), _callbacks.end(), [handle](const auto &callbackInfo) {
                return callbackInfo.first == handle;
            });

        if (iter == _callbacks.end()) {
            return false;
        }

        _callbacks.erase(iter);
        return true;
    }

    inline void UnregisterAll()
    {
        std::lock_guard<std::mutex> lock{_mutex};

        _callbacks.clear();
    }

    template <class... Args>
    inline void Invoke(Args &&...args) const
    {
        std::lock_guard<std::mutex> lock{_mutex};

        for (const auto &callbackInfo : _callbacks) {
            callbackInfo.second(args...);
        }
    }

    inline Callback &operator+=(Function &&callback)
    {
        Register(std::move(callback));
        return *this;
    }

private:
    mutable std::mutex _mutex;
    CbHandle _nextHandle{1};
    std::vector<std::pair<CbHandle, Function>> _callbacks;
};

class ConWorker
{
public:
    using FnCallback = std::function<bool()>;

    ConWorker() = default;

    inline ConWorker(std::chrono::milliseconds interval, FnCallback callback)
    {
        Start(std::move(interval), std::move(callback));
    }

    inline ~ConWorker()
    {
        Stop();
    }

    inline void Start(std::chrono::milliseconds interval, FnCallback callback)
    {
        Stop();
        _interval = std::move(interval);
        _callback = std::move(callback);
        _destroyFlag = false;
        _thread = std::thread{[this] { Thread(); }};
    }

    inline void Stop()
    {
        _destroyFlag = true;
        Notify();
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    inline void Notify()
    {
        _destroyConVar.notify_all();
    }

private:
    std::chrono::milliseconds _interval{};
    FnCallback _callback;
    std::thread _thread;
    std::mutex _mutex;
    std::condition_variable _destroyConVar;
    std::atomic<bool> _destroyFlag{false};

    void Thread()
    {
        while (!_destroyFlag) {
            if (!_callback()) {
                break;
            }
            std::unique_lock<std::mutex> lock{_mutex};
            _destroyConVar.wait_for(lock, _interval);
        }
    }
};

class Timer
{
public:
    Timer() = default;

    template <class... Args>
    inline Timer(Args &&...args)
    {
        Start(std::forward<Args>(args)...);
    }

    inline ~Timer()
    {
        Stop();
    }

    inline void Start(std::chrono::milliseconds interval, std::function<void()> callback)
    {
        Stop();
        _destroyFlag = false;
        _interval = std::move(interval);
        Reset();
        _thread = std::thread{&Timer::Thread, this, std::move(callback)};
    }

    inline void Stop()
    {
        _destroyFlag = true;
        _destroyConVar.notify_all();
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    inline void Reset()
    {
        _deadline = Clock::now() + _interval.load();
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::atomic<bool> _destroyFlag{false};
    std::mutex _mutex;
    std::condition_variable _destroyConVar;
    std::atomic<std::chrono::milliseconds> _interval;
    std::atomic<TimePoint> _deadline;
    std::thread _thread;

    inline void Thread(std::function<void()> callback)
    {
        while (true) {
            std::unique_lock<std::mutex> lock{_mutex};
            {
                _destroyConVar.wait_until(lock, _deadline.load());
            }
            lock.unlock();

            if (_destroyFlag) {
                break;
            }
            if (_deadline.load() > Clock::now()) {
                continue;
            }
            Reset();
            callback();
        }
    }
};
} // namespace Helper
