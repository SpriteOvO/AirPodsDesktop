#pragma once

#include <QSystemTrayIcon>

namespace Gui {

enum class TrayActivationAction { None, ShowMainWindow, QuickConnect };

inline TrayActivationAction
RouteTrayActivation(QSystemTrayIcon::ActivationReason reason, bool quickConnectEnabled)
{
    if (reason == QSystemTrayIcon::Trigger) {
        return quickConnectEnabled ? TrayActivationAction::QuickConnect
                                   : TrayActivationAction::ShowMainWindow;
    }
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::MiddleClick) {
        return TrayActivationAction::ShowMainWindow;
    }
    return TrayActivationAction::None;
}

} // namespace Gui
