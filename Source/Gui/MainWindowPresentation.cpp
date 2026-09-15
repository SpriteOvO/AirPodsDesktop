//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MainWindowPresentation.h"

#include <algorithm>

namespace Gui {
namespace {

BatteryPresentation
PresentBattery(const Core::AirPods::Details::BasicState &state, BatteryBadge badge)
{
    if (!state.battery.Available()) {
        return {};
    }

    return {
        .visible = true,
        .charging = state.isCharging,
        .value = state.battery.Value(),
        .badge = badge,
    };
}

} // namespace

void MainWindowViewModel::UpdateState(const Core::AirPods::State &state)
{
    _status = Status::Updating;
    _state = state;
}

void MainWindowViewModel::SetPodsOnly(bool podsOnly)
{
    _podsOnly = podsOnly;
}

bool MainWindowViewModel::IsPodsOnly() const
{
    return _podsOnly;
}

void MainWindowViewModel::Available()
{
    if (_status == Status::Unavailable) {
        _status = Status::Disconnected;
    }
}

void MainWindowViewModel::Unavailable()
{
    _status = Status::Unavailable;
    _state.reset();
}

void MainWindowViewModel::Disconnect()
{
    if (_status == Status::Unbind) {
        return;
    }

    _status = Status::Disconnected;
    _state.reset();
}

void MainWindowViewModel::Bind()
{
    _status = Status::Disconnected;
    _state.reset();
}

void MainWindowViewModel::Unbind()
{
    _status = Status::Unbind;
    _state.reset();
}

MainWindowPresentation MainWindowViewModel::Present() const
{
    MainWindowPresentation result;

    switch (_status) {
    case Status::Unavailable:
    case Status::Disconnected:
    case Status::Unbind:
        result.title = DisplayableStatus(_status);
        break;
    default:
        break;
    }

    if (_status == Status::Unbind) {
        result.buttonAction = ButtonAction::Bind;
    }

    if (!_state.has_value()) {
        return result;
    }

    result.title = _state->displayName;
    result.animationModel = _state->model;

    const int podsInCase = (_state->pods.left.isInCase ? 1 : 0) + (_state->pods.right.isInCase ? 1 : 0);
    if (podsInCase == 2) {
        result.scene = Scene::PodsInCase;
    }
    else if (podsInCase == 1) {
        result.scene = Scene::OnePodOut;
    }
    else {
        result.scene = _podsOnly ? Scene::PodsOnly : Scene::BothPodsOut;
    }

    result.leftBattery = PresentBattery(_state->pods.left, BatteryBadge::Left);
    result.rightBattery = PresentBattery(_state->pods.right, BatteryBadge::Right);
    result.caseBattery = PresentBattery(_state->caseBox, BatteryBadge::Case);

    // One unlabelled ring for the pair (the weaker pod, charging only if both are) while the
    // pods rest in the case or stand on their own; also whenever both simply report the same.
    const bool matchingPods = result.leftBattery.visible && result.rightBattery.visible &&
                              result.leftBattery.value == result.rightBattery.value &&
                              result.leftBattery.charging == result.rightBattery.charging;
    const bool mergePods =
        result.scene == Scene::PodsInCase || result.scene == Scene::PodsOnly || matchingPods;
    if (mergePods) {
        if (result.leftBattery.visible && result.rightBattery.visible) {
            result.rightBattery.value =
                (std::min)(result.leftBattery.value, result.rightBattery.value);
            result.rightBattery.charging =
                result.leftBattery.charging && result.rightBattery.charging;
        }
        else if (result.leftBattery.visible) {
            result.rightBattery = result.leftBattery;
        }
        result.leftBattery = {};
        result.rightBattery.badge = BatteryBadge::None;
    }

    if (_state->caseBox.isBothPodsInCase) {
        result.caseBattery.badge = BatteryBadge::None;
    }
    if (result.scene == Scene::PodsOnly) {
        result.caseBattery = {};
    }
    return result;
}

AnimationPresentation GetAnimationPresentation(Core::AirPods::Model model)
{
    using Core::AirPods::Model;

    switch (model) {
    case Model::AirPods_1:
        return {"qrc:/Resource/Video/AirPods_1.avi", {800, 400}};
    case Model::AirPods_2:
        return {"qrc:/Resource/Video/AirPods_2.avi", {800, 400}};
    case Model::AirPods_3:
        return {"qrc:/Resource/Video/AirPods_3.avi", {900, 450}};
    case Model::AirPods_4:
        return {"qrc:/Resource/Video/AirPods_4.avi", {900, 450}};
    case Model::AirPods_4_ANC:
        return {"qrc:/Resource/Video/AirPods_4_ANC.avi", {900, 450}};
    case Model::AirPods_Pro:
        return {"qrc:/Resource/Video/AirPods_Pro.avi", {900, 450}};
    case Model::AirPods_Pro_2:
    case Model::AirPods_Pro_2_USB_C:
        return {"qrc:/Resource/Video/AirPods_Pro_2.avi", {900, 450}};
    case Model::AirPods_Pro_3:
        return {"qrc:/Resource/Video/AirPods_Pro_3.avi", {900, 450}};
    case Model::AirPods_Max:
    case Model::AirPods_Max_USB_C:
        return {"qrc:/Resource/Video/AirPods_Max.avi", {600, 650}, true};
    case Model::Beats_Fit_Pro:
        return {"qrc:/Resource/Video/Beats_Fit_Pro.avi", {900, 450}};
    case Model::Powerbeats_3:
    case Model::Beats_X:
    case Model::Beats_Solo3:
    default:
        return {"qrc:/Resource/Video/AirPods_1.avi", {800, 400}};
    }
}

QString AnimationPresentation::FallbackResource() const
{
    if (!resource.startsWith("qrc:/Resource/Video/") || !resource.endsWith(".avi")) {
        return {};
    }
    auto result = resource;
    result.replace("qrc:/Resource/Video/", ":/Resource/Image/Animation/");
    result.chop(4);
    return result + ".png";
}

QPoint PopupPosition(const QRect &availableGeometry, QSize windowSize, QSize margin)
{
    return {
        (std::max)(
            availableGeometry.left(),
            availableGeometry.right() + 1 - windowSize.width() - margin.width()),
        (std::max)(
            availableGeometry.top(),
            availableGeometry.bottom() + 1 - windowSize.height() - margin.height())};
}

} // namespace Gui
