#pragma once

#include <QSystemTrayIcon>

namespace Gui {

enum class TrayActivationAction { None, ShowMainWindow, QuickConnect };

class TrayActivationState final
{
public:
    TrayActivationAction
    OnActivation(QSystemTrayIcon::ActivationReason reason, bool quickConnectEnabled)
    {
        if (reason == QSystemTrayIcon::Trigger) {
            if (!quickConnectEnabled) {
                return TrayActivationAction::ShowMainWindow;
            }
            _singleClickPending = true;
            return TrayActivationAction::None;
        }

        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::MiddleClick) {
            _singleClickPending = false;
            return TrayActivationAction::ShowMainWindow;
        }

        return TrayActivationAction::None;
    }

    TrayActivationAction OnSingleClickTimeout()
    {
        if (!_singleClickPending) {
            return TrayActivationAction::None;
        }

        _singleClickPending = false;
        return TrayActivationAction::QuickConnect;
    }

private:
    bool _singleClickPending{};
};

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
