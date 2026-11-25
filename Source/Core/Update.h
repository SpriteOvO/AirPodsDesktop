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

#include <string>
#include <optional>

#include <QString>
#include <QVersionNumber>

#include "../Helper.h"

namespace Core::Update {

using namespace std::chrono_literals;

using FnProgress = std::function<bool(size_t downloaded, size_t total)>;

struct ReleaseInfo {
    bool CanAutoUpdate() const;
    void OpenUrl() const;

    QVersionNumber version;
    QString url;
    QString fileName;
    std::string downloadUrl;
    size_t fileSize{0};
    QString sha256;
    QString changeLog;
    bool isPreRelease{false};
};

namespace Details {

// The updater selects one asset format per platform. Windows uses the NSIS installer, while Linux
// uses the Debian package because it is the format the project officially distributes.
enum class Platform { Windows, Linux };

Platform CurrentPlatform();

std::optional<ReleaseInfo> ParseSingleReleaseResponse(
    const std::string &text, Platform platform = CurrentPlatform());
std::optional<ReleaseInfo> ParseMultipleReleasesResponseFirst(
    const std::string &text, Platform platform = CurrentPlatform());
bool IsCompatibleUpdateAsset(const QString &fileName, Platform platform);

// Only a package-managed installation can hand the downloaded package to the system installer.
// Build trees, tarball extractions and other prefixes fall back to a manual download.
bool IsPackageManagedInstall(const QString &applicationDirPath);

// Clears the automatic-update fields when the selected asset cannot be applied to this install.
void ApplyInstallPolicy(ReleaseInfo &info, Platform platform, bool packageManaged);

bool VerifyFileSha256(const QString &filePath, const QString &expectedSha256);

} // namespace Details

QVersionNumber ToVersionNumber(QString str);

QVersionNumber GetLocalVersion();

std::optional<ReleaseInfo> FetchUpdateRelease();
bool DownloadInstall(const ReleaseInfo &info, const FnProgress &progressCallback);

class AsyncChecker
{
public:
    using FnCallback =
        std::function<void(const Core::Update::ReleaseInfo &releaseInfo, bool silent)>;

    AsyncChecker(FnCallback callback);
    ~AsyncChecker();

    void Start();
    void Stop();

private:
    constexpr static auto kInterval = 1h;

    FnCallback _callback;
    Helper::Timer _timer;
    bool _isFirst = true;

    void Checker();
};

} // namespace Core::Update
