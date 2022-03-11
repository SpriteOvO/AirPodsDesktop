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

#include "TaskbarStatus.h"

#include <QWindow>
#include <QApplication>
#include <QDesktopWidget>

#include "../Core/OS/Windows.h"
#include "../Application.h"

//
// Windows 10
//
// +-------------------------------------------------------------------------------------------+
// |                                       Shell_TaryWnd                                       |
// |                  +---------------------------------------------------------+              |
// |                  |                      ReBarWindow32                      |              |
// |  Start and Pins  | +------------------------------------+ +--------------+ |  Tray Icons  |
// |                  | |           MSTaskSwWClass           | |  Our Window  | |              |
// |                  | +------------------------------------+ +--------------+ |              |
// |                  +---------------------------------------------------------+              |
// +-------------------------------------------------------------------------------------------+
//
// Windows 11
//
// +-------------------------------------------------------------------------------------------+
// |                                       Shell_TaryWnd                                       |
// | +--------------------------------------------------------------------------+              |
// | |                               ReBarWindow32                              |              |
// | |                  +------------------------------------+ +--------------+ |  Tray Icons  |
// | |  Start and Pins  |           MSTaskSwWClass           | |  Our Window  | |              |
// | |                  +------------------------------------+ +--------------+ |              |
// | +--------------------------------------------------------------------------+              |
// +-------------------------------------------------------------------------------------------+
//
// ----------
// * All these windows are the same height.
//

std::optional<TaskBarInfo> GetTaskBarInfo()
{
    std::optional<TaskBarInfo> result;

    do {
        HWND hShellTrayWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (hShellTrayWnd == nullptr) {
            LOG(Warn, "Find window 'Shell_TrayWnd' failed.");
            break;
        }

        HWND hReBarWindow32 = FindWindowExW(hShellTrayWnd, nullptr, L"ReBarWindow32", nullptr);
        if (hReBarWindow32 == nullptr) {
            LOG(Warn, "Find window 'ReBarWindow32' failed.");
            break;
        }

        HWND hMSTaskSwWClass = FindWindowExW(hReBarWindow32, nullptr, L"MSTaskSwWClass", nullptr);
        if (hMSTaskSwWClass == nullptr) {
            LOG(Warn, "Find window 'MSTaskSwWClass' failed.");
            break;
        }

        RECT rectShellTrayWnd{}, rectReBarWindow32{}, rectMSTaskSwWClass{},
            rectMSTaskSwWClassForParent{};

        if (!GetWindowRect(hShellTrayWnd, &rectShellTrayWnd)) {
            LOG(Warn, "Failed to get the rect of window 'Shell_TrayWnd'.");
            break;
        }

        if (!GetWindowRect(hReBarWindow32, &rectReBarWindow32)) {
            LOG(Warn, "Failed to get the rect of window 'ReBarWindow32'.");
            break;
        }

        if (!GetWindowRect(hMSTaskSwWClass, &rectMSTaskSwWClass)) {
            LOG(Warn, "Failed to get the rect of window 'MSTaskSwWClass'.");
            break;
        }

        rectMSTaskSwWClassForParent = rectMSTaskSwWClass;
        if (MapWindowPoints(
                HWND_DESKTOP, hReBarWindow32, (POINT *)&rectMSTaskSwWClassForParent, 2) == 0) {
            LOG(Warn, "Failed to get the rect of the parent of window 'MSTaskSwWClass'.");
            break;
        }

        using Core::OS::Windows::Window::RectToQRect;

        auto qrectShellTrayWnd = RectToQRect(rectReBarWindow32);

        result = TaskBarInfo{
            .isHorizontal = qrectShellTrayWnd.width() > qrectShellTrayWnd.height(),
            .hShellTrayWnd = hShellTrayWnd,
            .hReBarWindow32 = hReBarWindow32,
            .hMSTaskSwWClass = hMSTaskSwWClass,
            .rectReBarWindow32 = RectToQRect(rectReBarWindow32),
            .rectMSTaskSwWClass = RectToQRect(rectMSTaskSwWClass),
            .rectMSTaskSwWClassForParent = RectToQRect(rectMSTaskSwWClassForParent),
        };

    } while (false);

    return result;
}

namespace Gui {

TaskbarStatus::TaskbarStatus(QWidget *parent) : QDialog{parent}
{
    using Utils::Qt::SetPaletteColor;

    _ui.setupUi(this);

    _isWin11OrGreater = Core::OS::Windows::System::Is11OrGreater();
    LOG(Info, "Is Windows 11 or greater: '{}'", _isWin11OrGreater);

    connect(this, &TaskbarStatus::OnSettingsChangedSafely, this, &TaskbarStatus::OnSettingsChanged);

    _updateTimer.callOnTimeout([this] { OnUpdateTimer(); });

    //
    // `Qt::FramelessWindowHint` will cause a qt internal error:
    //
    // "UpdateLayeredWindowIndirect failed for ptDst=(1346, 0), size=(80x40), dirty=(80x40 0, 0)
    // (The parameter is incorrect.)"
    //
    // It is printed at
    // https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/windows/qwindowsbackingstore.cpp?h=v5.15.2#n108
    //
    // We remove the flag `Qt::FramelessWindowHint` to make it call `BitBlt` instead of
    // `UpdateLayeredWindowIndirect`, and probably thanks to the later `setParent` call (I guess),
    // the actual effect will be the same as if the flag was set.
    //
    setWindowFlags(
        windowFlags() | Qt::Tool /* | Qt::FramelessWindowHint*/ | Qt::WindowStaysOnTopHint);

    SetPaletteColor(_ui.labelLeft, QPalette::WindowText, Qt::white);
    SetPaletteColor(_ui.labelRight, QPalette::WindowText, Qt::white);

    layout()->setContentsMargins(5, 5, 5, 5);

    _icon.left->SetText("L");
    _icon.right->SetText("R");
    _battery.left->setBatterySize(25, 10);
    _battery.left->setShowText(false);
    _battery.left->setChargingIconColor(Qt::white);
    _battery.right->setBatterySize(25, 10);
    _battery.right->setShowText(false);
    _battery.right->setChargingIconColor(Qt::white);

    _ui.horizontalLayoutLeft->insertWidget(0, _icon.left);
    _ui.horizontalLayoutLeft->addWidget(_battery.left);
    _ui.horizontalLayoutRight->insertWidget(0, _icon.right);
    _ui.horizontalLayoutRight->addWidget(_battery.right);
}

TaskbarStatus::~TaskbarStatus()
{
    _isStateReady = false;
    UpdateVisible();
}

void TaskbarStatus::UpdateState(const Core::AirPods::State &state)
{
    _status = Status::Updating;
    _airPodsState = state;
    Repaint();
}

void TaskbarStatus::UpdateVisible()
{
    if (_behavior != TaskbarStatusBehavior::Disable && _isStateReady) {
        if (!_isActuallyEnabled) {
            if (Enable()) {
                _isActuallyEnabled = true;
            }
        }
    }
    else {
        if (_isActuallyEnabled) {
            if (Disable()) {
                _isActuallyEnabled = false;
            }
        }
    }
}

bool TaskbarStatus::Enable()
{
    const auto optInfo = GetTaskBarInfo();
    if (!optInfo.has_value()) {
        LOG(Error, "Try to enable, but failed to `GetTaskBarInfo()`");
        return false;
    }
    const auto &info = optInfo.value();

    winId(); // makes `windowHandle()` have a value
    windowHandle()->setParent(QWindow::fromWinId((WId)info.hReBarWindow32));
    setAttribute(Qt::WA_TranslucentBackground);
    UpdatePos(info, true);
    _isFirstTimeout = true;
    _updateTimer.start(kUpdateInterval);
    show();
    return true;
}

bool TaskbarStatus::Disable()
{
    const auto optInfo = GetTaskBarInfo();
    if (!optInfo.has_value()) {
        LOG(Error, "Try to disable, but failed to `GetTaskBarInfo()`");
        return false;
    }
    const auto &info = optInfo.value();

    hide();
    _updateTimer.stop();
    UpdatePos(info, false);
    return true;
}

void TaskbarStatus::UpdatePos(const TaskBarInfo &info, bool enable)
{
    LOG(Trace, "The taskbar is '{}'", info.isHorizontal ? "horizontal" : "vertical");

    const auto &rectMSTaskSwWClass = info.rectMSTaskSwWClass;
    const auto &rectMSTaskSwWClassForParent = info.rectMSTaskSwWClassForParent;
    const auto &rectReBarWindow32 = info.rectReBarWindow32;

    if (enable) {
        if (!_isWin11OrGreater) {
            if (info.isHorizontal) {
                // Avoid causing multiple moves
                auto newWidth = rectReBarWindow32.right() - kFixedWidth - rectMSTaskSwWClass.left();

                MoveWindow(
                    info.hMSTaskSwWClass, rectMSTaskSwWClassForParent.left(),
                    rectMSTaskSwWClassForParent.top(), newWidth,
                    rectMSTaskSwWClassForParent.height(), true);

                move(rectReBarWindow32.width() - kFixedWidth, 0);
            }
            else {
                // Same as above
                auto newHeight =
                    rectReBarWindow32.bottom() - kFixedHeight - rectMSTaskSwWClass.top();

                MoveWindow(
                    info.hMSTaskSwWClass, rectMSTaskSwWClassForParent.left(),
                    rectMSTaskSwWClassForParent.top(), rectMSTaskSwWClassForParent.width(),
                    newHeight, true);

                move(0, rectReBarWindow32.height() - kFixedHeight);
            }
        }
        else {
            if (info.isHorizontal) {
                move(rectReBarWindow32.width() - kFixedWidth, 0);
            }
            else {
                // Currently Windows 11 does not support vertical taskbar, so we were unable to test
                // it.
                move(0, rectReBarWindow32.height() - kFixedHeight);
            }
        }

        if (info.isHorizontal) {
            _cachedLength = rectReBarWindow32.width();
            setFixedSize(kFixedWidth, info.rectMSTaskSwWClass.height());
        }
        else {
            _cachedLength = rectReBarWindow32.height();
            setFixedSize(info.rectMSTaskSwWClass.width(), kFixedHeight);
        }
    }
    else {
        if (!_isWin11OrGreater) {
            if (info.isHorizontal) {
                MoveWindow(
                    info.hMSTaskSwWClass, rectMSTaskSwWClassForParent.left(),
                    rectMSTaskSwWClassForParent.top(),
                    rectReBarWindow32.right() - rectMSTaskSwWClass.left(),
                    rectMSTaskSwWClass.height(), true);
            }
            else {
                MoveWindow(
                    info.hMSTaskSwWClass, rectMSTaskSwWClassForParent.left(),
                    rectMSTaskSwWClassForParent.top(), rectMSTaskSwWClass.width(),
                    rectReBarWindow32.bottom() - rectMSTaskSwWClass.top(), true);
            }
        }
    }
}

void TaskbarStatus::Unavailable()
{
    _status = Status::Unavailable;
    _airPodsState.reset();
    Repaint();
}

void TaskbarStatus::Disconnect()
{
    _status = Status::Disconnected;
    _airPodsState.reset();
    Repaint();
}

void TaskbarStatus::Unbind()
{
    _status = Status::Unbind;
    _airPodsState.reset();
    Repaint();
}

void TaskbarStatus::Repaint()
{
    switch (_status) {
    case Status::Unavailable:
    case Status::Disconnected:
    case Status::Unbind: {
        _isStateReady = false;
        break;
    }
    case Status::Updating: {
        if (!_airPodsState.has_value()) {
            _isStateReady = false;
            break;
        }
        const auto &state = _airPodsState.value();

        if (state.pods.left.battery.Available()) {
            const auto batteryValue = state.pods.left.battery.Value();

            _ui.labelLeft->setText(QString{"%1%"}.arg(batteryValue));
            _battery.left->setValue(batteryValue);
            _battery.left->setCharging(state.pods.left.isCharging);

            _icon.left->show();
            if (_behavior == TaskbarStatusBehavior::Text) {
                _ui.labelLeft->show();
                _battery.left->hide();
            }
            else {
                _ui.labelLeft->hide();
                _battery.left->show();
            }
        }
        else {
            _icon.left->hide();
            _ui.labelLeft->hide();
            _battery.left->hide();
        }

        if (state.pods.right.battery.Available()) {
            const auto batteryValue = state.pods.right.battery.Value();

            _ui.labelRight->setText(QString{"%1%"}.arg(batteryValue));
            _battery.right->setValue(batteryValue);
            _battery.right->setCharging(state.pods.right.isCharging);

            _icon.right->show();
            if (_behavior == TaskbarStatusBehavior::Text) {
                _ui.labelRight->show();
                _battery.right->hide();
            }
            else {
                _ui.labelRight->hide();
                _battery.right->show();
            }
        }
        else {
            _icon.right->hide();
            _ui.labelRight->hide();
            _battery.right->hide();
        }

        _isStateReady = true;
        break;
    }
    default:
        APD_ASSERT(false);
    }

    setToolTip(ApdApp->GetTrayIcon()->GetToolTip());

    UpdateVisible();
}

void TaskbarStatus::OnUpdateTimer()
{
    const auto optInfo = GetTaskBarInfo();
    if (!optInfo.has_value()) {
        LOG(Trace, "Try to update, but failed to `GetTaskBarInfo()`");
        return;
    }
    const auto &info = optInfo.value();

    bool taskbarResized = _cachedLength != (info.isHorizontal ? info.rectReBarWindow32.width()
                                                              : info.rectReBarWindow32.height());
    bool needToUpdate = taskbarResized || _isFirstTimeout;

    // We update the position again at the second time after the window is displayed, because the
    // first update may cause some shifting, I guess it's `setParent` causing some weird Qt bugs.
    if (_isFirstTimeout) {
        _isFirstTimeout = false;
    }

    if (taskbarResized) {
        LOG(Info, "Taskbar resized, status window need to update position");
    }

    if (needToUpdate) {
        UpdatePos(info, true);
    }
}

void TaskbarStatus::OnSettingsChanged(TaskbarStatusBehavior value)
{
    _behavior = value;
    Repaint();
}

void TaskbarStatus::paintEvent(QPaintEvent *event)
{
    QPainter painter{this};

#if defined APD_DEBUG
    if (_drawDebugBorder) {
        painter.setPen(QPen{Qt::red, 1.5f});
        painter.drawRect(rect());
    }
#endif

    //
    // Make transparent areas accept mouse events (only if we have not `setParent`)
    //
    // painter.setPen(Qt::NoPen);
    // painter.setBrush(Qt::black);
    // painter.setOpacity(0.5);
    // painter.drawRect(rect());

    QDialog::paintEvent(event);
}

void TaskbarStatus::mouseReleaseEvent(QMouseEvent *event)
{
    const auto button = event->button();

    if (button == Qt::LeftButton) {
#if defined APD_DEBUG
        _drawDebugBorder = !_drawDebugBorder;
        LOG(Debug, "_drawDebugBorder: {}", _drawDebugBorder);
        repaint();
#endif
        ApdApp->GetMainWindow()->show();
    }
    else if (button == Qt::RightButton) {
        ApdApp->GetTrayIcon()->GetContextMenu()->popup(event->globalPos());
    }

    QDialog::mouseReleaseEvent(event);
}

} // namespace Gui
