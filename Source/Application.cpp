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

#include "Application.h"

#include <QGuiApplication>
#include <QMessageBox>
#include <QStyleFactory>

#include <Config.h>
#include "Logger.h"
#include "Error.h"
#include "Utils.h"
#include "Gui/Utils.h"
#include "Gui/DownloadWindow.h"
#include "Gui/MainWindow.h"
#include "Gui/TaskbarStatus.h"
#include "Gui/TrayIcon.h"
#include "Gui/Theme.h"
#include "Core/AirPods.h"
#include "Core/AutoStart.h"
#include "Core/Bluetooth.h"
#include "Core/GlobalMedia.h"
#include "Core/LowAudioLatency.h"
#include "Core/QuickConnect.h"
#if defined APD_OS_WIN
    #include "Core/QuickConnect_win.h"
#endif
#include "Core/Settings.h"
#include "Core/Update.h"

namespace {

Gui::Theme::Mode ToThemeMode(Core::Settings::AppearanceMode mode)
{
    switch (mode) {
    case Core::Settings::AppearanceMode::System:
        return Gui::Theme::Mode::System;
    case Core::Settings::AppearanceMode::Light:
        return Gui::Theme::Mode::Light;
    case Core::Settings::AppearanceMode::Dark:
        return Gui::Theme::Mode::Dark;
    }
    return Gui::Theme::Mode::System;
}

} // namespace

ApdApplication::~ApdApplication()
{
    Core::Settings::SetApplyObserver(nullptr);
}

void ApdApplication::PreConstruction()
{
    setAttribute(Qt::AA_DisableWindowContextHelpButton);
    setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}

void ApdApplication::InitSettings(Core::Settings::LoadResult loadResult)
{
    const auto result = loadResult;

    switch (result) {
    case Core::Settings::LoadResult::AbiIncompatible:
        QMessageBox::information(
            nullptr, Config::ProgramName,
            tr("Settings format has changed a bit and needs to be reconfigured."));
        [[fallthrough]];

    case Core::Settings::LoadResult::NoAbiField:
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
               "If you frequently use AirPods with this computer, it is recommended that you click "
               "\"Yes\"."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;

    // Low audio latency
    //
    current.low_audio_latency =
        QMessageBox::question(
            nullptr, Config::ProgramName,
            tr("Do you want to enable the \"low audio latency\" feature?\n"
               "\n%1")
                .arg(constMetaFields.low_audio_latency.Description()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;

    Core::Settings::Save(std::move(current));

    QMessageBox::information(
        nullptr, Config::ProgramName,
        tr("Great! Everything is ready!\n"
           "\n"
           "Enjoy it all."));

    _trayIcon->ShowMessage(
        tr("You can find me in the system tray"),
        tr("Click the icon to view battery information, right-click to "
           "customize settings or quit."));
}

ApdApplication::ApdApplication(int argc, char *argv[]) : SingleApplication{argc, argv} {}

bool ApdApplication::Prepare(int argc, char *argv[])
{
    Error::Initialize();

    const auto &opts = _launchOptsMgr.Parse(argc, argv);

    Logger::Initialize(opts.enableTrace);

    LOG(Info, "Launched. Version: '{}'", Config::Version::String);
#if defined APD_BUILD_GIT_HASH
    LOG(Info, "Build git hash: '{}'", APD_BUILD_GIT_HASH);
#endif
#if defined APD_DEBUG
    LOG(Info, "Build configuration: Debug");
#else
    LOG(Info, "Build configuration: Not Debug");
#endif

    LOG(Info, "Opts: {}", opts);

    Logger::CleanUpOldLogFiles();

    // Fusion is fully palette-driven, which is what makes light/dark theming and the Fluent
    // stylesheet consistent across every control. It must be set before any widget exists.
    setStyle(QStyleFactory::create("Fusion"));

    QFont font;
    font.setFamily("Segoe UI");
    font.setFamilies({"Segoe UI Variable", "Segoe UI", "Microsoft YaHei UI"});
    font.setPointSize(9);

    setFont(font);
    setWindowIcon(QIcon{Config::QrcIconSvg});
    setQuitOnLastWindowClosed(false);

    connect(this, &ApdApplication::SetTranslatorSafely, this, &ApdApplication::SetTranslator);

#if defined APD_OS_WIN
    Core::OS::Windows::Winrt::Initialize();
#endif

    // pre-load for InitTranslator
    const auto settingsLoadResult = Core::Settings::Load();

    // Apply the saved appearance before constructing any visible widget. System mode keeps
    // following Windows changes; explicit light/dark modes only override the surface brightness.
    auto &theme = Gui::Theme::Manager::Instance();
    theme.SetMode(ToThemeMode(Core::Settings::GetCurrent().appearance_mode));
    theme.ApplyToApplication();

    InitTranslator();

#if defined APD_OS_WIN
    _quickConnectBackend = std::make_shared<Core::QuickConnect::WindowsBackend>();
#else
    _quickConnectBackend = std::make_shared<Core::QuickConnect::NullBackend>();
#endif
    _quickConnect = std::make_unique<Core::QuickConnect::Controller>(_quickConnectBackend);

    _trayIcon = std::make_unique<Gui::TrayIcon>(
        [this] { return GetCurrentLoadedLocaleIndex(); }, *_quickConnect);
    _taskbarStatus = std::make_unique<Gui::TaskbarStatus>();
    _mainWindow = std::make_unique<Gui::MainWindow>();
    _lowAudioLatencyController = std::make_unique<Core::LowAudioLatency::Controller>();
    _autoStartService = Core::AutoStart::CreateAutoStartService();
    _airPodsManager = std::make_unique<Core::AirPods::Manager>(this);

    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");

    ConnectGuiComponents();
    ConnectAirPodsManager();

    Core::Settings::SetApplyObserver(this);

    _mainWindow->Unavailable();
    _trayIcon->Unavailable();
    _taskbarStatus->Unavailable();

    InitSettings(settingsLoadResult);

    return true;
}

void ApdApplication::ConnectAirPodsManager()
{
    auto *manager = _airPodsManager.get();
    auto *mainWindow = _mainWindow.get();

    connect(
        manager, &Core::AirPods::Manager::BoundDeviceUnavailable, this,
        [] {
            auto settings = Core::Settings::ModifiableAccess();
            settings->device_address = 0;
        },
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, mainWindow, &Gui::MainWindow::UpdateState);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, _trayIcon.get(),
        &Gui::TrayIcon::UpdateState);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, _taskbarStatus.get(),
        &Gui::TaskbarStatus::UpdateState);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, mainWindow, &Gui::MainWindow::Disconnect);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, _trayIcon.get(),
        &Gui::TrayIcon::Disconnect);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, _taskbarStatus.get(),
        &Gui::TaskbarStatus::Disconnect);
    connect(
        manager, &Core::AirPods::Manager::ScannerAvailabilityChanged, mainWindow,
        [this, mainWindow](bool available) {
            if (available) {
                mainWindow->Available();
                _trayIcon->Disconnect();
                _taskbarStatus->Disconnect();
            }
            else {
                mainWindow->Unavailable();
                _trayIcon->Unavailable();
                _taskbarStatus->Unavailable();
            }
        });
    connect(
        manager, &Core::AirPods::Manager::DeviceConnectionChanged, _lowAudioLatencyController.get(),
        &Core::LowAudioLatency::Controller::SetDeviceConnectedSafely);
    connect(manager, &Core::AirPods::Manager::LidToggled, mainWindow, [mainWindow](bool opened) {
        if (opened) {
            emit mainWindow->ShowSafely();
        }
        else {
            emit mainWindow->HideSafely();
        }
    });
}

void ApdApplication::ConnectGuiComponents()
{
    connect(
        _trayIcon.get(), &Gui::TrayIcon::ShowMainWindowRequested, _mainWindow.get(),
        &Gui::MainWindow::show);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::UserUpdateRequested, _mainWindow.get(),
        &Gui::MainWindow::AskUserUpdate);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::ToolTipChanged, _taskbarStatus.get(),
        &Gui::TaskbarStatus::SetToolTip);

    connect(
        _taskbarStatus.get(), &Gui::TaskbarStatus::ShowMainWindowRequested, _mainWindow.get(),
        &Gui::MainWindow::show);
    connect(
        _taskbarStatus.get(), &Gui::TaskbarStatus::ShowTrayMenuRequested, _trayIcon.get(),
        &Gui::TrayIcon::ShowContextMenu);

    connect(
        _mainWindow.get(), &Gui::MainWindow::SilentUpdateAvailable, _trayIcon.get(),
        &Gui::TrayIcon::VersionUpdateAvailable);
}

int ApdApplication::Run()
{
    _airPodsManager->StartScanner();
    return exec();
}

void ApdApplication::SetTranslator(const QLocale &locale)
{
    const auto localeName = locale.name();

    LOG(Info, "SetTranslator() locale: {}", localeName);

    if (locale.language() == QLocale::C) {
        LOG(Warn, "Try to set a possibly invalid locale name '{}', ignore", localeName);
        return;
    }

    const auto &availableLocales = Utils::AvailableLocales();

    int index = -1;
    for (int i = 0; i < availableLocales.size(); ++i) {
        if (availableLocales.at(i).name() == localeName) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        LOG(Warn, "Try to set a untranslated language. locale name '{}', ignore", localeName);
        return;
    }

    if (_currentLoadedLocaleIndex == index) {
        LOG(Warn, "Try to set a same locale name '{}', ignore", localeName);
        return;
    }

    QDir translationFolder = QCoreApplication::applicationDirPath();
    translationFolder.cd("translations");
    _translator.load(locale, "apd", "_", translationFolder.absolutePath());

    installTranslator(&_translator);

    _currentLoadedLocaleIndex = index;
}

void ApdApplication::InitTranslator()
{
    LOG(Info, "currentLocale: {}", QLocale{}.name());

    const auto &localeFromSettings = Core::Settings::GetCurrent().language_locale;

    LOG(Info, "Locale from settings: '{}'", localeFromSettings);

    SetTranslator(localeFromSettings.isEmpty() ? QLocale{} : QLocale{localeFromSettings});
}

void ApdApplication::QuitSafely()
{
    Utils::Qt::QuitApplicationSafely();
}

void ApdApplication::OnLanguageLocaleChanged(const QLocale &locale)
{
    emit SetTranslatorSafely(locale);
}

void ApdApplication::OnAppearanceModeChanged(Core::Settings::AppearanceMode mode)
{
    Gui::Theme::Manager::Instance().SetMode(ToThemeMode(mode));
}

void ApdApplication::OnAutoRunChanged(bool enable)
{
    _autoStartService->SetEnabled(enable);
}

void ApdApplication::OnLowAudioLatencyChanged(bool enable)
{
    _lowAudioLatencyController->ControlSafely(enable);
    _trayIcon->OnLowAudioLatencyChangedSafely(enable);
}

void ApdApplication::OnAutomaticEarDetectionChanged(bool enable)
{
    _airPodsManager->OnAutomaticEarDetectionChanged(enable);
}

void ApdApplication::OnRssiMinChanged(int16_t rssiMin)
{
    _airPodsManager->OnRssiMinChanged(rssiMin);
}

void ApdApplication::OnDeviceAddressChanged(uint64_t address)
{
    if (address == 0) {
        emit _mainWindow->UnbindSafely();
        _trayIcon->Unbind();
    }
    else {
        emit _mainWindow->BindSafely();
        _trayIcon->Disconnect();
        _taskbarStatus->Disconnect();
    }

    // Queued rather than direct: this observer runs from `~ModifiableSafeAccessor`, which still
    // holds the settings mutex until it finishes. `OnBoundDeviceAddressChanged` joins the device
    // lookup thread, and `request_stop()` cannot interrupt the WinRT enumeration it is blocked
    // on, so calling it here would hold the settings mutex for the length of that enumeration
    // and stall every other thread touching settings.
    QMetaObject::invokeMethod(
        _airPodsManager.get(),
        [manager = _airPodsManager.get(), address] {
            manager->OnBoundDeviceAddressChanged(address);
        },
        Qt::QueuedConnection);
}

void ApdApplication::OnTrayIconBatteryChanged(Core::Settings::TrayIconBatteryBehavior behavior)
{
    emit _trayIcon->OnTrayIconBatteryChangedSafely(behavior);
}

void ApdApplication::OnTrayQuickConnectEnabledChanged(bool enable)
{
    _quickConnect->SetEnabled(enable);
}

void ApdApplication::OnTrayQuickConnectDeviceChanged(const QString &deviceId)
{
    _quickConnect->SetDeviceId(deviceId);
}

void ApdApplication::OnTaskbarBatteryChanged(Core::Settings::TaskbarStatusBehavior behavior)
{
    emit _taskbarStatus->OnSettingsChangedSafely(behavior);
}
