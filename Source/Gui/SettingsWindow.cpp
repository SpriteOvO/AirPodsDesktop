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


using namespace std::chrono_literals;

namespace Gui
{
    SettingsWindow::SettingsWindow(QWidget *parent) :
        QDialog{parent}
    {
        _ui.setupUi(this);

        LoadCurrent();

        connect(_ui.buttonBox, &QDialogButtonBox::accepted, this, &SettingsWindow::Save);
        connect(_ui.buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            &SettingsWindow::LoadDefault
        );

        connect(_ui.checkBoxAutoRun, &QCheckBox::toggled, this,
            [this](bool checked) { _data.auto_run = checked; }
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
        _ui.checkBoxAutoRun->setChecked(_data.auto_run);
    }

    void SettingsWindow::showEvent(QShowEvent *event)
    {
        LoadCurrent();
    }

} // namespace Gui
