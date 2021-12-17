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
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;

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

    connect(this, &ApdApplication::SetTranslatorSafely, this, &ApdApplication::SetTranslator);

    // pre-load for InitTranslator
    const auto settingsLoadResult = Core::Settings::Load();

    InitTranslator();

    QFont font;
    font.setFamily("Segoe UI");
    font.setFamilies({"Segoe UI Variable", "Segoe UI", "Microsoft YaHei UI"});
    font.setPointSize(9);

    setFont(font);
    setWindowIcon(QIcon{Config::QrcIconSvg});
    setQuitOnLastWindowClosed(false);

#if defined APD_OS_WIN
    Core::OS::Windows::Winrt::Initialize();
#endif

    _trayIcon = std::make_unique<Gui::TrayIcon>();
    _mainWindow = std::make_unique<Gui::MainWindow>();
    _lowAudioLatencyController = std::make_unique<Core::LowAudioLatency::Controller>();

    InitSettings(settingsLoadResult);

    return true;
}

int ApdApplication::Run()
{
    _mainWindow->GetApdMgr().StartScanner();
    return exec();
}

const QVector<QLocale> &ApdApplication::AvailableLocales()
{
    static QVector<QLocale> locales = []() {
        const auto localeNames = QString{Config::TranslationLocales}.split(';', Qt::SkipEmptyParts);

        QVector<QLocale> result = {QLocale{"en"}};

        for (const auto &localName : localeNames) {
            QLocale locale{localName};

            if (locale.language() == QLocale::C) {
                LOG(Warn, "Possibly invalid locale name '{}', ignore", localName);
                continue;
            }

            result.push_back(locale);
        }

        return result;
    }();

    return locales;
}

void ApdApplication::SetTranslator(const QLocale &locale)
{
    const auto localeName = locale.name();

    LOG(Info, "SetTranslator() locale: {}", localeName);

    if (locale.language() == QLocale::C) {
        LOG(Warn, "Try to set a possibly invalid locale name '{}', ignore", localeName);
        return;
    }

    const auto &availableLocales = ApdApplication::AvailableLocales();

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
    QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
}
