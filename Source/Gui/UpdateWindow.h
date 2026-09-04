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

#include <QDialog>

#include "../Core/Update.h"
#include "ui_UpdateWindow.h"

namespace Gui {

class UpdateWindow : public QDialog
{
    Q_OBJECT

public:
    enum class Action { Update, Skip, Later };

    explicit UpdateWindow(Core::Update::ReleaseInfo info, QWidget *parent = nullptr);
    Action SelectedAction() const;

protected:
    void changeEvent(QEvent *event) override;
    void reject() override;

private:
    Ui::UpdateWindow _ui;
    Core::Update::ReleaseInfo _info;
    Action _action{Action::Later};

    void UpdateText();
};

} // namespace Gui
