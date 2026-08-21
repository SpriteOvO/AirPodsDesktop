#pragma once

#include <QSystemTrayIcon>

namespace Gui {

inline bool IsQuickConnectActivation(QSystemTrayIcon::ActivationReason reason)
{
    return reason == QSystemTrayIcon::Trigger;
}

} // namespace Gui
