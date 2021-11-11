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

#include "Update.h"

#include <optional>

#include <QUrl>
#include <QProcess>
#include <QTemporaryDir>
#include <QDesktopServices>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <Config.h>
#include "../Logger.h"
#include "../Application.h"

using json = nlohmann::json;

namespace Core::Update {
namespace Impl {

std::optional<ReleaseInfo> ParseSingleReleaseResponse(const std::string &text)
{
    try {
        const auto root = json::parse(text);

        auto tag = QString::fromStdString(root["tag_name"].get<std::string>());
        auto body = QString::fromStdString(root["body"].get<std::string>());
        auto url = QString::fromStdString(root["html_url"].get<std::string>());

        // Check url
        if (url.indexOf(Config::UrlRepository) != 0) {
            LOG(Warn, "ParseSRResponse: 'html_url' invalid. content: {}", url);
            return std::nullopt;
        }

        // Check body
        QString changeLog;
        if (body.isEmpty()) {
            LOG(Warn, "ParseSRResponse: 'body' is empty.");
        }
        else {
            // Find change log

            int clBeginPos = body.indexOf("Change log", 0, Qt::CaseInsensitive);
            if (clBeginPos == -1) {
                clBeginPos = body.indexOf("ChangeLog", 0, Qt::CaseInsensitive);
            }

            if (clBeginPos == -1) {
                LOG(Warn, "ParseSRResponse: Find change log block failed. body: {}", body);
            }
            else {
                changeLog = body.right(body.length() - clBeginPos).trimmed();
                changeLog = changeLog.right(changeLog.length() - changeLog.indexOf('\n')).trimmed();

                // Find end of ChangeLog
                int clEndPos = changeLog.indexOf("\r\n\r\n");
                if (clEndPos == -1) {
                    clEndPos = changeLog.indexOf("\n\n");
                }

                changeLog = changeLog.left(clEndPos);
            }
        }

        ReleaseInfo info;

        info.version = QVersionNumber::fromString(tag);
        info.url = std::move(url);
        info.changeLog = std::move(changeLog);
        info.isPreRelease = root["prerelease"].get<bool>();

        for (const auto &asset : root["assets"]) {

            auto fileName = QString::fromStdString(asset["name"].get<std::string>());
            auto fileSize = asset["size"].get<size_t>();
            auto downloadUrl = asset["browser_download_url"].get<std::string>();

            if (fileName.isEmpty() || fileSize == 0 || downloadUrl.empty()) {
                LOG(Warn, "ParseSRResponse: Asset json fields value is empty. Continue.");
                continue;
            }

            // Check url
            if (downloadUrl.find(Config::UrlRepository) != 0) {
                LOG(Warn,
                    "ParseSRResponse: 'browser_download_url' invalid. Continue. content: '{}'",
                    downloadUrl);
                continue;
            }

            LOG(Info, "ParseSRResponse: Asset name: '{}', size: {}, downloadUrl: '{}'.", fileName,
                fileSize, downloadUrl);

#if !defined APD_OS_WIN
    #error "Need to port."
#endif
            // AirPodsDesktop-x.x.x-win32.exe
            //
            if (QFileInfo{fileName}.suffix() != "exe") {
                LOG(Warn, "ParseSRResponse: Asset suffix is unsupported. Continue.");
                continue;
            }

            if (fileName.indexOf(CONFIG_CPACK_SYSTEM_NAME) == -1) {
                LOG(Warn, "ParseSRResponse: Asset platform is mismatched. Continue.");
                continue;
            }

            info.fileName = std::move(fileName);
            info.downloadUrl = std::move(downloadUrl);
            info.fileSize = fileSize;

            LOG(Info, "ParseSRResponse: Found matching file.");
            break;
        }

        return info;
    }
    catch (const json::exception &ex) {
        LOG(Warn, "ParseSRResponse: json parse failed. what: '{}', text: '{}'", ex.what(), text);
        return std::nullopt;
    }
}

std::optional<ReleaseInfo> ParseMultipleReleasesResponseFirst(const std::string &text)
{
    try {
        auto root = json::parse(text);
        const auto &release = root.front();
        auto optInfo = ParseSingleReleaseResponse(release.dump());
        if (!optInfo.has_value()) {
            LOG(Warn, "One release info parsing failed.");
            return std::nullopt;
        }
        return optInfo.value();
    }
    catch (json::exception &ex) {
        LOG(Warn, "ParseMRResponse: json parse failed. what: '{}', text: '{}'", ex.what(), text);
        return std::nullopt;
    }
}

std::optional<ReleaseInfo> FetchLatestStableRelease()
{
    const cpr::Response response = cpr::Get(
        cpr::Url{"https://api.github.com/repos/SpriteOvO/AirPodsDesktop/releases/latest"},
        cpr::Header{{"Accept", "application/vnd.github.v3+json"}});

    if (response.status_code != 200) {
        LOG(Warn,
            "FetchLatestRelease: GitHub REST API response status code isn't 200. "
            "code: {} text: '{}'",
            response.status_code, response.text);
        return std::nullopt;
    }

    return Impl::ParseSingleReleaseResponse(response.text);
}

std::optional<ReleaseInfo> FetchReleaseByVersion(const QVersionNumber &version)
{
    const std::string tag = version.toString().toStdString();
    const cpr::Response response = cpr::Get(
        cpr::Url{"https://api.github.com/repos/SpriteOvO/AirPodsDesktop/releases/tags/" + tag},
        cpr::Header{{"Accept", "application/vnd.github.v3+json"}});

    if (response.status_code != 200) {
        LOG(Warn,
            "FetchReleaseByVersion: GitHub REST API response status code isn't 200. "
            "code: {} text: '{}'",
            response.status_code, response.text);
        return std::nullopt;
    }

    return Impl::ParseSingleReleaseResponse(response.text);
}

std::optional<ReleaseInfo> FetchLatestRelease(bool includePreRelease)
{
    if (includePreRelease) {

        const cpr::Response response = cpr::Get(
            cpr::Url{"https://api.github.com/repos/SpriteOvO/AirPodsDesktop/releases"},
            cpr::Header{{"Accept", "application/vnd.github.v3+json"}});

        if (response.status_code != 200) {
            LOG(Warn,
                "FetchRecentReleases: GitHub REST API response status code isn't 200. "
                "code: {} text: '{}'",
                response.status_code, response.text);
            return {};
        }
        return Impl::ParseMultipleReleasesResponseFirst(response.text);
    }
    else {
        return FetchLatestStableRelease();
    }
}

bool IsCurrentPreRelease()
{
    const auto optInfo = Impl::FetchReleaseByVersion(GetLocalVersion());
    if (!optInfo.has_value()) {
        LOG(Warn, "IsCurrentPreRelease: FetchReleaseByVersion() failed.");
        return false;
    }

    const auto result = optInfo->isPreRelease;

    LOG(Info, "IsCurrentPreRelease: returns {}.", result);
    return result;
}

bool NeedToUpdate(const ReleaseInfo &info)
{
    return info.version.normalized() > GetLocalVersion().normalized();
}

} // namespace Impl

//////////////////////////////////////////////////

bool ReleaseInfo::CanAutoUpdate() const
{
    return !fileName.isEmpty() && !downloadUrl.empty() && fileSize != 0;
}

void ReleaseInfo::OpenUrl() const
{
    QDesktopServices::openUrl(QUrl{url});
}

//////////////////////////////////////////////////

QVersionNumber GetLocalVersion()
{
    return QVersionNumber::fromString(Config::Version::String);
}

//////////////////////////////////////////////////

std::optional<ReleaseInfo> FetchUpdateRelease()
{
    const auto isCurrentPreRelease = Impl::IsCurrentPreRelease();
    LOG(Info, "Update: isCurrentPreRelease: '{}'", isCurrentPreRelease);

    const auto optInfo = Impl::FetchLatestRelease(isCurrentPreRelease);
    if (!optInfo.has_value()) {
        LOG(Warn, "Update: FetchLatestRelease() returned nullopt.");
        return std::nullopt;
    }

    const auto &latestInfo = optInfo.value();
    const auto needToUpdate = Impl::NeedToUpdate(latestInfo);

    LOG(Info, "Update: Latest version: '{}'", latestInfo.version.toString());
    if (!needToUpdate) {
        LOG(Info, "Update: No need to update.");
        return std::nullopt;
    }

    LOG(Info, "Update: Need to update.");

    if (latestInfo.version.toString() == Core::Settings::GetCurrent().skipped_version) {
        LOG(Info, "Update: User skipped this new version. Ignore.");
        return std::nullopt;
    }
    return optInfo;
}

//////////////////////////////////////////////////

bool DownloadInstall(const ReleaseInfo &info, const FnProgress &progressCallback)
{
    APD_ASSERT(Impl::NeedToUpdate(info));

    if (!info.CanAutoUpdate()) {
        LOG(Warn, "DownloadInstall: Cannot auto update.");
        return false;
    }

    QTemporaryDir tempPath;
    if (!tempPath.isValid()) {
        auto errorString = tempPath.errorString();
        LOG(Warn, "DownloadInstall: QTemporaryDir construct failed. error: '{}'", errorString);
        return false;
    }

    const QString filePath = QFileInfo{tempPath.filePath(info.fileName)}.absoluteFilePath();

    LOG(Info, "DownloadInstall: Ready to download to '{}'.", filePath);

    // Begin download

    std::ofstream outFile{filePath.toStdString(), std::ios::binary};
    auto response = cpr::Download(
        outFile, cpr::Url{info.downloadUrl},
        cpr::ProgressCallback{[&](cpr::cpr_off_t downloadTotal, cpr::cpr_off_t downloadNow,
                                  cpr::cpr_off_t uploadTotal, cpr::cpr_off_t uploadNow,
                                  intptr_t userdata) {
            LOG(Trace, "Downloaded {} / {} bytes.", downloadNow, downloadTotal);
            return progressCallback(downloadNow, downloadTotal);
        }});

    if (response.status_code != 200) {
        LOG(Warn,
            "DownloadInstall: Download response status code is not 200. code: {}, message: '{}'",
            response.status_code, response.error.message);
        return false;
    }

    if (response.downloaded_bytes != info.fileSize) {
        LOG(Warn, "Download: Download file size mismatch. Downloaded: {}, expect: {}",
            response.downloaded_bytes, info.fileSize);
        return false;
    }

    outFile.close();
    tempPath.setAutoRemove(false);

    // Download succeeded
    //
    LOG(Info, "Download: Downloaded succeeded. filePath: '{}', size: {}", filePath,
        response.downloaded_bytes);

    if (!QProcess::startDetached(filePath)) {
        LOG(Warn, "DownloadInstall: Start installer failed.");
        return false;
    }

    // Quit for install new version
    //
    ApdApplication::QuitSafety();

    return true;
}

//////////////////////////////////////////////////

AsyncChecker::AsyncChecker(FnCallback callback) : _callback{std::move(callback)} {}

AsyncChecker::~AsyncChecker()
{
    Stop();
}

void AsyncChecker::Start()
{
    // clang-format off
    _timer.Start(kInterval, [this] { Checker(); }, true);
    // clang-format on
}

void AsyncChecker::Stop()
{
    _timer.Stop();
}

void AsyncChecker::Checker()
{
    LOG(Info, "Checking update...");

    do {
        const auto optReleaseInfo = Core::Update::FetchUpdateRelease();
        if (!optReleaseInfo.has_value()) {
            break;
        }

        _callback(optReleaseInfo.value(), !_isFirst);
    } while (false);

    _isFirst = false;
}

} // namespace Core::Update
