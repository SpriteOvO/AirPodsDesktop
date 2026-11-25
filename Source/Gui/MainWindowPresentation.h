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
#include <vector>

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

enum class BatteryBadge : uint32_t {
    None,
    Left,
    Right,
    Case,
};

// What the sheet shows, following iOS:
//   PodsInCase   both pods rest in the open case: turntable render, merged ring + case ring
//   OnePodOut    one pod taken out: still render, L/R rings + case ring
//   BothPodsOut  both taken out: turntable render, L/R rings (merged when they match) + case ring
//   Headset      over-ear models with one battery and no case: turntable render, a single ring
enum class Scene : uint32_t {
    PodsInCase,
    OnePodOut,
    BothPodsOut,
    Headset,
};

struct BatteryPresentation {
    bool visible{false};
    bool charging{false};
    uint32_t value{0};
    BatteryBadge badge{BatteryBadge::None};

    bool operator==(const BatteryPresentation &) const = default;
};

struct MainWindowPresentation {
    QString title;
    ButtonAction buttonAction{ButtonAction::NoButton};
    std::optional<Core::AirPods::Model> animationModel;
    Scene scene{Scene::PodsInCase};
    BatteryPresentation leftBattery;
    BatteryPresentation rightBattery;
    BatteryPresentation caseBattery;
    // The advertised color code; it picks the device's own color render where one exists.
    Core::AppleCP::Color animationColor{Core::AppleCP::Color::White};

    bool operator==(const MainWindowPresentation &) const = default;
};

struct AnimationPresentation {
    QString resource;
    QSize sourceSize;
    // Horizontal centres of the pods and the case in the render, as fractions of its width. They
    // are the turntable axes measured over every frame, so the battery rings sit under them.
    qreal podsCenter{0.5};
    std::optional<qreal> caseCenter;

    QString FallbackResource() const;
    // Where the pictures of the separate pods and the case are, for the still scene. Models
    // without a charging case have none.
    QString StillDirectory() const;
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

// The render of `model` in `color`. Color codes follow Apple's device asset catalog (the `CL`
// field), which is what the device advertises; a code without a render of its own falls back
// to the model's default color.
AnimationPresentation GetAnimationPresentation(
    Core::AirPods::Model model, Core::AppleCP::Color color = Core::AppleCP::Color::White);
// Every color code that has a render of its own for `model`, default color first.
std::vector<Core::AppleCP::Color> GetAnimationColors(Core::AirPods::Model model);
QPoint PopupPosition(const QRect &availableGeometry, QSize windowSize, QSize margin);

} // namespace Gui
