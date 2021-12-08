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

#include <format>
#include <optional>

#include <cxxopts.hpp>
#include <spdlog/fmt/ostr.h>

namespace Opts {

enum class PrintAllLocales { None, Complete, LangOnly };

struct LaunchOpts {
    bool enableTrace{false};

    template <class OutStream>
    friend inline OutStream &operator<<(OutStream &outStream, const Opts::LaunchOpts &opts)
    {
        return outStream << std::format("{{ trace: {} }}", opts.enableTrace);
    }
};

class LaunchOptsManager
{
public:
    const LaunchOpts &Parse(int argc, char *argv[]);

    inline const auto &GetOpts() const
    {
        return _opts;
    }

private:
    LaunchOpts _opts;

    void HandlePrintAllLocales(PrintAllLocales action);
};

} // namespace Opts
