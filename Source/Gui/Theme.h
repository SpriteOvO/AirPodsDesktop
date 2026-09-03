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

#include <memory>

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

class QWidget;

namespace Gui::Theme {

enum class Mode { System, Light, Dark };

//
// Colour tokens for the whole application.
//
// The Windows 11 (Fluent) surfaces drive every regular window, while the `main*` and `battery*`
// tokens keep the iOS pairing-card look of the popup window.
//
struct Palette {
    bool dark{false};

    // Windows
    QColor windowBackground, surface, surfaceSecondary, cardBorder, separator;
    QColor text, textSecondary, textDisabled;
    QColor controlFill, controlHover, controlPressed, controlBorder;
    QColor accent, accentHover, accentPressed, accentDisabled, accentText;
    QColor popupSurface, popupBorder;

    // Main window (iOS card)
    QColor mainSurface, mainText, mainTextSecondary;
    QColor mainCloseBg, mainCloseHover, mainClosePressed, mainCloseGlyph;
    QColor batteryBorder, batteryNormal, batteryAlarm;

    bool operator==(const Palette &) const = default;
};

//
// Reads the Windows theme (light/dark, accent colour), turns it into a `QPalette` plus a
// stylesheet, and re-applies both whenever Windows broadcasts a theme change.
//
// GUI thread only.
//
class Manager : public QObject
{
    Q_OBJECT

public:
    // Windows that must not get DWM attributes (frameless popup, taskbar child) set this
    // dynamic property to `true`.
    constexpr static auto kSkipDwmProperty = "apdSkipDwm";

    static Manager &Instance();

    const Palette &Colors() const;
    bool IsDark() const;
    bool IsSystemDark() const;
    QColor Accent() const;
    Mode CurrentMode() const;
    void SetMode(Mode mode);

    QString StyleSheet() const;
    QPalette QtPalette() const;

    void ApplyToApplication();
    void ApplyToWindow(QWidget *topLevel);

Q_SIGNALS:
    void Changed();

private:
    class NativeFilter;
    class Impl;

    std::unique_ptr<Impl> _impl;

    Manager();
    ~Manager() override;

    void Refresh(bool force);
    void OnSystemThemeChanged();

    bool eventFilter(QObject *watched, QEvent *event) override;
};

} // namespace Gui::Theme
