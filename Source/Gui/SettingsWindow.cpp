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

#include "SettingsWindow.h"

#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QStyledItemDelegate>

#include "Theme.h"
#include <QCheckBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDesktopServices>

#include <Config.h>

#include "../Core/Debug.h"

using namespace std::chrono_literals;

namespace Gui {

SettingsWindow::SettingsWindow(
    std::function<int()> getCurrentLocaleIndex, Core::QuickConnect::Controller &quickConnect,
    QWidget *parent)
    : QDialog{parent}, _getCurrentLocaleIndex{std::move(getCurrentLocaleIndex)},
      _quickConnect{quickConnect}
{
    _ui.setupUi(this);

    // Navigation pane + stacked pages replace the former tab widget. The last page is Debug.
    const auto debugPageIndex = _ui.navList->count() - 1;
    APD_ASSERT(debugPageIndex == _ui.pages->count() - 1);
    APD_ASSERT(_ui.navList->item(debugPageIndex)->text() == "Debug");

    connect(
        _ui.navList, &QListWidget::currentRowChanged, _ui.pages, &QStackedWidget::setCurrentIndex);
    _ui.navList->setCurrentRow(0);

    for (const auto &[label, checkBox] : {
             std::pair{_ui.lbLowAudioLatency, _ui.cbLowAudioLatency},
             std::pair{_ui.lbAutoEarDetection, _ui.cbAutoEarDetection},
             std::pair{_ui.lbTrayQuickConnect, _ui.cbTrayQuickConnectEnabled},
         })
    {
        label->setBuddy(checkBox);
        checkBox->setAccessibleName(label->text());
    }

    // The default combo popup delegate ignores stylesheet item padding; the styled one honours it.
    _ui.cbLanguages->setItemDelegate(new QStyledItemDelegate{_ui.cbLanguages});
    _ui.cbAppearanceMode->setItemDelegate(new QStyledItemDelegate{_ui.cbAppearanceMode});
    _ui.cbTrayQuickConnectDevice->setItemDelegate(
        new QStyledItemDelegate{_ui.cbTrayQuickConnectDevice});
    _ui.lbAppearance->setBuddy(_ui.cbAppearanceMode);
    _ui.cbAppearanceMode->setAccessibleName(_ui.lbAppearance->text());

    // Neither is the dialog's default action; without this the first one would be drawn as the
    // accent-coloured default button.
    _ui.pbUnbind->setAutoDefault(false);
    _ui.pbOpenLogsDirectory->setAutoDefault(false);
    for (auto *button : _ui.buttonBox->buttons()) {
        if (auto *pushButton = qobject_cast<QPushButton *>(button); pushButton != nullptr) {
            pushButton->setAutoDefault(false);
        }
    }

    _aboutTextTemplate = _ui.label->text();
    UpdateDescriptions();
    UpdateStandardButtonTexts();
    connect(
        &Theme::Manager::Instance(), &Theme::Manager::Changed, this,
        &SettingsWindow::UpdateDescriptions);

#if !defined APD_DEBUG
    _ui.navList->item(debugPageIndex)->setHidden(true);
#else
    connect(
        _ui.cbAdvOverride, &QCheckBox::toggled, this, &SettingsWindow::On_cbAdvOverride_toggled);

    connect(
        _ui.teAdvOverride, &QTextEdit::textChanged, this,
        &SettingsWindow::On_teAdvOverride_textChanged);
#endif

    InitCreditsText();

    auto versionText =
        QString{"<a href=\"%1\">v%2</a>"}.arg(Config::UrlCurrentRelease).arg(CONFIG_VERSION_STRING);
#if defined APD_BUILD_GIT_HASH
    versionText +=
        QString{" (<a href=\"%1\">%2</a>)"}
            .arg(QString{"%1/commit/%2"}.arg(Config::UrlRepository).arg(APD_BUILD_GIT_HASH))
            .arg(QString{APD_BUILD_GIT_HASH}.left(7));
#endif
    _ui.lbVersion->setText(versionText);

    _ui.hsMaxReceivingRange->setMinimum(50);
    _ui.hsMaxReceivingRange->setMaximum(100);

    for (const auto &locale : Utils::AvailableLocales()) {
        const auto displayName = locale.language() == QLocale::English
                                     ? QStringLiteral("English")
                                     : locale.nativeLanguageName();
        _ui.cbLanguages->addItem(displayName);
    }
    _ui.cbLanguages->addItem("...");

    connect(&_quickConnect, &Core::QuickConnect::Controller::DevicesChanged, this, [this] {
        UpdateQuickConnectDevices(GetCurrent());
    });

    Update(GetCurrent(), false);

    connect(
        _ui.buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
        &SettingsWindow::RestoreDefaults);

    connect(
        _ui.cbLanguages, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (_trigger) {
                On_cbLanguages_currentIndexChanged(index);
            }
        });

    connect(_ui.cbAutoRun, &QCheckBox::toggled, this, [this](bool checked) {
        if (_trigger) {
            On_cbAutoRun_toggled(checked);
        }
    });

    connect(
        _ui.cbAppearanceMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int index) {
            if (_trigger) {
                On_cbAppearanceMode_currentIndexChanged(index);
            }
        });

    connect(_ui.cbTrayQuickConnectEnabled, &QCheckBox::toggled, this, [this](bool checked) {
        if (_trigger) {
            On_cbTrayQuickConnectEnabled_toggled(checked);
        }
    });

    connect(
        _ui.cbTrayQuickConnectDevice, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int index) {
            if (_trigger) {
                On_cbTrayQuickConnectDevice_currentIndexChanged(index);
            }
        });

    connect(_ui.cbLowAudioLatency, &QCheckBox::toggled, this, [this](bool checked) {
        if (_trigger) {
            On_cbLowAudioLatency_toggled(checked);
        }
    });

    connect(_ui.cbAutoEarDetection, &QCheckBox::toggled, this, [this](bool checked) {
        if (_trigger) {
            On_cbAutoEarDetection_toggled(checked);
        }
    });

    connect(_ui.hsMaxReceivingRange, &QSlider::valueChanged, this, [this](int value) {
        if (_trigger) {
            On_hsMaxReceivingRange_valueChanged(value);
        }
    });

    connect(
        _ui.rbDisplayBatteryOnTrayIconDisable, &QRadioButton::toggled, this, [this](bool checked) {
            if (_trigger) {
                On_cbDisplayBatteryOnTrayIcon_toggled(TrayIconBatteryBehavior::Disable);
            }
        });

    connect(
        _ui.rbDisplayBatteryOnTrayIconWhenLowBattery, &QRadioButton::toggled, this,
        [this](bool checked) {
            if (_trigger) {
                On_cbDisplayBatteryOnTrayIcon_toggled(TrayIconBatteryBehavior::WhenLowBattery);
            }
        });

    connect(
        _ui.rbDisplayBatteryOnTrayIconAlways, &QRadioButton::toggled, this, [this](bool checked) {
            if (_trigger) {
                On_cbDisplayBatteryOnTrayIcon_toggled(TrayIconBatteryBehavior::Always);
            }
        });

    connect(
        _ui.rbDisplayBatteryOnTaskbarDisable, &QRadioButton::toggled, this, [this](bool checked) {
            if (_trigger) {
                On_cbDisplayBatteryOnTaskbar_toggled(TaskbarStatusBehavior::Disable);
            }
        });

    connect(_ui.rbDisplayBatteryOnTaskbarText, &QRadioButton::toggled, this, [this](bool checked) {
        if (_trigger) {
            On_cbDisplayBatteryOnTaskbar_toggled(TaskbarStatusBehavior::Text);
        }
    });

    connect(_ui.rbDisplayBatteryOnTaskbarIcon, &QRadioButton::toggled, this, [this](bool checked) {
        if (_trigger) {
            On_cbDisplayBatteryOnTaskbar_toggled(TaskbarStatusBehavior::Icon);
        }
    });

    connect(_ui.pbUnbind, &QPushButton::clicked, this, [this]() {
        if (_trigger) {
            On_pbUnbind_clicked();
        }
    });

    connect(_ui.pbOpenLogsDirectory, &QPushButton::clicked, this, [this]() {
        if (_trigger) {
            On_pbOpenLogsDirectory_clicked();
        }
    });
}

int SettingsWindow::GetTabCount() const
{
    return _ui.pages->count();
}

void SettingsWindow::SetLowAudioLatencyChecked(bool checked)
{
    const auto oldTrigger = _trigger;
    _trigger = false;
    _ui.cbLowAudioLatency->setChecked(checked);
    _trigger = oldTrigger;
}

int SettingsWindow::GetTabCurrentIndex() const
{
    return _ui.pages->currentIndex();
}

int SettingsWindow::GetTabLastVisibleIndex() const
{
    return GetTabCount() - 2 /* Skip the Debug tab */;
}

void SettingsWindow::SetTabIndex(int index)
{
    _ui.navList->setCurrentRow(index);
}

void SettingsWindow::UpdateDescriptions()
{
    // The setting descriptions live in `Core::Settings` (as `Impl::Desc`) and are translated
    // there, so they are re-read on every language change.
    const auto &constMetaFields = GetConstMetaFields();

    _ui.lbDescLowAudioLatency->setText(constMetaFields.low_audio_latency.Description());
    _ui.lbDescAutoEarDetection->setText(constMetaFields.automatic_ear_detection.Description());
    _ui.lbDescTrayQuickConnect->setText(constMetaFields.tray_quick_connect_enabled.Description());

    // The About text carries a hard-coded link colour inside the (translated) HTML; swap it
    // for the theme accent so it stays readable in dark mode.
    const auto accent = Theme::Manager::Instance().Accent().name(QColor::HexRgb);
    _ui.label->setText(QString{_aboutTextTemplate}.replace("#0000ff", accent, Qt::CaseInsensitive));
}

void SettingsWindow::UpdateStandardButtonTexts()
{
    _ui.buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Restore Defaults"));
    _ui.buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));
}

void SettingsWindow::InitCreditsText()
{
    //: To credit translators, you can leave your name here if you wish.
    //: (Sorted by time added, separated by "|" character, only single "|" for empty)
    auto l10nContributorsStr = tr("|");
    if (!l10nContributorsStr.isEmpty()) {
        auto l10nContributors = l10nContributorsStr.split('|', Qt::SkipEmptyParts);
        if (l10nContributors.isEmpty()) {
            l10nContributorsStr.clear();
        }
        else {
            l10nContributorsStr = tr("Translation Contributors:");
            for (const auto &contributor : l10nContributors) {
                l10nContributorsStr += "<br> - " + contributor.trimmed();
            }
            l10nContributorsStr += "<br><br>";
        }
    }

    static QString libs = [] {
        struct LibInfo {
            const char *name, *url, *license, *licenseUrl;
        };
        static std::vector<LibInfo> libs{
            // clang-format off
            { "Qt 5", "https://www.qt.io/download-qt-installer", "LGPLv3", "https://doc.qt.io/qt-5/lgpl.html" },
            { "spdlog", "https://github.com/gabime/spdlog", "MIT", "https://github.com/gabime/spdlog/blob/v1.x/LICENSE" },
            { "cxxopts", "https://github.com/jarro2783/cxxopts", "MIT", "https://github.com/jarro2783/cxxopts/blob/master/LICENSE" },
            { "cpr", "https://github.com/whoshuu/cpr", "MIT", "https://github.com/whoshuu/cpr/blob/master/LICENSE" },
            { "json", "https://github.com/nlohmann/json", "MIT", "https://github.com/nlohmann/json/blob/develop/LICENSE.MIT" },
            { "SingleApplication", "https://github.com/itay-grudev/SingleApplication", "MIT", "https://github.com/itay-grudev/SingleApplication/blob/master/LICENSE" },
            { "pfr", "https://github.com/boostorg/pfr", "BSL-1.0", "https://github.com/boostorg/pfr/blob/develop/LICENSE_1_0.txt" },
            { "magic_enum", "https://github.com/Neargye/magic_enum", "MIT", "https://github.com/Neargye/magic_enum/blob/master/LICENSE" },
            { "stacktrace", "https://github.com/boostorg/stacktrace", "BSL-1.0", "https://www.boost.org/LICENSE_1_0.txt" } // clang-format on
        };

        QString result;
        for (const auto &lib : libs) {
            result += QString{"<br> - <a href=\"%2\">%1</a> (<a href=\"%4\">%3 License</a>)"}
                          .arg(lib.name)
                          .arg(lib.url)
                          .arg(lib.license)
                          .arg(lib.licenseUrl);
        }
        return result;
    }();
    auto libsStr = tr("Third-Party Libraries:") + libs;

    _ui.tbCredits->setHtml(l10nContributorsStr + libsStr);
}

void SettingsWindow::RestoreDefaults()
{
    Save(GetDefault());
    Update(GetCurrent(), false);
}

void SettingsWindow::Update(const Fields &fields, bool trigger)
{
    _trigger = trigger;

    auto currentLangIndex = _getCurrentLocaleIndex();
    _lastLanguageIndex = currentLangIndex;
    _ui.cbLanguages->setCurrentIndex(currentLangIndex);

    _ui.cbAutoRun->setChecked(fields.auto_run);

    _ui.cbAppearanceMode->setCurrentIndex(static_cast<int>(fields.appearance_mode));

    _ui.cbLowAudioLatency->setChecked(fields.low_audio_latency);

    _ui.cbAutoEarDetection->setChecked(fields.automatic_ear_detection);

    _ui.hsMaxReceivingRange->setValue(-fields.rssi_min);

    auto [batteryOnTrayIconDisable, batteryOnTrayIconWhenLowBattery, batteryOnTrayIconAlways] =
        std::make_tuple(
            fields.tray_icon_battery == TrayIconBatteryBehavior::Disable,
            fields.tray_icon_battery == TrayIconBatteryBehavior::WhenLowBattery,
            fields.tray_icon_battery == TrayIconBatteryBehavior::Always);

    _ui.rbDisplayBatteryOnTrayIconDisable->setChecked(batteryOnTrayIconDisable);
    _ui.rbDisplayBatteryOnTrayIconWhenLowBattery->setChecked(batteryOnTrayIconWhenLowBattery);
    _ui.rbDisplayBatteryOnTrayIconAlways->setChecked(batteryOnTrayIconAlways);

    auto [batteryOnTaskbarDisable, batteryOnTaskbarText, batteryOnTaskbarIcon] = std::make_tuple(
        fields.battery_on_taskbar == TaskbarStatusBehavior::Disable,
        fields.battery_on_taskbar == TaskbarStatusBehavior::Text,
        fields.battery_on_taskbar == TaskbarStatusBehavior::Icon);

    _ui.rbDisplayBatteryOnTaskbarDisable->setChecked(batteryOnTaskbarDisable);
    _ui.rbDisplayBatteryOnTaskbarText->setChecked(batteryOnTaskbarText);
    _ui.rbDisplayBatteryOnTaskbarIcon->setChecked(batteryOnTaskbarIcon);

    _ui.pbUnbind->setDisabled(fields.device_address == 0);

    UpdateQuickConnectDevices(fields);

    _trigger = true;
}

void SettingsWindow::UpdateQuickConnectDevices(const Fields &fields)
{
    const auto previousTrigger = _trigger;
    _trigger = false;

    const auto &selectedId = fields.tray_quick_connect_device_id;

    _ui.cbTrayQuickConnectDevice->clear();
    for (const auto &device : _quickConnect.Devices()) {
        _ui.cbTrayQuickConnectDevice->addItem(device.name, device.id);
    }

    auto selectedIndex = _ui.cbTrayQuickConnectDevice->findData(selectedId);
    if (selectedIndex == -1 && !selectedId.isEmpty()) {
        // The saved device is paired but not reporting an audio endpoint right now - switched off,
        // out of range, or the enumeration has not finished. Show it as chosen but unavailable
        // rather than an empty box that reads like nothing was ever configured.
        _ui.cbTrayQuickConnectDevice->addItem(tr("Saved device (not available)"), selectedId);
        selectedIndex = _ui.cbTrayQuickConnectDevice->count() - 1;
    }
    _ui.cbTrayQuickConnectDevice->setCurrentIndex(selectedIndex);

    _ui.cbTrayQuickConnectEnabled->setChecked(fields.tray_quick_connect_enabled);
    // Always leave the checkbox usable: a transient empty enumeration must never trap the user in
    // a state they cannot turn off.
    _ui.cbTrayQuickConnectEnabled->setEnabled(true);
    _ui.cbTrayQuickConnectDevice->setEnabled(_ui.cbTrayQuickConnectDevice->count() > 0);

    _trigger = previousTrigger;
}

void SettingsWindow::UpdateAdvOverride()
{
    auto advsStr = _ui.teAdvOverride->toPlainText();
    auto vAdvsStr = advsStr.split('\n', QString::SkipEmptyParts);

    std::vector<std::vector<uint8_t>> advs;

    for (const auto &advStr : vAdvsStr) {

        auto advBytesStr = advStr.split(' ', QString::SkipEmptyParts);

        std::vector<uint8_t> bytes;
        for (const auto advByteStr : advBytesStr) {
            bool success{false};
            auto byte = advByteStr.toUInt(&success, 16);
            APD_ASSERT(success);
            bytes.emplace_back(byte);
        }

        advs.emplace_back(std::move(bytes));
    }

    Core::Debug::DebugConfig::GetInstance().UpdateAdvOverride(
        _ui.cbAdvOverride->isChecked(), std::move(advs));
}

void SettingsWindow::showEvent(QShowEvent *event)
{
    Update(GetCurrent(), false);

    // Enumerating paired Bluetooth devices and every audio endpoint is not free, so it happens
    // when the dialog is opened rather than on every application start.
    _quickConnect.RefreshDevices();
}

void SettingsWindow::On_cbLanguages_currentIndexChanged(int index)
{
    if (_ui.cbLanguages->count() != index + 1) {
        _lastLanguageIndex = index;

        const auto &availableLocales = Utils::AvailableLocales();
        const auto &locale = availableLocales.at(index);

        ModifiableAccess()->language_locale = locale.name();
    }
    else {
        _ui.cbLanguages->setCurrentIndex(_lastLanguageIndex);
        // clang-format off
        QDesktopServices::openUrl(QUrl{
            "https://github.com/SpriteOvO/AirPodsDesktop/blob/main/CONTRIBUTING.md#-translation-guide"
        });
        // clang-format on
    }
}

void SettingsWindow::On_cbAutoRun_toggled(bool checked)
{
    ModifiableAccess()->auto_run = checked;
}

void SettingsWindow::On_cbTrayQuickConnectEnabled_toggled(bool checked)
{
    ModifiableAccess()->tray_quick_connect_enabled = checked;
}

void SettingsWindow::On_cbTrayQuickConnectDevice_currentIndexChanged(int index)
{
    const auto id = _ui.cbTrayQuickConnectDevice->itemData(index).toString();
    ModifiableAccess()->tray_quick_connect_device_id = id;
}

void SettingsWindow::On_pbUnbind_clicked()
{
    _ui.pbUnbind->setDisabled(true);
    ModifiableAccess()->device_address = 0;
}

void SettingsWindow::On_cbDisplayBatteryOnTrayIcon_toggled(TrayIconBatteryBehavior behavior)
{
    ModifiableAccess()->tray_icon_battery = behavior;
}

void SettingsWindow::On_cbAppearanceMode_currentIndexChanged(int index)
{
    if (index < static_cast<int>(AppearanceMode::System) ||
        index > static_cast<int>(AppearanceMode::Dark))
    {
        return;
    }
    ModifiableAccess()->appearance_mode = static_cast<AppearanceMode>(index);
}

void SettingsWindow::On_cbDisplayBatteryOnTaskbar_toggled(TaskbarStatusBehavior behavior)
{
    ModifiableAccess()->battery_on_taskbar = behavior;
}

void SettingsWindow::On_cbLowAudioLatency_toggled(bool checked)
{
    ModifiableAccess()->low_audio_latency = checked;
}

void SettingsWindow::On_cbAutoEarDetection_toggled(bool checked)
{
    ModifiableAccess()->automatic_ear_detection = checked;
}

void SettingsWindow::On_hsMaxReceivingRange_valueChanged(int value)
{
    ModifiableAccess()->rssi_min = -value;
}

void SettingsWindow::On_pbOpenLogsDirectory_clicked()
{
    Utils::File::OpenFileLocation(Logger::GetLogFilePath());
}

void SettingsWindow::On_cbAdvOverride_toggled(bool checked)
{
    UpdateAdvOverride();
}

void SettingsWindow::On_teAdvOverride_textChanged()
{
    UpdateAdvOverride();
}

} // namespace Gui
