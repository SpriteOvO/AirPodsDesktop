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
#include <functional>
#include <condition_variable>
#include <QString>

#define __TO_STRING(expr) #expr
#define TO_STRING(expr) __TO_STRING(expr)

namespace Helper {

template <class T>
inline constexpr bool is_string_v =
    std::is_same_v<T, std::string> || std::is_same_v<T, std::wstring>;

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

class NonCopyable {
protected:
    NonCopyable() = default;

    NonCopyable(const NonCopyable &) = delete;
    NonCopyable &operator=(const NonCopyable &) = delete;
};

template <class T>
QString ToString(const T &value);

template <>
inline QString ToString<std::vector<uint8_t>>(const std::vector<uint8_t> &value) {
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

using CbHandle = uint64_t;

template <class Function>
class Callback {
public:
    inline CbHandle Register(Function &&callback) {
        std::lock_guard<std::mutex> lock{_mutex};

        auto thisHandle = _nextHandle++;
        _callbacks.emplace_back(thisHandle, std::move(callback));
        return thisHandle;
    }

    inline bool Unregister(CbHandle handle) {
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

    inline void UnregisterAll() {
        std::lock_guard<std::mutex> lock{_mutex};

        _callbacks.clear();
    }

    template <class... Args>
    inline void Invoke(Args &&...args) const {
        std::lock_guard<std::mutex> lock{_mutex};

        for (const auto &callbackInfo : _callbacks) {
            callbackInfo.second(args...);
        }
    }

    inline Callback &operator+=(Function &&callback) {
        Register(std::move(callback));
        return *this;
    }

private:
    mutable std::mutex _mutex;
    CbHandle _nextHandle{1};
    std::vector<std::pair<CbHandle, Function>> _callbacks;
};

class Timer {
public:
    Timer() = default;

    template <class... Args>
    inline Timer(Args &&...args) {
        Start(std::forward<Args>(args)...);
    }

    inline ~Timer() {
        Stop();
    }

    inline void Start(std::chrono::milliseconds interval, std::function<void()> callback) {
        Stop();
        _destroy = false;
        _interval = std::move(interval);
        Reset();
        _thread = std::thread{&Timer::Thread, this, std::move(callback)};
    }

    inline void Stop() {
        _destroy = true;
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    inline void Reset() {
        _deadline = Clock::now() + _interval.load();
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::atomic<bool> _destroy{false};
    std::atomic<std::chrono::milliseconds> _interval;
    std::atomic<TimePoint> _deadline;
    std::thread _thread;

    inline void Thread(std::function<void()> callback) {
        while (true) {
            std::this_thread::sleep_until(_deadline.load());
            if (_destroy) {
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
