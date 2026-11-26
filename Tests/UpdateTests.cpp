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

#include <QTest>

#include <Config.h>
#include "Source/Core/Update.h"

namespace {

QString AssetJson(const QString &name, size_t size)
{
    const auto url = QString{Config::UrlReleases} + "/download/v1.2.3/" + name;
    const auto digest = QString(64, QChar{'a'});

    return QString{R"({"name":"%1","size":%2,"browser_download_url":"%3","digest":"sha256:%4"})"}
        .arg(name)
        .arg(size)
        .arg(url, digest);
}

std::string ReleaseJson(const QList<QString> &assets)
{
    const auto base = QString{Config::UrlReleases};
    const auto assetsJson = QStringList{assets}.join(QChar{','});

    return QString{
               R"({"tag_name":"v1.2.3","body":"## Change log\n- Test change",)"
               R"("html_url":"%1/tag/v1.2.3","prerelease":false,"assets":[%2]})"}
        .arg(base, assetsJson)
        .toStdString();
}

} // namespace

class UpdateTests : public QObject
{
    Q_OBJECT

private slots:
    void ToVersionNumber_data();
    void ToVersionNumber();

    void SelectsUpdateAsset_data();
    void SelectsUpdateAsset();

    void ParsesWindowsRelease();
    void ParsesLinuxRelease();

    void DetectsPackageManagedInstall_data();
    void DetectsPackageManagedInstall();

    void AppliesInstallPolicy_data();
    void AppliesInstallPolicy();
};

void UpdateTests::ToVersionNumber_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("release tag") << QString{"v1.2.3"} << QString{"1.2.3"};
    QTest::newRow("upper-case tag") << QString{"V0.6.1"} << QString{"0.6.1"};
    QTest::newRow("plain version") << QString{"1.2.3"} << QString{"1.2.3"};
    QTest::newRow("two components") << QString{"v2.0"} << QString{"2.0"};
}

void UpdateTests::ToVersionNumber()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);

    QCOMPARE(Core::Update::ToVersionNumber(text).toString(), expected);
}

void UpdateTests::SelectsUpdateAsset_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<bool>("isLinux");
    QTest::addColumn<bool>("expected");

    QTest::newRow("Windows installer")
        << QString{"AirPodsDesktop-0.6.1-win64.exe"} << false << true;
    QTest::newRow("Windows portable archive")
        << QString{"AirPodsDesktop-0.6.1-win64-portable.zip"} << false << false;
    QTest::newRow("Windows bridge is not selected by the x64 updater")
        << QString{"AirPodsDesktop-0.6.1-win32-bridge.exe"} << false << false;
    QTest::newRow("Linux Debian package")
        << QString{"airpodsdesktop_0.6.1_amd64.deb"} << true << true;
    QTest::newRow("Linux package of another architecture")
        << QString{"airpodsdesktop_0.6.1_arm64.deb"} << true << false;
    QTest::newRow("Linux tarball")
        << QString{"airpodsdesktop-0.6.1-Linux.tar.gz"} << true << false;
    QTest::newRow("Windows installer on Linux")
        << QString{"AirPodsDesktop-0.6.1-win64.exe"} << true << false;
    QTest::newRow("Linux package on Windows")
        << QString{"airpodsdesktop_0.6.1_amd64.deb"} << false << false;
}

void UpdateTests::SelectsUpdateAsset()
{
    QFETCH(QString, fileName);
    QFETCH(bool, isLinux);
    QFETCH(bool, expected);

    const auto platform = isLinux ? Core::Update::Details::Platform::Linux
                                  : Core::Update::Details::Platform::Windows;
    QCOMPARE(Core::Update::Details::IsCompatibleUpdateAsset(fileName, platform), expected);
}

void UpdateTests::ParsesWindowsRelease()
{
    const auto json =
        ReleaseJson({AssetJson(QStringLiteral("AirPodsDesktop-1.2.3-win64.exe"), 10),
                     AssetJson(QStringLiteral("AirPodsDesktop-1.2.3-win64-portable.zip"), 20)});

    const auto info = Core::Update::Details::ParseSingleReleaseResponse(
        json, Core::Update::Details::Platform::Windows);

    QVERIFY(info.has_value());
    QCOMPARE(info->version, QVersionNumber(1, 2, 3));
    QCOMPARE(info->fileName, QString{"AirPodsDesktop-1.2.3-win64.exe"});
    QCOMPARE(info->sha256, QString(64, QChar{'a'}));
    QVERIFY(info->CanAutoUpdate());
}

void UpdateTests::ParsesLinuxRelease()
{
    const auto json = ReleaseJson(
        {AssetJson(QStringLiteral("airpodsdesktop-1.2.3-Linux.tar.gz"), 20),
         AssetJson(QStringLiteral("airpodsdesktop_1.2.3_amd64.deb"), 10)});

    const auto info = Core::Update::Details::ParseSingleReleaseResponse(
        json, Core::Update::Details::Platform::Linux);

    QVERIFY(info.has_value());
    QCOMPARE(info->version, QVersionNumber(1, 2, 3));
    QCOMPARE(info->fileName, QString{"airpodsdesktop_1.2.3_amd64.deb"});
    QCOMPARE(info->fileSize, size_t{10});
    QCOMPARE(info->sha256, QString(64, QChar{'a'}));
    QVERIFY(info->CanAutoUpdate());
}

void UpdateTests::DetectsPackageManagedInstall_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("expected");

    QTest::newRow("Debian bundle") << QString{"/usr/lib/airpodsdesktop"} << true;
    QTest::newRow("system prefix") << QString{"/usr"} << true;
    QTest::newRow("build tree")
        << QString{"/home/user/AirPodsDesktop/Build/Binary"} << false;
    QTest::newRow("similar prefix") << QString{"/usrlocal/bin"} << false;
    QTest::newRow("home prefix") << QString{"/home/user/.local/lib/airpodsdesktop"} << false;
}

void UpdateTests::DetectsPackageManagedInstall()
{
    QFETCH(QString, path);
    QFETCH(bool, expected);

    QCOMPARE(Core::Update::Details::IsPackageManagedInstall(path), expected);
}

void UpdateTests::AppliesInstallPolicy_data()
{
    QTest::addColumn<bool>("isLinux");
    QTest::addColumn<bool>("packageManaged");
    QTest::addColumn<bool>("autoUpdate");

    QTest::newRow("Windows installer")
        << false << false << true;
    QTest::newRow("Linux package-managed install")
        << true << true << true;
    QTest::newRow("Linux tarball or build tree")
        << true << false << false;
}

void UpdateTests::AppliesInstallPolicy()
{
    QFETCH(bool, isLinux);
    QFETCH(bool, packageManaged);
    QFETCH(bool, autoUpdate);

    Core::Update::ReleaseInfo info;
    info.version = QVersionNumber(9, 9, 9);
    info.fileName = isLinux ? QString{"airpodsdesktop_9.9.9_amd64.deb"}
                            : QString{"AirPodsDesktop-9.9.9-win64.exe"};
    info.downloadUrl = "https://example.invalid/package";
    info.fileSize = 10;
    info.sha256 = QString(64, QChar{'a'});

    const auto platform = isLinux ? Core::Update::Details::Platform::Linux
                                  : Core::Update::Details::Platform::Windows;
    Core::Update::Details::ApplyInstallPolicy(info, platform, packageManaged);

    QCOMPARE(info.CanAutoUpdate(), autoUpdate);
    QCOMPARE(info.fileName.isEmpty(), !autoUpdate);
}

QTEST_MAIN(UpdateTests)

#include "UpdateTests.moc"
