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
#include "PlaybackMonitor_linux.h"

#if defined APD_OS_LINUX

    #include <algorithm>
    #include <charconv>
    #include <chrono>
    #include <cstring>
    #include <memory>
    #include <unistd.h>

    #include <pulse/pulseaudio.h>

namespace Core::GlobalMedia {

namespace Details {

bool HasExternalPlayback(const std::vector<LinuxPlaybackStream> &streams, int64_t selfProcessId)
{
    return std::any_of(streams.begin(), streams.end(), [selfProcessId](const auto &stream) {
        if (stream.corked) {
            return false;
        }
        return !stream.processId.has_value() || *stream.processId != selfProcessId;
    });
}

} // namespace Details

namespace {

constexpr auto kPulseTimeout = std::chrono::milliseconds{500};

std::optional<int64_t> ParseProcessId(const pa_proplist *proplist)
{
    if (proplist == nullptr) {
        return std::nullopt;
    }
    const char *raw = pa_proplist_gets(proplist, PA_PROP_APPLICATION_PROCESS_ID);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    int64_t value{};
    const auto *end = raw + std::strlen(raw);
    const auto [ptr, ec] = std::from_chars(raw, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

class PulsePlaybackProbe
{
public:
    ~PulsePlaybackProbe()
    {
        Reset();
    }

    std::optional<bool> ReadActivity()
    {
        if (!EnsureConnected()) {
            return std::nullopt;
        }

        struct CollectState {
            std::vector<Details::LinuxPlaybackStream> streams;
            bool finished{false};
            bool failed{false};
        } state;

        pa_operation *op = pa_context_get_sink_input_info_list(
            _context,
            [](pa_context *, const pa_sink_input_info *info, int eol, void *userdata) {
                auto *collect = static_cast<CollectState *>(userdata);
                if (eol < 0) {
                    collect->failed = true;
                    collect->finished = true;
                    return;
                }
                if (eol > 0) {
                    collect->finished = true;
                    return;
                }
                if (info != nullptr) {
                    collect->streams.push_back(Details::LinuxPlaybackStream{
                        ParseProcessId(info->proplist),
                        info->corked != 0,
                    });
                }
            },
            &state);

        if (op == nullptr) {
            Reset();
            return std::nullopt;
        }

        const bool completed = RunUntil([&] {
            if (state.finished) {
                return true;
            }
            const auto ctxState = pa_context_get_state(_context);
            return ctxState == PA_CONTEXT_FAILED || ctxState == PA_CONTEXT_TERMINATED;
        });

        if (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
            pa_operation_cancel(op);
        }
        pa_operation_unref(op);

        if (!completed || !state.finished || state.failed ||
            pa_context_get_state(_context) != PA_CONTEXT_READY)
        {
            Reset();
            return std::nullopt;
        }

        return Details::HasExternalPlayback(state.streams, static_cast<int64_t>(getpid()));
    }

private:
    pa_mainloop *_mainloop{nullptr};
    pa_context *_context{nullptr};

    void Reset()
    {
        if (_context != nullptr) {
            pa_context_disconnect(_context);
            pa_context_unref(_context);
            _context = nullptr;
        }
        if (_mainloop != nullptr) {
            pa_mainloop_free(_mainloop);
            _mainloop = nullptr;
        }
    }

    template <class Pred>
    bool RunUntil(Pred &&done)
    {
        const auto deadline = std::chrono::steady_clock::now() + kPulseTimeout;
        while (!done()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return false;
            }
            const auto remainingUs =
                std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count();
            if (pa_mainloop_prepare(_mainloop, static_cast<int>(remainingUs)) < 0 ||
                pa_mainloop_poll(_mainloop) < 0 || pa_mainloop_dispatch(_mainloop) < 0)
            {
                return false;
            }
        }
        return true;
    }

    bool EnsureConnected()
    {
        if (_context != nullptr) {
            if (pa_context_get_state(_context) == PA_CONTEXT_READY) {
                // Drain any pending server events (e.g. server disconnect) before issuing a query.
                while (pa_mainloop_iterate(_mainloop, 0, nullptr) > 0) {
                }
                if (pa_context_get_state(_context) == PA_CONTEXT_READY) {
                    return true;
                }
            }
            Reset();
        }

        _mainloop = pa_mainloop_new();
        if (_mainloop == nullptr) {
            return false;
        }

        _context = pa_context_new(pa_mainloop_get_api(_mainloop), "AirPodsDesktop");
        if (_context == nullptr) {
            Reset();
            return false;
        }

        // Do not autospawn a PulseAudio daemon on headless or non-audio sessions.
        if (pa_context_connect(_context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
            Reset();
            return false;
        }

        const bool settled = RunUntil([this] {
            const auto state = pa_context_get_state(_context);
            return state == PA_CONTEXT_READY || state == PA_CONTEXT_FAILED ||
                   state == PA_CONTEXT_TERMINATED;
        });

        if (!settled || pa_context_get_state(_context) != PA_CONTEXT_READY) {
            Reset();
            return false;
        }
        return true;
    }
};

} // namespace

PlaybackProbe CreatePlaybackProbe()
{
    auto probe = std::make_shared<PulsePlaybackProbe>();
    return [probe = std::move(probe)] { return probe->ReadActivity(); };
}

} // namespace Core::GlobalMedia

#endif
