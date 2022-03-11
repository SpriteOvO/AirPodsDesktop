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

#pragma once

#include <QLabel>
#include <QDialog>
#include <QSlider>
#include <QCheckBox>

#include "../Core/Settings.h"
#include "../Utils.h"

#include "ui_SettingsWindow.h"

namespace Gui {

using namespace Core::Settings;

class SettingsWindow : public QDialog
{
    Q_OBJECT

public:
    SettingsWindow(QWidget *parent = nullptr);

    int GetTabCount() const;
    int GetTabCurrentIndex() const;
    void SetTabIndex(int index);

private:
    Ui::SettingsWindow _ui;
    bool _trigger{true};
    int _lastLanguageIndex{0};

    void InitCreditsText();
    void RestoreDefaults();
    void Update(const Fields &fields, bool trigger);

    void showEvent(QShowEvent *event) override;

    // General
    void On_cbLanguages_currentIndexChanged(int index);
    void On_cbAutoRun_toggled(bool checked);
    void On_pbUnbind_clicked();

    // Visual
    void On_cbDisplayBatteryOnTrayIcon_toggled(TrayIconBatteryBehavior behavior);
    void On_cbDisplayBatteryOnTaskbar_toggled(TaskbarStatusBehavior behavior);

    // Features
    void On_cbLowAudioLatency_toggled(bool checked);
    void On_cbAutoEarDetection_toggled(bool checked);
    void On_hsMaxReceivingRange_valueChanged(int value);

    // About
    void On_pbOpenLogsDirectory_clicked();

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
    UTILS_QT_REGISTER_LANGUAGECHANGE(QDialog, [this] {
        _ui.retranslateUi(this);
        InitCreditsText();
    });
};
} // namespace Gui
