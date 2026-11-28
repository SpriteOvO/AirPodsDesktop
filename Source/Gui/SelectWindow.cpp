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

#include <QFontMetrics>
#include <QPushButton>
#include <QSize>

namespace Gui {

namespace {
// Mirrors the `QListWidget::item` rule in Theme.cpp.
constexpr int kItemMinHeight = 32;
constexpr int kItemVerticalPadding = 8;
} // namespace

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

    // The stylesheet gives items a min-height, but on some Qt versions the view still lays the
    // rows out with the plain font height, so each row is painted taller than the space reserved
    // for it and the rows overlap. Pin an explicit size hint so layout and painting agree, and
    // keep it tall enough for the current UI font.
    const auto metrics = _ui.listWidget->fontMetrics();
    const int rowHeight = qMax(kItemMinHeight, metrics.height() + 2 * kItemVerticalPadding);
    for (int i = 0; i < _ui.listWidget->count(); ++i) {
        _ui.listWidget->item(i)->setSizeHint(QSize{0, rowHeight});
    }

    _ui.listWidget->setCurrentRow(0);
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
