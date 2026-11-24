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

#include <QColor>
#include <QTimer>
#include <QWidget>

namespace Gui::Widget {

// An indeterminate spinner like iOS's activity indicator: eight spokes, the brightest one
// stepping clockwise once a second with the others fading behind it. It only ticks while shown.
//
class ActivityIndicator : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kSpokes = 8;

    explicit ActivityIndicator(int size, QWidget *parent = nullptr);

    void SetColor(const QColor &color);
    // The spoke drawn brightest, 0 being straight up.
    int HeadSpoke() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QTimer _timer{this};
    QColor _color{Qt::gray};
    int _head{0};
};

} // namespace Gui::Widget
