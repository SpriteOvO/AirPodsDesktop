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

#include "SettingsWindow.h"

#include <QLabel>
#include <QToolTip>
#include <QCheckBox>
#include <QPushButton>
#include <QMessageBox>

#include <Config.h>

using namespace std::chrono_literals;

namespace Gui {

class TipLabel : public QLabel
{
    Q_OBJECT

private:
    constexpr static auto content = "(?)";

public:
    TipLabel(QString text, QWidget *parent) : QLabel{content, parent}, _text{std::move(text)}
    {
        QPalette palette = this->palette();
        palette.setColor(QPalette::WindowText, Qt::darkGray);
        setPalette(palette);
    }

private:
    QString _text;

    void enterEvent(QEvent *event) override
    {
        QToolTip::showText(QCursor::pos(), _text, this);
    }

    void leaveEvent(QEvent *event) override
    {
        QToolTip::hideText();
    }
};

SettingsWindow::SettingsWindow(QWidget *parent) : QDialog{parent}
{
    _ui.setupUi(this);

    auto versionText = QString{"<a href=\"%1\">v%2</a>"}
            .arg("https://github.com/SpriteOvO/AirPodsDesktop/releases/tag/" CONFIG_VERSION_STRING)
                           .arg(CONFIG_VERSION_STRING);
#if defined APD_BUILD_GIT_HASH
    versionText +=
        QString{" (<a href=\"%1\">%2</a>)"}
            .arg("https://github.com/SpriteOvO/AirPodsDesktop/commit/" APD_BUILD_GIT_HASH)
            .arg(QString{APD_BUILD_GIT_HASH}.left(7));
#endif
    _ui.lbVersion->setText(versionText);

    _ui.hlLowAudioLatency->addWidget(new TipLabel{
        tr("It improves the problem of short audio not playing, but may increase "
           "battery consumption."),
        this});

    _ui.hlTipAutoEarDetection->addWidget(new TipLabel{
        tr("Pause the media when you remove the AirPods and play it when both "
           "AirPods are put back on."),
        this});

    _ui.hsVolumeLevel->setMinimum(0);
    _ui.hsVolumeLevel->setMaximum(100);

    _ui.hsMaxReceivingRange->setMinimum(50);
    _ui.hsMaxReceivingRange->setMaximum(100);

    Update(Core::Settings::GetCurrent(), false);

    _ui.lbVolumeLevel->setText(tr("Volume level %1").arg(_ui.hsVolumeLevel->value()));

    connect(
        _ui.buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
        &SettingsWindow::RestoreDefaults);

    connect(_ui.cbAutoRun, &QCheckBox::toggled, this, [this](bool checked) {
        if (_trigger) {
            On_cbAutoRun_toggled(checked);
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

    connect(_ui.cbReduceLoudSounds, &QCheckBox::toggled, this, [this](bool checked) {
        if (_trigger) {
            On_cbReduceLoudSounds_toggled(checked);
        }
    });

    connect(_ui.hsVolumeLevel, &QSlider::valueChanged, this, [this](int value) {
        if (_trigger) {
            On_hsVolumeLevel_valueChanged(value);
        }
    });

    connect(_ui.hsMaxReceivingRange, &QSlider::valueChanged, this, [this](int value) {
        if (_trigger) {
            On_hsMaxReceivingRange_valueChanged(value);
        }
    });

    connect(_ui.cbDisplayBatteryOnTrayIcon, &QCheckBox::toggled, this, [this](bool checked) {
        if (_trigger) {
            On_cbDisplayBatteryOnTrayIcon_toggled(checked);
        }
    });

    connect(_ui.pbUnbind, &QPushButton::clicked, this, [this]() {
        if (_trigger) {
            On_pbUnbind_clicked();
        }
    });

    connect(_ui.pbShowLogFileLocation, &QPushButton::clicked, this, [this]() {
        if (_trigger) {
            On_pbShowLogFileLocation_clicked();
        }
    });
}

void SettingsWindow::RestoreDefaults()
{
    Core::Settings::Save(Core::Settings::GetDefault());
    Update(Core::Settings::GetCurrent(), false);
}

void SettingsWindow::Update(const Core::Settings::Fields &fields, bool trigger)
{
    _trigger = trigger;

    _ui.cbAutoRun->setChecked(fields.auto_run);

    _ui.cbLowAudioLatency->setChecked(fields.low_audio_latency);

    _ui.cbAutoEarDetection->setChecked(fields.automatic_ear_detection);

    _ui.cbReduceLoudSounds->setChecked(fields.reduce_loud_sounds);
    if (fields.loud_volume_level > kSliderVolumeLevelAlertValue) {
        _sliderEnableVolumeLevelWarning = false;
    }
    _ui.hsVolumeLevel->setValue(fields.loud_volume_level);

    _ui.hsMaxReceivingRange->setValue(-fields.rssi_min);

    _ui.cbDisplayBatteryOnTrayIcon->setChecked(fields.tray_icon_battery);

    _ui.pbUnbind->setDisabled(fields.device_address == 0);

    _trigger = true;
}

void SettingsWindow::showEvent(QShowEvent *event)
{
    Update(Core::Settings::GetCurrent(), false);
}

void SettingsWindow::On_cbAutoRun_toggled(bool checked)
{
    Core::Settings::ModifiableAccess()->auto_run = checked;
}

void SettingsWindow::On_pbUnbind_clicked()
{
    _ui.pbUnbind->setDisabled(true);
    Core::Settings::ModifiableAccess()->device_address = 0;
}

void SettingsWindow::On_cbDisplayBatteryOnTrayIcon_toggled(bool checked)
{
    Core::Settings::ModifiableAccess()->tray_icon_battery = checked;
}

void SettingsWindow::On_cbLowAudioLatency_toggled(bool checked)
{
    Core::Settings::ModifiableAccess()->low_audio_latency = checked;
}

void SettingsWindow::On_cbAutoEarDetection_toggled(bool checked)
{
    Core::Settings::ModifiableAccess()->automatic_ear_detection = checked;
}

void SettingsWindow::On_cbReduceLoudSounds_toggled(bool checked)
{
    Core::Settings::ModifiableAccess()->reduce_loud_sounds = checked;
}

void SettingsWindow::On_hsVolumeLevel_valueChanged(int value)
{
    if (_sliderEnableVolumeLevelWarning && value > kSliderVolumeLevelAlertValue) {
        auto button = QMessageBox::warning(
            this, Config::ProgramName,
            tr("Further increases in the volume level may harm your ears.\n"
               "\n"
               "Click \"Ignore\" if you want to disable the warning and limitation."),
            QMessageBox::Ok | QMessageBox::Ignore, QMessageBox::Ok);
        if (button == QMessageBox::Ignore) {
            _sliderEnableVolumeLevelWarning = false;
        }
        else {
            _ui.hsVolumeLevel->setValue(kSliderVolumeLevelAlertValue);
            return;
        }
    }

    _ui.lbVolumeLevel->setText(tr("Volume level %1").arg(value));
    Core::Settings::ModifiableAccess()->loud_volume_level = value;
}

void SettingsWindow::On_hsMaxReceivingRange_valueChanged(int value)
{
    Core::Settings::ModifiableAccess()->rssi_min = -value;
}

void SettingsWindow::On_pbShowLogFileLocation_clicked()
{
    Utils::File::ShowFileLocation(Logger::GetLogFilePath());
}

} // namespace Gui

#include "SettingsWindow.moc"
