//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
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

#include "TrayIcon.h"

#include <QFont>
#include <QPainter>
#include <QSvgRenderer>

#include <Config.h>
#include "../Application.h"
#include "MainWindow.h"

namespace Gui {

TrayIcon::TrayIcon()
{
    connect(_actionNewVersion, &QAction::triggered, this, &TrayIcon::OnNewVersionClicked);
    connect(_actionSettings, &QAction::triggered, this, &TrayIcon::OnSettingsClicked);
    connect(_actionAbout, &QAction::triggered, this, &TrayIcon::OnAboutClicked);
    connect(_actionQuit, &QAction::triggered, qApp, &QApplication::quit, Qt::QueuedConnection);
    connect(_tray, &QSystemTrayIcon::activated, this, &TrayIcon::OnIconClicked);
    connect(_tray, &QSystemTrayIcon::messageClicked, this, [this]() { ShowMainWindow(); });

    connect(
        this, &TrayIcon::OnTrayIconBatteryChangedSafety, this, &TrayIcon::OnTrayIconBatteryChanged);

    _actionNewVersion->setVisible(false);

    _menu->addAction(_actionNewVersion);
    _menu->addSeparator();
    _menu->addAction(_actionSettings);
    _menu->addSeparator();
    _menu->addAction(_actionAbout);
    _menu->addAction(_actionQuit);

    _tray->setContextMenu(_menu);
    _tray->setIcon(ApdApplication::windowIcon());
    _tray->show();

    if (ApdApplication::IsFirstTimeUse()) {
        _tray->showMessage(
            tr("You can find me in the system tray"),
            tr("Click the icon to view battery information, right-click to "
               "customize settings or quit."));
    }
}

void TrayIcon::UpdateState(const Core::AirPods::State &state)
{
    _status = Status::Updating;
    _airPodsState = state;
    Repaint();
}

void TrayIcon::Unavailable()
{
    _status = Status::Unavailable;
    _airPodsState.reset();
    Repaint();
}

void TrayIcon::Disconnect()
{
    _status = Status::Disconnected;
    _airPodsState.reset();
    Repaint();
}

void TrayIcon::Unbind()
{
    _status = Status::Unbind;
    _airPodsState.reset();
    Repaint();
}

void TrayIcon::VersionUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo)
{
    _updateReleaseInfo = releaseInfo;
    _actionNewVersion->setVisible(true);
    Repaint();
}

void TrayIcon::ShowMainWindow()
{
    ApdApp->GetMainWindow()->show();
}

void TrayIcon::Repaint()
{
    QString toolTipContent;
    std::optional<Core::AirPods::Battery::value_type> minBattery;

    switch (_status) {
    case Status::Unavailable:
    case Status::Disconnected:
    case Status::Unbind:
        toolTipContent = DisplayableStatus(_status);
        break;
    case Status::Updating: {
        if (!_airPodsState.has_value()) {
            break;
        }
        const auto &state = _airPodsState.value();

        // toolTipContent += Helper::ToString(state.model);
        toolTipContent += Core::AirPods::GetDisplayName();

        // clang-format off
        if (state.pods.left.battery.has_value()) {
            const auto batteryValue = state.pods.left.battery.value();

            toolTipContent += QString{tr("\nLeft: %1%%2")}
                .arg(batteryValue)
                .arg(state.pods.left.isCharging ? tr(" (charging)") : "");

            minBattery = batteryValue;
        }

        if (state.pods.right.battery.has_value()) {
            const auto batteryValue = state.pods.right.battery.value();

            toolTipContent += QString{tr("\nRight: %1%%2")}
                .arg(batteryValue)
                .arg(state.pods.right.isCharging ? tr(" (charging)") : "");

            if (minBattery.has_value() && batteryValue < minBattery.value() ||
                !minBattery.has_value()) {
                minBattery = batteryValue;
            }
        }

        if (state.caseBox.battery.has_value()) {
            toolTipContent += QString{tr("\nCase: %1%%2")}
                .arg(state.caseBox.battery.value())
                .arg(state.caseBox.isCharging ? tr(" (charging)") : "");
        }
        // clang-format on
        break;
    }
    default:
        APD_ASSERT(false);
    }

    if (_updateReleaseInfo.has_value()) {
        toolTipContent += '\n' + _actionNewVersion->text();
    }

    _tray->setToolTip("AirPodsDesktop\n" + toolTipContent.trimmed());

    // RepaintIcon

    std::optional<QString> iconText;

    if (minBattery.has_value() && _drawBattery) {
        iconText = QString::number(minBattery.value());
    }
    else {
        iconText.reset();
    }

    static const QColor kNewVersionAvailableDot = Qt::yellow;

    auto optIcon = GenerateIcon(
        64, iconText,
        _updateReleaseInfo.has_value() ? std::optional<QColor>{kNewVersionAvailableDot}
                                       : std::nullopt);
    if (optIcon.has_value()) {
        _tray->setIcon(QIcon{QPixmap::fromImage(optIcon.value())});
    }
}

std::optional<QImage> TrayIcon::GenerateIcon(
    int size, const std::optional<QString> &optText, const std::optional<QColor> &dot)
{
    QImage result{size, size, QImage::Format_ARGB32};
    QPainter painter{&result};

    result.fill(Qt::transparent);

    QSvgRenderer{QString{Config::QrcIconSvg}}.render(&painter);

    painter.setRenderHint(QPainter::Antialiasing);

    painter.save();
    do {
        if (!optText.has_value() || optText->isEmpty()) {
            break;
        }
        const auto &text = optText.value();

        static std::unordered_map<int, std::optional<QFont>> trayIconFonts;

        const auto &adjustFont = [](const QString &family,
                                    int desiredSize) -> std::optional<QFont> {
            int lastHeight = 0;

            for (int i = 1; i < 100; i++) {
                QFont font{family, i};
                font.setBold(true);

                int currentHeight = QFontMetrics{font}.height();
                if (currentHeight == desiredSize ||
                    lastHeight < desiredSize && currentHeight > desiredSize) [[unlikely]]
                {
                    LOG(Info,
                        "Found a suitable font for the tray icon. "
                        "Family: '{}', desiredSize: '{}', fontHeight: '{}', fontSize: '{}'",
                        family, desiredSize, currentHeight, i);
                    return font;
                }
                lastHeight = currentHeight;
            }

            LOG(Warn,
                "Cannot find a suitable font for the tray icon. Family: '{}', desiredSize: "
                "'{}'",
                family, desiredSize);

            return std::nullopt;
        };

        auto textHeight = size * 0.8;

        if (!trayIconFonts.contains(textHeight)) {
            trayIconFonts[textHeight] = adjustFont(ApdApp->font().family(), textHeight);
        }

        const auto &optFont = trayIconFonts[textHeight];
        if (!optFont.has_value()) {
            break;
        }
        const auto &font = optFont.value();
        const auto &fontMetrics = QFontMetrics{font};

        const auto textWidth = fontMetrics.width(text);
        textHeight = fontMetrics.height();

        constexpr auto kMargin = QSizeF{2, 0};

        const auto textRect = QRectF{
            (double)size - textWidth - kMargin.width(), size - textHeight - kMargin.height(),
            (double)textWidth, textHeight};
        const auto bgRect = QRectF{
            textRect.left() - kMargin.width(), textRect.top() - kMargin.height(),
            textRect.width() + kMargin.width() * 2, textRect.height() + kMargin.height() * 2};

        painter.setPen(Qt::white);
        painter.setBrush(QColor{255, 36, 66});
        painter.setFont(font);

        painter.drawRoundedRect(bgRect, 10, 10);
        painter.drawText(textRect, text);

    } while (false);
    painter.restore();

    painter.save();
    do {
        if (!dot.has_value()) {
            break;
        }

        const double dotDiameter = size * 0.4;

        painter.setBrush(dot.value());
        painter.drawEllipse(QRectF{size - dotDiameter, 0, dotDiameter, dotDiameter});
    } while (false);
    painter.restore();

    return result;
}

void TrayIcon::OnNewVersionClicked()
{
    APD_ASSERT(_updateReleaseInfo.has_value());

    _actionNewVersion->setVisible(false);

    auto releaseInfo = std::move(_updateReleaseInfo.value());
    _updateReleaseInfo.reset();
    Repaint();

    ApdApp->GetMainWindow()->AskUserUpdate(releaseInfo);
}

void TrayIcon::OnSettingsClicked()
{
    if (!_settingsWindow.isVisible() ||
        _settingsWindow.GetTabCurrentIndex() == _settingsWindow.GetTabCount() - 1)
    {
        _settingsWindow.SetTabIndex(0);
    }

    _settingsWindow.show();
    _settingsWindow.raise();
}

void TrayIcon::OnAboutClicked()
{
    _settingsWindow.SetTabIndex(_settingsWindow.GetTabCount() - 1);
    _settingsWindow.show();
    _settingsWindow.raise();
}

void TrayIcon::OnIconClicked(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger ||
        reason == QSystemTrayIcon::MiddleClick)
    {
        ShowMainWindow();
    }
}

void TrayIcon::OnTrayIconBatteryChanged(bool value)
{
    _drawBattery = value;

    auto optState = Core::AirPods::GetCurrentState();
    if (optState.has_value()) {
        UpdateState(optState.value());
    }
}

} // namespace Gui
