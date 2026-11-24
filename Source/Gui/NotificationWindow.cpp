#include "NotificationWindow.h"

#include <QApplication>
#include <QCursor>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include "MainWindow.h"
#include "MainWindowPresentation.h"
#include "Theme.h"

namespace Gui {

NotificationWindow::NotificationWindow(QWidget *parent)
    : QWidget{
          parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                      Qt::WindowDoesNotAcceptFocus},
      _title{new QLabel{this}}, _message{new QLabel{this}}, _progress{new QProgressBar{this}},
      _close{new QPushButton{QString::fromUtf8("\xC3\x97"), this}}
{
    setObjectName("connectionNotification");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setProperty(Theme::Manager::kSkipDwmProperty, true);
    setFixedWidth(320);

    _title->setObjectName("notificationTitle");
    _message->setObjectName("notificationMessage");
    for (auto *label : {_title, _message}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignCenter);
    }
    auto titleFont = _title->font();
    titleFont.setPointSize(15);
    titleFont.setWeight(QFont::DemiBold);
    _title->setFont(titleFont);
    _title->setProperty("fontRole", "display");
    _close->setObjectName("notificationClose");
    _close->setFixedSize(25, 25);
    _close->setFocusPolicy(Qt::NoFocus);
    _close->setCursor(Qt::PointingHandCursor);
    _close->setAccessibleName(QCoreApplication::translate("Gui::SettingsWindow", "Close"));
    connect(_close, &QPushButton::clicked, this, &QWidget::hide);

    _progress->setRange(0, 0);
    _progress->setTextVisible(false);
    _progress->setFixedHeight(4);
    auto *layout = new QVBoxLayout{this};
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(_close, 0, Qt::AlignRight);
    layout->addWidget(_title);
    layout->addWidget(_message);
    layout->addWidget(_progress);
    _autoHide.setSingleShot(true);
    connect(&_autoHide, &QTimer::timeout, this, &QWidget::hide);
    _placementTimer.setInterval(100);
    connect(&_placementTimer, &QTimer::timeout, this, &NotificationWindow::Place);
    connect(
        &Theme::Manager::Instance(), &Theme::Manager::Changed, this,
        &NotificationWindow::ApplyTheme);
    ApplyTheme();
}

void NotificationWindow::Show(
    const QString &title, const QString &message, bool warning, bool pending,
    const QRect &trayGeometry)
{
    _autoHide.stop();
    // Resolve the cursor fallback once, so overflow-tray notifications do not follow the mouse.
    if (!trayGeometry.isEmpty() || !isVisible() || _targetScreen == nullptr) {
        _targetScreen = QGuiApplication::screenAt(
            trayGeometry.isEmpty() ? QCursor::pos() : trayGeometry.center());
    }
    if (_targetScreen == nullptr) {
        _targetScreen = screen();
    }
    _warning = warning;
    _title->setText(title);
    _message->setText(message);
    _progress->setVisible(pending);
    ApplyTheme();
    layout()->activate();
    adjustSize();
    Place();
    show();
    _placementTimer.start();
    if (!pending) {
        _autoHide.start(warning ? 8000 : 3500);
    }
}

void NotificationWindow::Place()
{
    auto *targetScreen = _targetScreen.data();
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    const auto available = targetScreen->availableGeometry();
    auto target = PopupPosition(available, size(), {24, 24});
    QRect card{target, size()};
    for (auto *window : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(window) == nullptr || !window->isVisible()) {
            continue;
        }
        const auto battery = window->frameGeometry();
        // Also avoid the battery card's destination while its slide-in is running.
        const auto batteryTarget = PopupPosition(available, window->size(), {24, 24});
        if (window->screen() != targetScreen ||
            (!card.intersects(battery) && !card.intersects(QRect{batteryTarget, window->size()})))
        {
            continue;
        }
        target.setY(batteryTarget.y() - height() - 12);
        if (target.y() < available.top()) {
            target =
                QPoint{batteryTarget.x() - width() - 12, available.bottom() + 1 - height() - 24};
        }
        target.setX(qMax(available.left(), target.x()));
        target.setY(qMax(available.top(), target.y()));
        break;
    }
    if (pos() != target) {
        move(target);
    }
}

void NotificationWindow::ApplyTheme()
{
    const auto &colors = Theme::Manager::Instance().Colors();
    _title->setStyleSheet(
        QString{"color: %1; background: transparent;"}.arg(colors.mainText.name()));
    _message->setStyleSheet(
        QString{"color: %1; background: transparent;"}.arg(
            (_warning ? colors.errorText : colors.mainTextSecondary).name()));
    _close->setStyleSheet(
        QString{"QPushButton { background: %1; color: %2; border: none; border-radius: 12px; "
                "padding: 0; font-size: 20px; } QPushButton:hover { background: %3; } "
                "QPushButton:pressed { background: %4; }"}
            .arg(
                colors.mainCloseBg.name(), colors.mainCloseGlyph.name(),
                colors.mainCloseHover.name(), colors.mainClosePressed.name()));
    _progress->setStyleSheet(
        QString{"QProgressBar { border: none; background: %1; border-radius: 2px; } "
                "QProgressBar::chunk { background: %2; border-radius: 2px; }"}
            .arg(colors.mainCloseBg.name(), colors.accent.name()));
    update();
}

void NotificationWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::Manager::Instance().Colors().mainSurface);
    painter.drawRoundedRect(QRectF{rect()}, 32, 32);
}

void NotificationWindow::hideEvent(QHideEvent *event)
{
    _autoHide.stop();
    _placementTimer.stop();
    QWidget::hideEvent(event);
}

} // namespace Gui
