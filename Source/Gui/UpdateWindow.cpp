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

#include "UpdateWindow.h"

#include <QEvent>
#include <QPushButton>

#include "Theme.h"

namespace Gui {

UpdateWindow::UpdateWindow(Core::Update::ReleaseInfo info, QWidget *parent)
    : QDialog{parent}, _info{std::move(info)}
{
    _ui.setupUi(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    _ui.releaseNotes->setPlainText(_info.changeLog);
    connect(_ui.updateButton, &QPushButton::clicked, this, [this] {
        _action = Action::Update;
        accept();
    });
    connect(_ui.skipButton, &QPushButton::clicked, this, [this] {
        _action = Action::Skip;
        accept();
    });
    connect(_ui.laterButton, &QPushButton::clicked, this, &UpdateWindow::reject);
    connect(_ui.viewButton, &QPushButton::clicked, this, [this] { _info.OpenUrl(); });
    connect(&Theme::Manager::Instance(), &Theme::Manager::Changed, this, &UpdateWindow::UpdateText);
    UpdateText();
}

UpdateWindow::Action UpdateWindow::SelectedAction() const
{
    return _action;
}

void UpdateWindow::reject()
{
    _action = Action::Later;
    QDialog::reject();
}

void UpdateWindow::UpdateText()
{
    _ui.currentVersion->setText(
        tr("Current version: %1").arg(Core::Update::GetLocalVersion().toString()));
    _ui.latestVersion->setText(tr("Latest version: %1").arg(_info.version.toString()));
    _ui.preRelease->setVisible(_info.isPreRelease);
    _ui.releaseNotes->setAccessibleName(_ui.notesTitle->text());
    if (_info.changeLog.isEmpty()) {
        _ui.releaseNotes->setPlainText(
            tr("No release notes are available. You can view the release page for details."));
    }
}

void UpdateWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        _ui.retranslateUi(this);
        UpdateText();
    }
    QDialog::changeEvent(event);
}

} // namespace Gui
