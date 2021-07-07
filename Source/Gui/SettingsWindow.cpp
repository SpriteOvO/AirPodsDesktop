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

#include <QPushButton>
#include <QCheckBox>
#include <QToolTip>
#include <QLabel>


using namespace std::chrono_literals;

namespace Gui
{
    class TipLabel : public QLabel
    {
        Q_OBJECT

    private:
        constexpr static auto content = "(?)";

    public:
        TipLabel(QString text, QWidget *parent) :
            QLabel{content, parent},
            _text{std::move(text)}
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


    SettingsWindow::SettingsWindow(QWidget *parent) :
        QDialog{parent}
    {
        _ui.setupUi(this);

        CreateUi();
        ConnectUi();

        LoadCurrent();
    }

    void SettingsWindow::CreateUi()
    {
        _ui.gridLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        int row = -1, column = 0;

        const auto &newLine = [&](QWidget *widget, Qt::Alignment alignment = {}) {
            row += 1;
            column = 0;
            _ui.gridLayout->addWidget(widget, row, column, alignment);
        };

        const auto &sameLine = [&](QWidget *widget, Qt::Alignment alignment = {}) {
            _ui.gridLayout->addWidget(widget, row, ++column, alignment);
        };

        // Launch when system starts
        //

        _checkBoxAutoRun = new QCheckBox{tr("Launch when system starts"), this};
        newLine(_checkBoxAutoRun);

        // Low audio latency mode
        //

        _checkBoxLowAudioLatency = new QCheckBox{tr("Low audio latency mode"), this};
        newLine(_checkBoxLowAudioLatency);
        sameLine(
            new TipLabel{
                tr("It improves the problem of short audio not playing, but may increase battery consumption."),
                this
            },
            Qt::AlignLeft
        );

        // Automatic ear detection
        //

        _checkBoxAutomaticEarDetection = new QCheckBox{tr("Automatic ear detection"), this};
        newLine(_checkBoxAutomaticEarDetection);
        sameLine(
            new TipLabel{
                tr("Pause the media when you remove the AirPods and play it when both AirPods are put back on."),
                this
            }, Qt::AlignLeft
        );

        // RSSI
        //

        _sliderMaximumReceivingRange = new QSlider{Qt::Horizontal, this};
        newLine(
            new QLabel{tr("Maximum receiving range"), this}
        );
        newLine(_sliderMaximumReceivingRange);

        // Reduce loud sounds
        //

        _checkReduceLoudSounds = new QCheckBox{tr("Reduce loud sounds"), this};
        newLine(_checkReduceLoudSounds);

        _sliderVolumeLevel = new QSlider{Qt::Horizontal, this};
        _labelVolumeLevel = new QLabel{tr("Volume Level"), this};
        newLine(_labelVolumeLevel);
        newLine(_sliderVolumeLevel);
    }

    void SettingsWindow::ConnectUi()
    {
        connect(_ui.buttonBox, &QDialogButtonBox::accepted, this, &SettingsWindow::Save);
        connect(_ui.buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, &SettingsWindow::LoadDefault
        );

        connect(_checkBoxAutoRun, &QCheckBox::toggled, this,
            [this](bool checked) { _data.auto_run = checked; }
        );

        connect(_checkBoxLowAudioLatency, &QCheckBox::toggled, this,
            [this](bool checked) { _data.low_audio_latency = checked; }
        );

        connect(_checkBoxAutomaticEarDetection, &QCheckBox::toggled, this,
            [this](bool checked) { _data.automatic_ear_detection = checked; }
        );

        _sliderMaximumReceivingRange->setMinimum(50);
        _sliderMaximumReceivingRange->setMaximum(100);
        connect(_sliderMaximumReceivingRange, &QSlider::valueChanged, this,
            [this](int value) { _data.rssi_min = -value; }
        );

        connect(_checkReduceLoudSounds, &QCheckBox::toggled, this,
            [this](bool checked) { _data.reduce_loud_sounds = checked; }
        );
        _sliderVolumeLevel->setMinimum(0);
        _sliderVolumeLevel->setMaximum(100);
        connect(_sliderVolumeLevel, &QSlider::valueChanged, this,
            [this](int value)
            {
                if (_sliderEnableVolumeLevelWarning && value > kSliderVolumeLevelAlertValue)
                {
                    auto button = QMessageBox::warning(
                        _sliderVolumeLevel,
                        Config::ProgramName,
                        tr(
                            "Further increases in the volume level may harm your ears.\n"
                            "\n"
                            "Click \"Ignore\" if you want to disable the warning and limitation."
                        ),
                        QMessageBox::Ok | QMessageBox::Ignore,
                        QMessageBox::Ok
                    );
                    if (button == QMessageBox::Ignore) {
                        _sliderEnableVolumeLevelWarning = false;
                    }
                    else {
                        _sliderVolumeLevel->setValue(kSliderVolumeLevelAlertValue - 1);
                        return;
                    }
                }

                _labelVolumeLevel->setText(tr("Volume Level %1").arg(value));
                _data.loud_volume_level = value;
            }
        );
    }

    void SettingsWindow::LoadCurrent()
    {
        _data = Core::Settings::GetCurrent();
        Update();
    }

    void SettingsWindow::LoadDefault()
    {
        _data = Core::Settings::GetDefault();
        Update();
    }

    void SettingsWindow::Save()
    {
        Core::Settings::SaveToCurrentAndLocal(_data);
    }

    void SettingsWindow::Update()
    {
        _checkBoxAutoRun->setChecked(_data.auto_run);

        _checkBoxLowAudioLatency->setChecked(_data.low_audio_latency);

        _checkBoxAutomaticEarDetection->setChecked(_data.automatic_ear_detection);

        _sliderMaximumReceivingRange->setValue(-_data.rssi_min);

        _checkReduceLoudSounds->setChecked(_data.reduce_loud_sounds);
        if (_data.loud_volume_level > kSliderVolumeLevelAlertValue) {
            _sliderEnableVolumeLevelWarning = false;
        }
        _sliderVolumeLevel->setValue(_data.loud_volume_level);
    }

    void SettingsWindow::showEvent(QShowEvent *event)
    {
        LoadCurrent();
    }

} // namespace Gui

#include "SettingsWindow.moc"
