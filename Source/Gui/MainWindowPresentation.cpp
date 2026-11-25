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
    result.animationColor = _state->color;

    if (Core::AirPods::HasSingleBattery(_state->model)) {
        // One battery and no case: the pods/case placement bits carry nothing meaningful here.
        result.scene = Scene::Headset;
        result.rightBattery = PresentBattery(_state->pods.left, BatteryBadge::None);
        return result;
    }

    const int podsInCase =
        (_state->pods.left.isInCase ? 1 : 0) + (_state->pods.right.isInCase ? 1 : 0);
    if (podsInCase == 2) {
        result.scene = Scene::PodsInCase;
    }
    else if (podsInCase == 1) {
        result.scene = Scene::OnePodOut;
    }
    else {
        result.scene = Scene::BothPodsOut;
    }

    result.leftBattery = PresentBattery(_state->pods.left, BatteryBadge::Left);
    result.rightBattery = PresentBattery(_state->pods.right, BatteryBadge::Right);
    if (Core::AirPods::HasChargingCase(_state->model)) {
        result.caseBattery = PresentBattery(_state->caseBox, BatteryBadge::Case);
    }

    // One unlabelled ring for the pair (the weaker pod, charging only if both are) while the
    // pods rest in the case; also whenever both simply report the same.
    const bool matchingPods = result.leftBattery.visible && result.rightBattery.visible &&
                              result.leftBattery.value == result.rightBattery.value &&
                              result.leftBattery.charging == result.rightBattery.charging;
    const bool mergePods = result.scene == Scene::PodsInCase || matchingPods;
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
    return result;
}

namespace {

// A model rendered in each of its colors. All colors share one framing, so the centres hold for
// every render; `colors` lists the color codes with a render, the default color first.
struct ColorRenders {
    Core::AirPods::Model model;
    const char *directory; // under Video/ and Image/Animation/, one `<code>.avi`/`.png` per color
    QSize sourceSize;
    qreal podsCenter;
    std::optional<qreal> caseCenter;
    std::vector<uint8_t> colors;
};

const std::vector<ColorRenders> &ColorRenderTable()
{
    using Core::AirPods::Model;

    // Generated from Apple's device asset catalog: a color's `CL` code is what the device
    // advertises. Centres are the mean turntable axes over all frames.
    static const std::vector<ColorRenders> table{
        // 16 Silver, 2 Red, 3 Blue, 15 Dark Grey, 17 Green
        {Model::AirPods_Max, "AirPods_Max", {320, 432}, 0.5, std::nullopt, {16, 2, 3, 15, 17}},
        // 19 Silver, 18 Black, 20 Blue, 21 Orange, 22 Purple
        {Model::AirPods_Max_USB_C,
         "AirPods_Max_USB_C",
         {320, 432},
         0.5,
         std::nullopt,
         {19, 18, 20, 21, 22}},
        // 1 Black, 0 White, 2 Stone Purple, 3 Sage Gray, 4 Sunset, 5 Lemon Lime, 6 Eventide, 7
        // Mica, 8 Ochre, 9 Umber, 10 Black, 11 Grey, 12 Pink, 13 Orange
        {Model::Beats_Fit_Pro,
         "Beats_Fit_Pro",
         {896, 448},
         0.195,
         0.748,
         {1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
        // 9 Black, 0 White, 1 Gloss Black, 6 Silver, 7 Gold, 8 Rose Gold, 14 Ultra Voilet, 15
        // Product RED, 18 Brick Red, 19 Turf Green, 20 Asphalt Gray, 21 Break Blue, 27 Matte
        // Silver, 29 Matte Gold, 32 Pop Indigo, 33 Pop Magenta, 34 Pop Blue, 35 Pop Violet, 37
        // Defiant Black-Red, 42 Mickey 90th Anniversary Edition, 46 Satin Gold, 47 Satin Silver, 61
        // Club Red, 62 Club Yellow, 63 Club White, 64 Club Navy, 91 Satin Gold, 92 Satin White
        {Model::Beats_Solo3, "Beats_Solo3", {336, 448}, 0.5, std::nullopt, {9,  0,  1,  6,  7,  8,
                                                                            14, 15, 18, 19, 20, 21,
                                                                            27, 29, 32, 33, 34, 35,
                                                                            37, 42, 46, 47, 61, 62,
                                                                            63, 64, 91, 92}},
        // 1 Black, 0 White, 11 Flash Blue, 12 Siren Red, 13 Shock Yellow, 18 Brick Red, 19 Turf
        // Green, 20 Asphalt Gray, 21 Break Blue, 23 Trophy Gold, 32 Pop Indigo, 33 Pop Magenta, 34
        // Pop Blue, 35 Pop Violet, 37 Defiant Black-Red
        {Model::Powerbeats_3,
         "Powerbeats_3",
         {368, 432},
         0.5,
         std::nullopt,
         {1, 0, 11, 12, 13, 18, 19, 20, 21, 23, 32, 33, 34, 35, 37}},
        // 1 Black, 0 White, 3 Blue, 5 Grey, 27 Matte Silver, 29 Matte Gold, 37 Defiant Black-Red,
        // 65 Sky Blue, 73 Red
        {Model::Beats_X,
         "Beats_X",
         {352, 384},
         0.5,
         std::nullopt,
         {1, 0, 3, 5, 27, 29, 37, 65, 73}},
    };
    return table;
}

const ColorRenders *FindColorRenders(Core::AirPods::Model model)
{
    const auto &table = ColorRenderTable();
    const auto it = std::find_if(
        table.cbegin(), table.cend(), [model](const auto &entry) { return entry.model == model; });
    return it == table.cend() ? nullptr : &*it;
}

} // namespace

AnimationPresentation
GetAnimationPresentation(Core::AirPods::Model model, Core::AppleCP::Color color)
{
    using Core::AirPods::Model;

    if (const auto *renders = FindColorRenders(model)) {
        const auto code = static_cast<uint8_t>(color);
        const bool rendered = std::find(renders->colors.cbegin(), renders->colors.cend(), code) !=
                              renders->colors.cend();
        const auto shown = rendered ? code : renders->colors.front();
        return {
            QString{"qrc:/Resource/Video/%1/%2.avi"}.arg(renders->directory).arg(shown),
            renders->sourceSize, renders->podsCenter, renders->caseCenter};
    }

    // The AirPods come in white only. Centres are the mean turntable axes over all frames.
    switch (model) {
    case Model::AirPods_1:
        return {"qrc:/Resource/Video/AirPods_1.avi", {800, 400}, 0.261, 0.732};
    case Model::AirPods_2:
        return {"qrc:/Resource/Video/AirPods_2.avi", {800, 400}, 0.261, 0.732};
    case Model::AirPods_3:
        return {"qrc:/Resource/Video/AirPods_3.avi", {900, 450}, 0.217, 0.776};
    case Model::AirPods_4:
        return {"qrc:/Resource/Video/AirPods_4.avi", {900, 450}, 0.233, 0.755};
    case Model::AirPods_4_ANC:
        return {"qrc:/Resource/Video/AirPods_4_ANC.avi", {900, 450}, 0.233, 0.755};
    case Model::AirPods_5:
        return {"qrc:/Resource/Video/AirPods_4_ANC.avi", {900, 450}, 0.233, 0.755};
    case Model::AirPods_Pro:
        return {"qrc:/Resource/Video/AirPods_Pro.avi", {900, 450}, 0.209, 0.762};
    case Model::AirPods_Pro_2:
    case Model::AirPods_Pro_2_USB_C:
        return {"qrc:/Resource/Video/AirPods_Pro_2.avi", {900, 450}, 0.212, 0.756};
    case Model::AirPods_Pro_3:
        return {"qrc:/Resource/Video/AirPods_Pro_3.avi", {900, 450}, 0.210, 0.733};
    default:
        return {"qrc:/Resource/Video/AirPods_1.avi", {800, 400}, 0.261, 0.732};
    }
}

std::vector<Core::AppleCP::Color> GetAnimationColors(Core::AirPods::Model model)
{
    const auto *renders = FindColorRenders(model);
    if (renders == nullptr) {
        return {Core::AppleCP::Color::White};
    }
    std::vector<Core::AppleCP::Color> colors;
    for (const auto code : renders->colors) {
        colors.push_back(static_cast<Core::AppleCP::Color>(code));
    }
    return colors;
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

QString AnimationPresentation::StillDirectory() const
{
    if (!caseCenter.has_value() || !resource.startsWith("qrc:/Resource/Video/") ||
        !resource.endsWith(".avi"))
    {
        return {};
    }
    auto result = resource;
    result.replace("qrc:/Resource/Video/", ":/Resource/Image/Still/");
    result.chop(4);
    return result;
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
