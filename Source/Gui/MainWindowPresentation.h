//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#pragma once

#include <optional>

#include <QColor>
#include <QSize>
#include <QRect>
#include <QString>

#include "Base.h"
#include "../Core/AirPods.h"

namespace Gui {

enum class ButtonAction : uint32_t {
    NoButton,
    Bind,
};

struct BatteryPresentation {
    bool visible{false};
    bool charging{false};
    uint32_t value{0};

    bool operator==(const BatteryPresentation &) const = default;
};

struct MainWindowPresentation {
    QString title;
    ButtonAction buttonAction{ButtonAction::NoButton};
    std::optional<Core::AirPods::Model> animationModel;
    BatteryPresentation leftBattery;
    BatteryPresentation rightBattery;
    BatteryPresentation caseBattery;
    bool singleBattery{false};
    std::optional<QColor> animationTint;

    bool operator==(const MainWindowPresentation &) const = default;
};

struct AnimationPresentation {
    QString resource;
    QSize sourceSize;
    bool removeEnclosedBackground{false};

    QString FallbackResource() const;
};

class MainWindowViewModel
{
public:
    void UpdateState(const Core::AirPods::State &state);
    void Available();
    void Unavailable();
    void Disconnect();
    void Bind();
    void Unbind();

    MainWindowPresentation Present() const;

private:
    Status _status{Status::Unavailable};
    std::optional<Core::AirPods::State> _state;
};

AnimationPresentation GetAnimationPresentation(Core::AirPods::Model model);

// The animations are rendered in one finish. Returns the tint that approximates the device's
// actual color, or nothing when the stock render already matches or the color is unknown.
std::optional<QColor> GetAnimationTint(Core::AirPods::Model model, Core::AppleCP::Color color);
QPoint PopupPosition(const QRect &availableGeometry, QSize windowSize, QSize margin);

} // namespace Gui
