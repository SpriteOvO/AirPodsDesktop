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

#include <QString>
#include <string>

#include "../Status.h"

namespace Core::Update {

struct Info {
    using FnProgress = std::function<bool(size_t downloaded, size_t total)>;

    bool CanAutoUpdate() const;
    Status DownloadAndInstall(const FnProgress &progressCallback) const;
    void PopupLatestUrl() const;

    bool needToUpdate{false};
    QString localVer;
    QString latestVer;
    QString latestUrl;
    QString latestFileName;
    std::string latestFileUrl;
    size_t fileSize{0};
    QString changeLog;
};

using StatusInfo = std::pair<Status, std::optional<Info>>;

StatusInfo Check();

} // namespace Core::Update
