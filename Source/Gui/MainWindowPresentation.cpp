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

namespace Gui {
namespace {

BatteryPresentation PresentBattery(const Core::AirPods::Details::BasicState &state)
{
    if (!state.battery.Available()) {
        return {};
    }

    return {
        .visible = true,
        .charging = state.isCharging,
        .value = state.battery.Value(),
    };
}

} // namespace

void MainWindowViewModel::UpdateState(const Core::AirPods::State &state)
{
    _status = Status::Updating;
    _state = state;
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
    result.leftBattery = PresentBattery(_state->pods.left);
    result.rightBattery = PresentBattery(_state->pods.right);
    result.caseBattery = PresentBattery(_state->caseBox);
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

} // namespace Gui
