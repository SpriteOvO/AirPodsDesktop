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

#include <string>
#include <optional>

#include <QString>
#include <QVersionNumber>

namespace Core::Update {

using FnProgress = std::function<bool(size_t downloaded, size_t total)>;

struct ReleaseInfo {
    bool CanAutoUpdate() const;

    QVersionNumber version;
    QString url;
    QString fileName;
    std::string downloadUrl;
    size_t fileSize{0};
    QString changeLog;
    bool isPreRelease{false};
};

QVersionNumber GetLocalVersion();

std::optional<ReleaseInfo> FetchUpdateRelease();
bool DownloadInstall(const ReleaseInfo &info, const FnProgress &progressCallback);

} // namespace Core::Update
