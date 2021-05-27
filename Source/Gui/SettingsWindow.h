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

#pragma once

#include <QDialog>

#include "../Core/Settings.h"
#include "../Utils.h"
#include "ui_SettingsWindow.h"


namespace Gui
{
    class SettingsWindow : public QDialog
    {
        Q_OBJECT

    public:
        SettingsWindow(QWidget *parent = nullptr);

    private:
        Ui::SettingsWindow _ui;

        QCheckBox *_checkBoxAutoRun;
        QCheckBox *_checkBoxLowAudioLatency;

        Core::Settings::Data _data;

        void InitUi();

        void LoadCurrent();
        void LoadDefault();
        void Save();

        void Update();

        void showEvent(QShowEvent *event) override;

        UTILS_QT_DISABLE_ESC_QUIT(SettingsWindow, QDialog);
    };

} // namespace Gui
