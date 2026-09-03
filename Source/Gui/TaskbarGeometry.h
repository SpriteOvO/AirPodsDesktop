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

#include <algorithm>
#include <cmath>

#include <QRect>
#include <QSize>

namespace Gui::TaskbarGeometry {

struct Layout {
    QRect statusLogical;
    QRect taskButtonsNative;
};

constexpr inline int kDefaultDpi = 96;

inline int NativeToLogical(int value, int dpi)
{
    const auto effectiveDpi = dpi > 0 ? dpi : kDefaultDpi;
    return static_cast<int>(std::lround(static_cast<double>(value) * kDefaultDpi / effectiveDpi));
}

inline int LogicalToNative(int value, int dpi)
{
    const auto effectiveDpi = dpi > 0 ? dpi : kDefaultDpi;
    return static_cast<int>(std::lround(static_cast<double>(value) * effectiveDpi / kDefaultDpi));
}

inline Layout CalculateLayout(
    const QSize &rebarNativeSize, const QRect &taskButtonsNative, int dpi, bool horizontal,
    const QSize &statusLogicalSize = {60, 40})
{
    auto adjustedTaskButtons = taskButtonsNative;
    QRect status;

    if (horizontal) {
        const auto statusNativeWidth = LogicalToNative(statusLogicalSize.width(), dpi);
        adjustedTaskButtons.setWidth(
            std::max(0, rebarNativeSize.width() - statusNativeWidth - taskButtonsNative.left()));
        status = {
            NativeToLogical(rebarNativeSize.width(), dpi) - statusLogicalSize.width(), 0,
            statusLogicalSize.width(), NativeToLogical(taskButtonsNative.height(), dpi)};
    }
    else {
        const auto statusNativeHeight = LogicalToNative(statusLogicalSize.height(), dpi);
        adjustedTaskButtons.setHeight(
            std::max(0, rebarNativeSize.height() - statusNativeHeight - taskButtonsNative.top()));
        status = {
            0, NativeToLogical(rebarNativeSize.height(), dpi) - statusLogicalSize.height(),
            NativeToLogical(taskButtonsNative.width(), dpi), statusLogicalSize.height()};
    }

    return {.statusLogical = status, .taskButtonsNative = adjustedTaskButtons};
}

inline QRect
RestoreTaskButtons(const QSize &rebarNativeSize, const QRect &taskButtonsNative, bool horizontal)
{
    auto restored = taskButtonsNative;
    if (horizontal) {
        restored.setWidth(std::max(0, rebarNativeSize.width() - taskButtonsNative.left()));
    }
    else {
        restored.setHeight(std::max(0, rebarNativeSize.height() - taskButtonsNative.top()));
    }
    return restored;
}

} // namespace Gui::TaskbarGeometry
