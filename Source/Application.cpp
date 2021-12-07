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

#include <Config.h>
#include "Logger.h"
#include "Error.h"
#include "Core/Bluetooth.h"
#include "Core/GlobalMedia.h"
#include "Core/Settings.h"
#include "Core/Update.h"

void ApdApplication::PreConstruction()
{
    setAttribute(Qt::AA_DisableWindowContextHelpButton);
    setAttribute(Qt::AA_EnableHighDpiScaling);
}

void ApdApplication::InitSettings()
{
    auto result = Core::Settings::Load();

    switch (result) {
    case Core::Settings::LoadResult::AbiIncompatible:
        QMessageBox::information(
            nullptr, Config::ProgramName,
            tr("Settings format has changed a bit and needs to be reconfigured."));
        [[fallthrough]];

    case Core::Settings::LoadResult::NoAbiField:
        _isFirstTimeUse = true;
        FirstTimeUse();
        break;

    case Core::Settings::LoadResult::Successful:
        break;

    default:
        FatalError(std::format("Unhandled LoadResult: '{}'", Helper::ToUnderlying(result)), true);
    }

    Core::Settings::Apply();
}

void ApdApplication::FirstTimeUse()
{
    const auto &constMetaFields = Core::Settings::GetConstMetaFields();

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
               "\n%1")
                .arg(constMetaFields.low_audio_latency.Description()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;

    Core::Settings::Save(std::move(current));

    QMessageBox::information(
        nullptr, Config::ProgramName,
        tr("Great! Everything is ready!\n"
           "\n"
           "Enjoy it all~"));
}

ApdApplication::ApdApplication(int argc, char *argv[]) : SingleApplication{argc, argv} {}

bool ApdApplication::Prepare(int argc, char *argv[])
{
    Error::Initialize();

    try {
        using namespace cxxopts;

        cxxopts::Options opts{Config::ProgramName, Config::Description};

        opts.add_options()(
            "trace", "Enable trace level logging.", value<bool>()->default_value("false"));

        auto args = opts.parse(argc, argv);
        _launchOptions.enableTrace = args["trace"].as<bool>();
    }
    catch (cxxopts::OptionException &exception) {
        FatalError(std::format("Parse options failed.\n\n{}", exception.what()), true);
        return false;
    }

    Logger::Initialize(_launchOptions.enableTrace);

    LOG(Info, "Launched. Version: '{}'", Config::Version::String);
#if defined APD_BUILD_GIT_HASH
    LOG(Info, "Build git hash: '{}'", APD_BUILD_GIT_HASH);
#endif
#if defined APD_DEBUG
    LOG(Info, "Build configuration: Debug");
#else
    LOG(Info, "Build configuration: Not Debug");
#endif

    LOG(Info, "Args: {{ trace: {} }}", _launchOptions.enableTrace);

    setFont(QFont{"Segoe UI", 9});
    setWindowIcon(QIcon{Config::QrcIconSvg});
    setQuitOnLastWindowClosed(false);

#if defined APD_OS_WIN
    Core::OS::Windows::Winrt::Initialize();
#endif

    _trayIcon = std::make_unique<Gui::TrayIcon>();
    _mainWindow = std::make_unique<Gui::MainWindow>();
    _lowAudioLatencyController = std::make_unique<Core::LowAudioLatency::Controller>();

    InitSettings();

    return true;
}

int ApdApplication::Run()
{
    _mainWindow->GetApdMgr().StartScanner();
    return exec();
}

void ApdApplication::QuitSafety()
{
    QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
}
