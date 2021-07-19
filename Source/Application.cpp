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

#include "Application.h"

#include <QMessageBox>
#include <spdlog/spdlog.h>

#include <Config.h>
#include "Logger.h"
#include "Core/Bluetooth.h"
#include "Core/GlobalMedia.h"
#include "Core/Settings.h"
#include "Core/Update.h"

void Application::PreConstructorInit()
{
    setAttribute(Qt::AA_DisableWindowContextHelpButton);
    setAttribute(Qt::AA_EnableHighDpiScaling);
}

void Application::InitSettings()
{
    Status status = Core::Settings::LoadFromLocal();
    if (status.IsFailed()) {
        switch (status.GetValue()) {
        case Status::SettingsLoadedDataAbiIncompatible:
            QMessageBox::information(
                nullptr, Config::ProgramName,
                tr("Settings format has changed a bit and needs to be reconfigured."));
            [[fallthrough]];

        case Status::SettingsLoadedDataNoAbiVer:
            _isFirstTimeUse = true;
            Core::Settings::LoadDefault();
            FirstTimeUse();
            break;

        default:
            Core::Settings::LoadDefault();
            SPDLOG_CRITICAL("Unhandled error occurred while loading settings: {}", status);
            break;
        }
    }
}

void Application::FirstTimeUse()
{
    QMessageBox::information(
        nullptr, Config::ProgramName,
        tr("Hello, welcome to %1!\n"
           "\n"
           "This seems to be your first time using this program.\n"
           "Let's configure something together.")
            .arg(Config::ProgramName));

    auto current = Core::Settings::GetCurrent();

    // Auto run
    //
    current.auto_run =
        QMessageBox::question(
            nullptr, Config::ProgramName,
            tr("Do you want this program to launch when the system starts?\n"
               "\n"
               "If you frequently use AirPods on the desktop, I recommend that you click \"Yes\"."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;

    // Low audio latency
    //
    current.low_audio_latency =
        QMessageBox::question(
            nullptr, Config::ProgramName,
            tr("Do you want to enable \"low audio latency\" mode?\n"
               "\n"
               "It improves the problem of short audio not playing, but may increase battery "
               "consumption."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;

    Core::Settings::SaveToCurrentAndLocal(std::move(current));

    QMessageBox::information(
        nullptr, Config::ProgramName,
        tr("Great! Everything is ready!\n"
           "\n"
           "Enjoy it all~"));
}

Application::Application(int argc, char *argv[]) : QApplication{argc, argv}
{
    bool enableTrace = false;

    try {
        _options.add_options()(
            "trace", "Enable trace level logging.", cxxopts::value<bool>()->default_value("false"));

        auto args = _options.parse(argc, argv);
        enableTrace = args["trace"].as<bool>();
    }
    catch (cxxopts::OptionException &exception) {
        Logger::DoError(QString{"Parse options failed.\n\n%1"}.arg(exception.what()), true);
        APD_ASSERT(false);
    }

    Logger::Initialize(enableTrace);

    SPDLOG_INFO("Launched. Version: '{}'", Config::Version::String);
#if defined APD_DEBUG
    SPDLOG_INFO("Build configuration: Debug");
#else
    SPDLOG_INFO("Build configuration: Not Debug");
#endif

    SPDLOG_INFO("Args: {{ trace: {} }}", enableTrace);

    InitSettings();
    Core::Bluetooth::Initialize();
    Core::GlobalMedia::Initialize();

    setFont(QFont{"Segoe UI", 9});
    setWindowIcon(QIcon{":/Resource/Image/Icon.svg"});
    setQuitOnLastWindowClosed(false);

    _sysTray = std::make_unique<Gui::SysTray>();
    _infoWindow = std::make_unique<Gui::InfoWindow>();

    connect(this, &Application::aboutToQuit, this, &Application::QuitHandler);
}

int Application::Run()
{
    do {
        if (!CheckUpdate()) {
            break;
        }

        Core::AirPods::StartScanner();

    } while (false);

    return exec();
}

bool Application::CheckUpdate()
{
    const auto optInfo = Core::Update::FetchLatestRelease();
    if (!optInfo.has_value()) {
        SPDLOG_WARN("AppUpdate: Core::Update::FetchLatestRelease() returned nullopt.");
        return true;
    }

    const auto &latestInfo = optInfo.value();

    const auto localVerNum = Core::Update::GetLocalVersion();
    const auto needToUpdate = Core::Update::NeedToUpdate(latestInfo);

    SPDLOG_INFO(
        "{}. Local version: {}, latest version: {}.",
        needToUpdate ? "Need to update" : "No need to update", localVerNum.toString(),
        latestInfo.version.toString());

    if (!needToUpdate) {
        SPDLOG_INFO("AppUpdate: No need to update.");
        return true;
    }

    if (latestInfo.version.toString() == Core::Settings::GetCurrent().skipped_version) {
        SPDLOG_INFO("AppUpdate: User skipped this new version. Ignore.");
        return true;
    }

    QString changeLogBlock;
    if (!latestInfo.changeLog.isEmpty()) {
        changeLogBlock = QString{"%1\n%2\n\n"}.arg(tr("Change log:")).arg(latestInfo.changeLog);
    }

    auto button = QMessageBox::question(
        nullptr, Config::ProgramName,
        tr("Hey! I found a new version available!\n"
           "\n"
           "Current version: %1\n"
           "Latest version: %2\n"
           "\n"
           "%3"
           "Click \"Ignore\" to skip this new version.\n"
           "\n"
           "Do you want to update it now?")
            .arg(localVerNum.toString())
            .arg(latestInfo.version.toString())
            .arg(changeLogBlock),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Ignore, QMessageBox::Yes);

    if (button == QMessageBox::Yes) {
        SPDLOG_INFO("AppUpdate: User clicked Yes.");

        if (!latestInfo.CanAutoUpdate()) {
            SPDLOG_INFO("AppUpdate: Popup latest url and quit.");
            latestInfo.PopupUrl();
            Application::QuitSafety();
            return false;
        }

        _downloadWindow = std::make_unique<Gui::DownloadWindow>(latestInfo);
        _downloadWindow->show();
    }
    else if (button == QMessageBox::Ignore) {
        SPDLOG_INFO("AppUpdate: User clicked Ignore.");

        auto currentSettings = Core::Settings::GetCurrent();
        currentSettings.skipped_version = latestInfo.version.toString();
        Core::Settings::SaveToCurrentAndLocal(std::move(currentSettings));
    }
    else {
        SPDLOG_INFO("AppUpdate: User clicked No.");
    }
    return true;
}

void Application::QuitHandler()
{
    Core::AirPods::OnQuit();
}

void Application::PopupAboutWindow(QWidget *parent)
{
    // clang-format off

    QString content = tr(
        "<h3>%1</h3>"
        "<p>%2</p>"
        "<hr>"
        "<p>Version %3 (<a href=\"%4\">Change log</a>)</p>"
        "<p>Open source on <a href=\"%5\">%6</a></p>"
        "<p>Licensed under the <a href=\"%7\">%8</a></p>"
        "<p>%9</p>")
        .arg(Config::ProgramName)
        .arg(Config::Description)
        .arg(Config::Version::String).arg(Config::UrlCurrentRelease)
        .arg(Config::UrlRepository).arg("GitHub")
        .arg(Config::UrlLicense).arg(Config::License)
        .arg(Config::Copyright);

    // clang-format on

    QMessageBox::about(parent, tr("About %1").arg(Config::ProgramName), content);
}

void Application::QuitSafety()
{
    QMetaObject::invokeMethod(qApp, &Application::quit, Qt::QueuedConnection);
}
