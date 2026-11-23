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

#include <QPixmap>
#include <QWidget>

namespace Gui::Widget {

// Fades a region of a window between two arbitrary layouts: snapshot the region, change the
// widgets underneath, then let the snapshot fade out over a cover in the window colour and the
// cover fade out over the new content. The owner drives both opacities, so the region moves in
// step with whatever else is fading (the device picture).
//
class FadeOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit FadeOverlay(QWidget *parent = nullptr);

    // `snapshot` is what `region` (parent coordinates) looked like before the change.
    void Begin(QPixmap snapshot, const QRect &region);
    // 1 = old look fully visible; 0 = only the cover.
    void SetSnapshotOpacity(qreal opacity);
    // 1 = new content hidden behind the cover; 0 = new content fully visible.
    void SetCoverOpacity(qreal opacity);
    // Forget the "before" look while keeping the cover up (between the two halves of a fade).
    void DropSnapshot();
    void Finish();
    bool IsRunning() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap _snapshot;
    qreal _snapshotOpacity{0.0};
    qreal _coverOpacity{0.0};
    bool _running{false};
};

} // namespace Gui::Widget
