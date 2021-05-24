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
#include <QVersionNumber>
#include <QDesktopServices>
#include <cpr/cpr.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "../Logger.h"
#include "../Application.h"


using json = nlohmann::json;

namespace Core::Update
{
    namespace Impl
    {
        StatusInfo ParseResponse(const std::string &text)
        {
            try {
                auto root = json::parse(text);

                const auto &tagField = root["tag_name"];
                const auto &bodyField = root["body"];
                const auto &urlField = root["html_url"];
                const auto &assets = root["assets"];

                if (!tagField.is_string() || !bodyField.is_string() || !urlField.is_string() ||
                    !assets.is_array())
                {
                    spdlog::warn(
                        "ParseResponse: Response json fields type mismatch. "
                        "tag: {}, body: {}, url: {}, assets: {}",
                        tagField.type_name(), bodyField.type_name(), urlField.type_name(),
                        assets.type_name()
                    );

                    return std::make_pair(
                        Status{Status::UpdateResponseJsonFieldsTypeMismatch}, std::nullopt
                    );
                }

                auto tag = QString::fromStdString(tagField.get<std::string>());
                auto body = QString::fromStdString(bodyField.get<std::string>());
                auto url = QString::fromStdString(urlField.get<std::string>());

                // Check url
                //
                if (url.indexOf(Config::UrlRepository) != 0)
                {
                    spdlog::warn("ParseResponse: 'url' invalid. content: {}", url);

                    return std::make_pair(
                        Status{Status::UpdateResponseJsonFieldUrlInvalid}.SetAdditionalData(url),
                        std::nullopt
                    );
                }

                // Check body
                //
                QString changeLog;
                if (body.isEmpty()) {
                    spdlog::warn("ParseResponse: 'body' is empty.");
                }
                else {
                    // Find change log
                    //
                    int clBeginPos = body.indexOf("Change log", 0, Qt::CaseInsensitive);
                    if (clBeginPos == -1) {
                        clBeginPos = body.indexOf("ChangeLog", 0, Qt::CaseInsensitive);
                    }

                    if (clBeginPos == -1) {
                        spdlog::warn("ParseResponse: Find change log block failed. body: {}", body);
                    }
                    else {
                        changeLog = body.right(body.length() - clBeginPos).trimmed();
                        changeLog = changeLog.right(changeLog.length() - changeLog.indexOf('\n'))
                            .trimmed();

                        // Find end of ChangeLog
                        //
                        int clEndPos = changeLog.indexOf("\r\n\r\n");
                        if (clEndPos == -1) {
                            clEndPos = changeLog.indexOf("\n\n");
                        }
                        
                        changeLog = changeLog.left(clEndPos);
                    }
                }

                // Get versions
                //
                auto localVer = QVersionNumber::fromString(Config::Version::String);
                auto latestVer = QVersionNumber::fromString(tag);

                // Fill result
                //
                Info info;

                info.needToUpdate = latestVer.normalized() > localVer.normalized();
                info.localVer = localVer.toString();
                info.latestVer = latestVer.toString();
                info.latestUrl = std::move(url);
                info.changeLog = std::move(changeLog);

                if (info.needToUpdate)
                {
                    for (const auto &asset : assets)
                    {
                        const auto &nameField = asset["name"];
                        const auto &sizeField = asset["size"];
                        const auto &downloadUrlField = asset["browser_download_url"];

                        if (!nameField.is_string() || !sizeField.is_number_unsigned() ||
                            !downloadUrlField.is_string())
                        {
                            spdlog::warn(
                                "ParseResponse: Asset json fields type mismatch. Continue. "
                                "name: {}, size: {}, download_url: {}",
                                nameField.type_name(), sizeField.type_name(),
                                downloadUrlField.type_name()
                            );
                            continue;
                        }

                        auto name = QString::fromStdString(nameField.get<std::string>());
                        const auto &size = sizeField.get<size_t>();
                        const auto &downloadUrl = downloadUrlField.get<std::string>();

                        if (name.isEmpty() || size == 0 || downloadUrl.empty()) {
                            spdlog::warn(
                                "ParseResponse: Asset json fields value is empty. Continue."
                            );
                            continue;
                        }

                        spdlog::info(
                            "ParseResponse: Asset name: {}, size: {}, downloadUrl: {}.",
                            name, size, downloadUrl
                        );
#if !defined APD_OS_WIN
#   error "Need to port."
#endif
                        // AirPodsDesktop-x.x.x-win32.exe
                        //
                        if (QFileInfo{name}.suffix() != "exe") {
                            spdlog::warn("ParseResponse: Asset suffix is unsupported. Continue.");
                            continue;
                        }

                        if (name.indexOf(CONFIG_CPACK_SYSTEM_NAME) == -1) {
                            spdlog::warn("ParseResponse: Asset platform is incorrectly. Continue.");
                            continue;
                        }

                        info.latestFileName = name;
                        info.latestFileUrl = downloadUrlField;
                        info.fileSize = size;

                        spdlog::info("ParseResponse: Found matching file.");
                        break;
                    }
                }

                spdlog::info(
                    "{}. Local version: {}, latest version: {}.",
                    info.needToUpdate ? "Need to update" : "No need to update",
                    info.localVer, info.latestVer
                );

                return std::make_pair(Status::Success, std::move(info));
            }
            catch (json::exception &exception)
            {
                spdlog::warn(
                    "Update: json parse failed. what: {}, text: {}",
                    exception.what(), text
                );

                return std::make_pair(
                    Status{Status::UpdateResponseJsonParseFailed}
                        .SetAdditionalData(exception.what(), text),
                    std::nullopt
                );
            }
        }

    } // namespace Impl


    bool Info::CanAutoUpdate() const
    {
        if (latestFileName.isEmpty() || latestFileUrl.empty() || fileSize == 0) {
            spdlog::warn("CanAutoUpdate: Nothing to download.");
            return false;
        }

#if !defined APD_OS_WIN
#   error "Need to port."
#endif
        spdlog::info("CanAutoUpdate: Latest file name: '{}'.", latestFileName);

        if (QFileInfo{latestFileName}.suffix() != "exe") {
            spdlog::warn("CanAutoUpdate: Cannot auto update. Because the suffix is unsupported.");
            return false;
        }

        // AirPodsDesktop-x.x.x-win32.exe
        //
        if (latestFileName.indexOf(CONFIG_CPACK_SYSTEM_NAME) == -1) {
            spdlog::warn("CanAutoUpdate: Cannot auto update. Because the platform is incorrectly.");
            return false;
        }

        spdlog::info("CanAutoUpdate: Can auto update.");
        return true;
    }

    Status Info::DownloadAndInstall(const FnProgress &progressCallback) const
    {
        if (!CanAutoUpdate()) {
            spdlog::warn("Download: Cannot auto update.");
            return Status{Status::UpdateDownloadCannotAutoUpdate};
        }

        QTemporaryDir tempPath;
        if (!tempPath.isValid())
        {
            auto errorString = tempPath.errorString();
            spdlog::warn(
                "Download: QTemporaryDir construct failed. error: {}",
                errorString
            );
            return Status{Status::UpdateDownloadCreateDirectoryFailed}
                .SetAdditionalData(std::move(errorString));
        }

        QFileInfo file = tempPath.filePath(QUrl{QString::fromStdString(latestFileUrl)}.fileName());
        QString filePath = file.absoluteFilePath();

        spdlog::info("Download: Ready to download to '{}'.", filePath);

        // Begin download
        //

        std::ofstream outFile{filePath.toStdString(), std::ios::binary};
        auto response = cpr::Download(
            outFile,
            cpr::Url{latestFileUrl},
            cpr::ProgressCallback{
                [&](size_t downloadTotal, size_t downloadNow, size_t uploadTotal, size_t uploadNow)
                {
                    spdlog::trace("Downloaded {} / {} bytes.", downloadNow, downloadTotal);
                    return progressCallback(downloadTotal, downloadNow);
                }
            }
        );

        if (response.status_code != 200)
        {
            spdlog::warn(
                "Download: Download response status code is not 200. code: {}, message: {}",
                response.status_code, response.error.message
            );
            return Status{Status::UpdateDownloadStatusCodeIsNot200}
                .SetAdditionalData(response.status_code, response.error.message);
        }

        if (response.downloaded_bytes != fileSize)
        {
            spdlog::warn(
                "Download: Download file size mismatch. Downloaded: {}, expect: {}",
                response.downloaded_bytes, fileSize
            );
            return Status{Status::UpdateDownloadFileSizeMismatch}
                .SetAdditionalData((size_t)response.downloaded_bytes, fileSize);
        }

        outFile.close();
        tempPath.setAutoRemove(false);

        // Download succeeded
        //
        spdlog::info(
            "Download: Downloaded succeeded. filePath: {}, size: {}",
            filePath, response.downloaded_bytes
        );

        if (!QProcess::startDetached(filePath)) {
            spdlog::warn("Download: Start installer failed.");
            return Status::UpdateDownloadStartInstallerFailed;
        }

        // Quit for install new version
        //
        Application::QuitSafety();

        return Status::Success;
    }

    void Info::PopupLatestUrl() const
    {
        QDesktopServices::openUrl(QUrl{latestUrl});
    }

    StatusInfo Check()
    {
        cpr::Response response = cpr::Get(
            cpr::Url{"https://api.github.com/repos/SpriteOvO/AirPodsDesktop/releases/latest"},
            cpr::Header{{"Accept", "application/vnd.github.v3+json"}}
        );

        if (response.status_code != 200)
        {
            spdlog::warn(
                "Check: GitHub REST API response status code isn't 200. code: {}, text: {}",
                response.status_code, response.text
            );

            return std::make_pair(
                Status{Status::UpdateResponseStatusCodeIsNot200}
                    .SetAdditionalData(response.status_code, response.text),
                std::nullopt
            );
        }

        return Impl::ParseResponse(response.text);
    }

} // namespace Core::Update
