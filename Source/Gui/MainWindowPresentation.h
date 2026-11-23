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
//   BothPodsOut  both taken out: keeps turning for a while
//   PodsOnly     after that: pods without the case, still, a single ring for the pair
enum class Scene : uint32_t {
    PodsInCase,
    OnePodOut,
    BothPodsOut,
    PodsOnly,
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
    // The window decides when "both out" has lasted long enough to drop the case.
    void SetPodsOnly(bool podsOnly);
    bool IsPodsOnly() const;
    void Available();
    void Unavailable();
    void Disconnect();
    void Bind();
    void Unbind();

    MainWindowPresentation Present() const;

private:
    Status _status{Status::Unavailable};
    std::optional<Core::AirPods::State> _state;
    bool _podsOnly{false};
};

AnimationPresentation GetAnimationPresentation(Core::AirPods::Model model);
QPoint PopupPosition(const QRect &availableGeometry, QSize windowSize, QSize margin);

} // namespace Gui
