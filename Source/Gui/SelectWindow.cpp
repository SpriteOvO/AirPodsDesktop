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

#include "SelectWindow.h"

#include <QPushButton>

namespace Gui {

SelectWindow::SelectWindow(const QString &title, const QStringList &items, QWidget *parent)
    : QDialog{parent}
{
    _ui.setupUi(this);

    const auto &connectButton = [this](QDialogButtonBox::StandardButton button) {
        connect(_ui.buttonBox->button(button), &QPushButton::clicked, this, [this, button]() {
            OnButtonClicked(button);
        });
    };

    APD_ASSERT(!items.isEmpty());

    connectButton(QDialogButtonBox::Yes);
    connectButton(QDialogButtonBox::Cancel);

    _ui.label->setText(title);
    _ui.listWidget->addItems(items);
    _ui.listWidget->item(0)->setSelected(true);
}

bool SelectWindow::HasResult() const
{
    return _clickedButton == QDialogButtonBox::Yes;
}

int SelectWindow::GetSeletedIndex() const
{
    return _ui.listWidget->currentRow();
}

void SelectWindow::OnButtonClicked(QDialogButtonBox::StandardButton button)
{
    _clickedButton = button;
}
} // namespace Gui
