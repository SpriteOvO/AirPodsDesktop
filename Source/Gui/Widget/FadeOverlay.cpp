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

#include "FadeOverlay.h"

#include <algorithm>

#include <QPainter>

namespace Gui::Widget {

FadeOverlay::FadeOverlay(QWidget *parent) : QWidget{parent}
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
}

void FadeOverlay::Begin(QPixmap snapshot, const QRect &region)
{
    _snapshot = std::move(snapshot);
    _snapshotOpacity = 1.0;
    _coverOpacity = 1.0;
    _running = true;
    setGeometry(region);
    raise();
    show();
    update();
}

void FadeOverlay::SetSnapshotOpacity(qreal opacity)
{
    _snapshotOpacity = std::clamp(opacity, 0.0, 1.0);
    update();
}

void FadeOverlay::SetCoverOpacity(qreal opacity)
{
    _coverOpacity = std::clamp(opacity, 0.0, 1.0);
    update();
}

void FadeOverlay::DropSnapshot()
{
    _snapshot = {};
    _snapshotOpacity = 0.0;
    update();
}

void FadeOverlay::Finish()
{
    _running = false;
    _snapshot = {};
    hide();
}

bool FadeOverlay::IsRunning() const
{
    return _running;
}

void FadeOverlay::paintEvent(QPaintEvent *)
{
    if (!_running) {
        return;
    }
    const QColor cover = parentWidget() ? parentWidget()->palette().color(QPalette::Window)
                                        : palette().color(QPalette::Window);

    QPainter painter{this};
    if (_coverOpacity > 0.0) {
        painter.setOpacity(_coverOpacity);
        painter.fillRect(rect(), cover);
    }
    if (!_snapshot.isNull() && _snapshotOpacity > 0.0) {
        painter.setOpacity(_snapshotOpacity);
        painter.drawPixmap(rect(), _snapshot);
    }
}

} // namespace Gui::Widget
